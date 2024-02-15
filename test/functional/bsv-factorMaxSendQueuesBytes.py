#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.cdefs import (ONE_MEGABYTE)
from test_framework.util import *
from test_framework.blocktools import ChainManager, prepare_init_chain


# This test checks input parameter factormaxsendqueuesbytes, which is setting maxSendQueuesBytes.
# Scenario:
#  Prepare chain with 100 blocks, then add 2 big blocks of size 5MB (oldBlock and newBlock).
#  Create 15 peers. Each of them sends GetData for a block which is 5 MB.
#  Count received BLOCK messages.
# Run this scenario with 3 different cases:
# 1. Run bitcoind with -factormaxsendqueuesbytes=15. Peers are requesting oldBlock.
# 2. Run bitcoind with -factormaxsendqueuesbytes=1. Peers are requesting oldBlock.
# 3. Run bitcoind with -factormaxsendqueuesbytes=1. Peers are requesting newBlock (tip of the chain).
# 4. Run bitcoind with -factormaxsendqueuesbytes=1. Whitelisted peers are requesting newBlock (tip of the chain).
# In cases 1. and 4., all peers should receive the block, in case 2. and 3., some peers receive blocks and other receive reject messages.
# In case 2, downloads are in series, while in cases 1. and 4., downloads are in parallel.
# Forth case is special because we are requesting the newest block. Download limitations for whitelisted peers do not apply.

class MaxSendQueuesBytesTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.chain = ChainManager()
        self.num_nodes = 1
        self.next_block = 0
        self.headerSize = 24
        self.num_peers = 15
        self.excessiveblocksize = 5 * ONE_MEGABYTE
        self.msgOverhead = 512 # Allow for additional memory overhead of queued messages

    # Request block "block" from all nodes.
    def requestBlocks(self, test_nodes, block):
        REJECT_TOOBUSY = int('0x44', 16)

        numberOfRejectedMsgs = 0
        numberOfReceivedBlocks = 0

        def on_block(conn, message):
            nonlocal numberOfReceivedBlocks
            numberOfReceivedBlocks += 1

        def on_reject(conn, message):
            assert_equal(message.code, REJECT_TOOBUSY)
            nonlocal numberOfRejectedMsgs
            numberOfRejectedMsgs += 1

        getdata_request = msg_getdata([CInv(CInv.BLOCK, block)])

        for test_node in test_nodes:
            test_node.cb.on_block = on_block
            test_node.cb.on_reject = on_reject
            test_node.cb.send_message(getdata_request)

        # Let bitcoind process and send all the messages.
        for test_node in test_nodes:
            test_node.cb.sync_with_ping()

        return numberOfReceivedBlocks, numberOfRejectedMsgs

    def prepareChain(self):
        node = NodeConnCB()
        connections = []
        connections.append(
            NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node))
        node.add_connection(connections[0])

        NetworkThread().start()
        node.wait_for_verack()

        # Generate some old blocks
        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        _, _, self.next_block = prepare_init_chain(self.chain, 100, 0, start_block=0, block_0=False, node=node)

        return node

    def mineBigBlock(self, node):
        block = self.chain.next_block(self.next_block, spend=self.chain.get_spendable_output(), block_size=self.excessiveblocksize)
        self.next_block += 1
        self.chain.save_spendable_output()
        node.send_message(msg_block(block))
        node.sync_with_ping()
        createdBlock = self.nodes[0].getbestblockhash()
        logger.info("Big block %s created (%d B)", createdBlock, self.excessiveblocksize)
        createdBlock = int(createdBlock, 16)
        return createdBlock

    def run_test(self):
        node = self.prepareChain()

        # Mine a big block.
        oldBlock = self.mineBigBlock(node)

        # Mine another big block so that the previous block is not the tip of chain.
        newBlock = self.mineBigBlock(node)

        self.stop_node(0)

        # Scenario 1: Blocks from bitcoind should be sent in parallel as factormaxsendqueuesbytes=num_peers.
        args = ["-excessiveblocksize={}".format(self.excessiveblocksize + self.headerSize + self.msgOverhead),
                "-blockmaxsize={}".format(self.excessiveblocksize + self.headerSize),'-rpcservertimeout=500',
                '-maxsendbuffer=1000']
        with self.run_node_with_connections("should be sent in parallel as factormaxsendqueuesbytes=num_peers",
                                            0,
                                            args+["-factormaxsendqueuesbytes={}".format(self.num_peers)],
                                            self.num_peers) as connections:

            start = time.time()
            numberOfReceivedBlocksParallel, numberOfRejectedMsgs = self.requestBlocks(connections, oldBlock)
            logger.info("finished requestBlock duration %s s", time.time() - start)
            assert_equal(self.num_peers, numberOfReceivedBlocksParallel)
            assert_equal(0, numberOfRejectedMsgs)

        # Scenario 2: Blocks from bitcoind should not be sent in parallel because factormaxsendqueuesbytes=1
        # only allows one 5MB to be downloaded at once.
        with self.run_node_with_connections("should not be sent in parallel because factormaxsendqueuesbytes=1",
                                            0,
                                            args+["-factormaxsendqueuesbytes=1"],
                                            self.num_peers) as connections:

            start = time.time()
            numberOfReceivedBlocksSeries, numberOfRejectedMsgs = self.requestBlocks(connections, oldBlock)
            logger.info("finished requestBlock duration %s s", time.time() - start)
            # numReceivedBlocksSeries may vary between test runs (based on processing power).
            # But still we expect the processing to be slow enough that with 15 messages at least one will be rejected.
            logger.info("%d blocks received when running with factormaxsendqueuesbytes=%d.", numberOfReceivedBlocksSeries, 1)
            assert_greater_than(numberOfReceivedBlocksParallel, numberOfReceivedBlocksSeries)
            assert_greater_than(numberOfRejectedMsgs, 0)
            assert_equal(numberOfReceivedBlocksSeries+numberOfRejectedMsgs, 15)

        # Scenario 3: Blocks from bitcoind should not be sent in parallel if we reached rate limit,
        # because we are requesting the most recent block from non whitelisted peer.
        with self.run_node_with_connections(
                "some blocks should be rejected because non whitelisted peer are requesting most recent block", 0,
                args + ["-factormaxsendqueuesbytes=1"], self.num_peers) as connections:
            start = time.time()
            numberOfReceivedBlocksNewBlock, numberOfRejectedMsgs = self.requestBlocks(connections, newBlock)
            logger.info("finished requestBlock duration %s s", time.time() - start)
            logger.info("%d blocks received when running with factormaxsendqueuesbytes=%d.", numberOfReceivedBlocksNewBlock, 1)
            assert_greater_than(self.num_peers, numberOfReceivedBlocksNewBlock)
            assert_greater_than(numberOfRejectedMsgs, 0)

        # Scenario 4: Blocks from bitcoind should be sent in parallel, because there is no limit on whitelisted peers
        # requesting most recent block even if queue is full.
        with self.run_node_with_connections("should be sent in parallel, because we are requesting the most recent block",
                                            0,
                                            args+["-factormaxsendqueuesbytes=1", "-whitelist=127.0.0.1"],
                                            self.num_peers) as connections:

            start = time.time()
            numberOfReceivedBlocksNewBlock, numberOfRejectedMsgs = self.requestBlocks(connections, newBlock)
            logger.info("finished requestBlock duration %s s", time.time() - start)
            assert_equal(self.num_peers, numberOfReceivedBlocksNewBlock)
            assert_equal(0, numberOfRejectedMsgs)


if __name__ == '__main__':
    MaxSendQueuesBytesTest().main()
