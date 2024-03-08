#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing RPC functionality:
    getcurrentlyvalidatingblocks
    waitaftervalidatingblock
    getwaitingblocks

We send one block to test RPC functionalities
    1. we send block and call waitaftervalidatingblock
    2. then we call getwaitingblocks to ensure hash is in
        the waiting list
    3. then we call getcurrentlyvalidatingblocks to make sure
        we started validating block
    4. After we release waiting status by calling waitaftervalidatingblock again
    5. then we wait with getcurrentlyvalidatinblocks to finish its validation
"""
import time

from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import p2p_port, assert_equal
from bsv_pbv_common import (
    wait_for_waiting_blocks,
    wait_for_validating_blocks
)


class WaitAfterValidation(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        block_count = 0

        # Create a P2P connections
        node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection)

        NetworkThread().start()
        node0.wait_for_verack()

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(block_count)
        block_count += 1
        node0.send_message(msg_block(block))

        block = self.chain.next_block(block_count)
        self.log.info(f"block hash: {block.hash}")
        self.nodes[0].waitaftervalidatingblock(block.hash, "add")

        # make sure block hash is in waiting list
        wait_for_waiting_blocks({block.hash}, self.nodes[0], self.log)

        self.log.info("sending block")
        node0.send_message(msg_block(block))

        # make sure we started validating block
        wait_for_validating_blocks({block.hash}, self.nodes[0], self.log)

        # sleep a bit and check that in the meantime validation hasn't proceeded
        time.sleep(1)
        assert(block.hash != self.nodes[0].getbestblockhash())

        # after validating the block we release its waiting status
        self.nodes[0].waitaftervalidatingblock(block.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        assert_equal(block.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    WaitAfterValidation().main()
