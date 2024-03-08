#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
First test (__test_getdata):

1. Send block via p2p connection (block validation completion is delayed)
2. Request get data before it is validated
3. Complete validation
4. After validation is complete we should get the delayed block reply for get
   data request sent earlier.

This case can happen because we announce a block immediately after receiving it
and performing some basic checks on it. On such messages nodes react by requesting
a compact block but there are cases when compact block is discarded and in such
cases getblock is used to request the entire block - in case this request comes
before we finished validation we need to defer the response and not consume it
without replying.

Second test (__test_getblocks):

1. Send block1 via p2p connection (block validation completion is delayed)
2. Request get blocks before it is validated
2. Send block2 via p2p connection (block validation completion is delayed)
3. Complete validation of block1
4. After validation is complete we should get the delayed block reply for get
   blocks request sent earlier even though block2 is still not validated (as the
   request has been made before that point)
5. Complete validation of block2 just to finish the test
"""

from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    msg_getdata,
    msg_getblocks,
    CInv,
    CBlockLocator
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    p2p_port,
    wait_until,
    check_for_log_msg)
from bsv_pbv_common import (
    wait_for_waiting_blocks,
    wait_for_validating_blocks
)


class PBVCallGetDataBeforeBlockIsValidated(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        block_count = 0

        # Create a P2P connection
        node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()

        # send one to get out of IBD state
        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(block_count)
        block_count += 1
        node0.send_message(msg_block(block))

        self.nodes[0].waitforblockheight(1)

        block_count = self.__test_getdata(node0, block_count)
        block_count = self.__test_getblocks(node0, block_count)

    def __test_getdata(self, node0, block_count):
        block = self.chain.next_block(block_count)
        block_count += 1
        self.log.info(f"block hash: {block.hash}")

        self.__send_blocking_validation_block(block, node0)

        receivedBlock = False

        def on_block(conn, message):
            nonlocal receivedBlock
            message.block.calc_sha256()
            if message.block.sha256 == block.sha256:
                receivedBlock = True
        node0.on_block = on_block
        node0.send_message(msg_getdata([CInv(CInv.BLOCK, int(block.hash, 16))]))

        wait_until(lambda: check_for_log_msg(self, block.hash + " is still waiting as a candidate", "/node0"))

        # remove block validating status to finish validation
        self.nodes[0].waitaftervalidatingblock(block.hash, "remove")

        def wait_for_getdata_reply():
            return receivedBlock
        wait_until(wait_for_getdata_reply)

        return block_count

    def __test_getblocks(self, node0, block_count):
        block1 = self.chain.next_block(block_count)
        block_count += 1
        self.log.info(f"block1 hash: {block1.hash}")

        self.__send_blocking_validation_block(block1, node0)

        receivedBlock = False

        def on_block(conn, message):
            nonlocal receivedBlock
            message.block.calc_sha256()
            if message.block.sha256 == block1.sha256:
                receivedBlock = True
        node0.on_block = on_block
        node0.send_message(msg_getblocks())

        block2 = self.chain.next_block(block_count)
        block_count += 1
        self.log.info(f"block2 hash: {block2.hash}")

        self.__send_blocking_validation_waiting_block(block2, node0)

        wait_until(lambda: check_for_log_msg(self, "Blocks that were received before getblocks message", "/node0"))

        # remove block validating status to finish validation
        self.nodes[0].waitaftervalidatingblock(block1.hash, "remove")

        def wait_for_getblocks_reply():
            return receivedBlock
        wait_until(wait_for_getblocks_reply)

        # remove block validating status to finish validation
        self.nodes[0].waitaftervalidatingblock(block2.hash, "remove")

        # wait till validation of block finishes
        node0.sync_with_ping()

        return block_count

    def __send_blocking_validation_waiting_block(self, block, node0):
        # set block validating status to wait after validation
        self.nodes[0].waitaftervalidatingblock(block.hash, "add")

        # make sure block hash is in the waiting list
        wait_for_waiting_blocks({block.hash}, self.nodes[0], self.log)

        node0.send_message(msg_block(block))

    def __send_blocking_validation_block(self, block, node0):
        self.__send_blocking_validation_waiting_block(block, node0)
        wait_for_validating_blocks({block.hash}, self.nodes[0], self.log)


if __name__ == '__main__':
    PBVCallGetDataBeforeBlockIsValidated().main()
