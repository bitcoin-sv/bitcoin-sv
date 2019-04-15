#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import contextlib
from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.cdefs import (ONE_MEGABYTE)
from test_framework.util import *
from test_framework.blocktools import ChainManager

# This test checks input parameter factorMaxSendQueuesBytes, which is setting maxSendQueuesBytes.
# Scenario:
#  Prepare chain with 100 blocks, then add 2 big blocks of size 3MB (oldBlock and newBlock).
#  Create 15 peers. Each of them sends GetData for a block which is 3 MB.
#  Count received BLOCK messages.
# Run this scenario with 3 different cases:
# 1. Run bitcoind with -factorMaxSendQueuesBytes set to 15. Peers are requesting oldBlock.
# 2. Run bitcoind with -factorMaxSendQueuesBytes set to 1. Peers are requesting oldBlock.
# 3. Run bitcoind with -factorMaxSendQueuesBytes set to 1. Peers are requesting newBlock (tip of the chain).
# In cases 1. and 3., all peers should receive the block, in case 2., only 1 peer should receive the block.
# In case 2, downloads are in series, while in cases 1. and 3., downloads are in parallel.
# Third case is special because we are requesting the newest block. Limitations for downloading do not apply here.

class MaxSendQueuesBytesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.chain = ChainManager()
        self.num_nodes = 1
        self.next_block = 0
        self.headerSize = 24
        self.num_peers = 15
        self.excessiveblocksize = 3 * ONE_MEGABYTE

    # Request block "block" from all nodes.
    def requestBlocks(self, test_nodes, block):
        receivedBlocks = []
        def on_block(conn, message): 
            receivedBlocks.append(message.block.calc_sha256())

        getdata_request = msg_getdata([CInv(2, block)])

        for test_node in test_nodes:
            test_node.on_block = on_block
            test_node.send_message(getdata_request)

        # Let bitcoind process and send all the messages.
        for test_node in test_nodes:
            test_node.sync_with_ping()

        return len(receivedBlocks)

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

        # Create the first block with a coinbase output to our key
        block = self.chain.next_block(self.next_block)
        self.next_block += 1
        self.chain.save_spendable_output()
        node.send_message(msg_block(block))

        # Bury the block 100 deep so the coinbase output is spendable
        for i in range(1, 100):
            block = self.chain.next_block(self.next_block)
            self.next_block += 1
            self.chain.save_spendable_output()
            node.send_message(msg_block(block))

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

        @contextlib.contextmanager
        def run_connection(factorMaxSendingBlocksSize, blockSize):
            title = "Send GetData and receive block messages while factorMaxSendingBlockSize is {}.".format(factorMaxSendingBlocksSize)
            logger.debug("setup %s", title)

            args = ["-excessiveblocksize={}".format(self.excessiveblocksize + self.headerSize), 
                    "-blockmaxsize={}".format(self.excessiveblocksize + self.headerSize), 
                    "-factorMaxSendQueuesBytes={}".format(factorMaxSendingBlocksSize)]

            self.start_node(0, args)       

            test_nodes = []
            for i in range(self.num_peers):
                test_nodes.append(NodeConnCB())

            connections = []
            for test_node in test_nodes:
                connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node)
                connections.append(connection)
                test_node.add_connection(connection)

            thr = NetworkThread()
            thr.start()
            for test_node in test_nodes:
                test_node.wait_for_verack()

            logger.debug("before %s", title)
            yield test_nodes
            logger.debug("after %s", title)

            for connection in connections:
                connection.close()
            del connections
            thr.join()
            disconnect_nodes(self.nodes[0],1)
            self.stop_node(0)
            logger.debug("finished %s", title)

        node = self.prepareChain()

         # Mine a big block.
        oldBlock = self.mineBigBlock(node)
        
        # Mine another big block so that the previous block is not the tip of chain.
        newBlock = self.mineBigBlock(node)
        
        self.stop_node(0)

        # Scenario 1: Blocks from bitcoind should be sent in parallel as factorMaxSendQueuesBytes=num_peers.
        with run_connection(self.num_peers, self.excessiveblocksize) as test_nodes:
            numReceivedBlocksParallel = self.requestBlocks(test_nodes, oldBlock)
        assert_equal(self.num_peers, numReceivedBlocksParallel)

        # Scenario 2: Blocks from bitcoind should not be sent in parallel because factorMaxSendQueuesBytes=1 
        # only allows one 3MB to be downloaded at once.
        with run_connection(1, self.excessiveblocksize) as test_nodes:
            numReceivedBlocksSeries = self.requestBlocks(test_nodes, oldBlock)
        # numReceivedBlocksSeries may vary between test runs (based on processing power).
        # But still it should be less than the number of all requested blocks.
        assert_greater_than(numReceivedBlocksParallel, numReceivedBlocksSeries)
        logger.info("%d blocks received when running with factorMaxSendQueuesBytes=%d.", numReceivedBlocksSeries, 1)

        # Scenario 3: Blocks from bitcoind should be sent in parallel, because we are requesting the most recent block.
        with run_connection(1, self.excessiveblocksize) as test_nodes:
            numReceivedBlocksNewBlock = self.requestBlocks(test_nodes, newBlock)
        assert_equal(self.num_peers, numReceivedBlocksNewBlock)


if __name__ == '__main__':
    MaxSendQueuesBytesTest().main()
