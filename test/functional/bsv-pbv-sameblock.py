#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Send two same blocks via different p2p connection, one of the blocks will
be processed and the other ignored
"""
import glob

from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    p2p_port,
    wait_until
)
from bsv_pbv_common import (
    wait_for_waiting_blocks,
    wait_for_validating_blocks
)


class PBVSameBlock(BitcoinTestFramework):

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

        node1 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node1)
        node1.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()
        node1.wait_for_verack()

        # send one to get out of IBD state
        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(block_count)
        block_count += 1
        node0.send_message(msg_block(block))

        self.nodes[0].waitforblockheight(1)

        block = self.chain.next_block(block_count)

        # set block validating status to wait after validation
        self.nodes[0].waitaftervalidatingblock(block.hash, "add")

        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block.hash}, self.nodes[0], self.log)

        node0.send_message(msg_block(block))
        node1.send_message(msg_block(block))

        # make sure we started validating blocks.
        # One is validating the other is ignored.
        wait_for_validating_blocks({block.hash}, self.nodes[0], self.log)

        def wait_for_log():
            line_text = block.hash + " will not be considered by the current"
            for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
                if line_text in line:
                    self.log.info("Found line: %s", line)
                    return True
            return False

        # wait for the log of the ignored block.
        wait_until(wait_for_log)

        # remove block validating status to finish validation
        self.nodes[0].waitaftervalidatingblock(block.hash, "remove")

        # wait till validation of block finishes
        node0.sync_with_ping()

        self.nodes[0].waitforblockheight(2)
        assert_equal(block.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVSameBlock().main()
