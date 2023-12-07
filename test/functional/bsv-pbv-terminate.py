#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing whether a newer (better) block with more chain work terminates validation of one
older block that is currently validating

The situation looks like this:
      1
   /    \
2,3,4    5
        / \
       6   7

1. 2,3,4,5 are send for parallel validation
2. block 5 will not be considered as there are already maxparallelblocksperpeer
   siblings being processed
3. send blocks 6,7. Because there are only 4 validation slots block 2 validation
   is terminated and swapped with block 7.
3. let through block7 that should be active in the end

The log file is checked to verify termination
"""
import glob

from test_framework.blocktools import prepare_init_chain
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
    wait_for_validating_blocks,
    wait_for_not_validating_blocks
)


class PBVTerminate(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1",
                            "-maxparallelblocks=3",
                            "-maxparallelblocksperpeer=3"]]

    def run_test(self):
        block_count = 0

        # Create a P2P connection
        node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection)

        node1 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node1)
        node1.add_connection(connection)

        node2 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node2)
        node2.add_connection(connection)

        node3 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node3)
        node3.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()
        node1.wait_for_verack()
        node2.wait_for_verack()
        node3.wait_for_verack()

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))

        _, out, block_count = prepare_init_chain(self.chain, 101, 100, block_0=False, start_block=0, node=node0)

        self.log.info("waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)

        tip_block_num = block_count-1

        block2 = self.chain.next_block(block_count, spend=out[0], extra_txns=8)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block3 = self.chain.next_block(block_count, spend=out[0], extra_txns=10)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block4 = self.chain.next_block(block_count, spend=out[0], extra_txns=12)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block5 = self.chain.next_block(block_count, spend=out[0], extra_txns=14)
        block5_num = block_count
        block_count += 1

        block6 = self.chain.next_block(block_count, spend=out[1], extra_txns=8)
        block_count += 1

        self.chain.set_tip(block5_num)

        block7 = self.chain.next_block(block_count, spend=out[1], extra_txns=10)

        self.log.info(f"block2 hash: {block2.hash}")
        self.nodes[0].waitaftervalidatingblock(block2.hash, "add")
        self.log.info(f"block3 hash: {block3.hash}")
        self.nodes[0].waitaftervalidatingblock(block3.hash, "add")
        self.log.info(f"block4 hash: {block4.hash}")
        self.nodes[0].waitaftervalidatingblock(block4.hash, "add")

        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block2.hash, block3.hash, block4.hash}, self.nodes[0], self.log)

        node0.send_message(msg_block(block2))
        # make sure we started validating block2 first as we expect this one to
        # be terminated later on in the test before its validation is complete
        # (algorithm for premature termination selects based on block height and
        # and validation duration - those that are in validation with smaller
        # height and longer are terminated first)
        wait_for_validating_blocks({block2.hash}, self.nodes[0], self.log)

        node1.send_message(msg_block(block3))
        node2.send_message(msg_block(block4))
        # make sure we started validating blocks
        wait_for_validating_blocks({block2.hash, block3.hash, block4.hash}, self.nodes[0], self.log)

        node3.send_message(msg_block(block5))
        self.log.info(f"block5 hash: {block5.hash}")

        # check log file for logging about which block validation was terminated
        termination_log_found = False
        for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
            if f"Block {block2.hash} will not be considered by the current tip activation as the maximum parallel block" in line:
                termination_log_found = True
                self.log.info("Found line: %s", line.strip())
                break

        self.log.info(f"block6 hash: {block6.hash}")
        self.nodes[0].waitaftervalidatingblock(block6.hash, "add")
        self.log.info(f"block7 hash: {block7.hash}")
        self.nodes[0].waitaftervalidatingblock(block7.hash, "add")

        wait_for_waiting_blocks({block6.hash, block7.hash}, self.nodes[0], self.log)

        node3.send_message(msg_block(block6))
        wait_for_validating_blocks({block6.hash}, self.nodes[0], self.log)

        node3.send_message(msg_block(block7))
        wait_for_validating_blocks({block7.hash}, self.nodes[0], self.log)

        self.nodes[0].waitaftervalidatingblock(block2.hash, "remove")
        # block2 should be canceled.
        wait_for_not_validating_blocks({block2.hash}, self.nodes[0], self.log)

        self.log.info("removing wait status from block7")
        self.nodes[0].waitaftervalidatingblock(block7.hash, "remove")

        # finish block7 validation
        wait_for_not_validating_blocks({block7.hash}, self.nodes[0], self.log)

        # remove wait status from block to finish its validations so the test exits properly
        self.nodes[0].waitaftervalidatingblock(block3.hash, "remove")
        self.nodes[0].waitaftervalidatingblock(block4.hash, "remove")
        self.nodes[0].waitaftervalidatingblock(block6.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # block7 should be active in the end
        assert_equal(block7.hash, self.nodes[0].getbestblockhash())

        # check log file for logging about which block validation was terminated
        termination_log_found = False
        for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
            if f"Block {block2.hash} validation was terminated before completion." in line:
                termination_log_found = True
                self.log.info("Found line: %s", line.strip())
                break

        assert_equal(termination_log_found, True)


if __name__ == '__main__':
    PBVTerminate().main()
