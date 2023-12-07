#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test entering and exiting of safe mode by large invalid branch where data of first branch block arrives last
Scenario:
1. Generate two branches. Main branch has SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE blocks from last common block, alternative branch has 10 blocks more.
2. Send SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE - 1 blocks of main branch and whole alternative branch.
4. Node enter safe mode because alternative branch is more than SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE ahead of main chain and inside SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE
5. Send one more block of main chain.
6. Node exits safe mode because alternative branch is no longer inside SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE (it has still more than SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE)
"""
from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.mininode import msg_block, CBlock, CTxOut, msg_headers, CBlockHeader
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_equal
from test_framework.cdefs import SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE, SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE


class ExitSafeModeForDistantFork(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)
        self._last_block_time = 0

    def run_test(self):
        with self.run_node_with_connections("Preparation", 0, None, 2) as (conn1, conn2):
            last_block_time = 0
            conn1.rpc.generate(1)

            branch_1_root, last_block_time = make_block(conn1, last_block_time=last_block_time)
            branch_1_blocks = [branch_1_root]
            for _ in range(SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE):
                new_block, last_block_time = make_block(conn1, branch_1_blocks[-1], last_block_time=last_block_time)
                branch_1_blocks.append(new_block)

            branch_2_root, last_block_time = make_block(conn2, last_block_time=last_block_time)
            branch_2_blocks = [branch_2_root]
            for _ in range(SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE + SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE + 2):
                new_block, last_block_time = make_block(conn2, branch_2_blocks[-1], last_block_time=last_block_time)
                branch_2_blocks.append(new_block)

            # send main branch that should be active tip
            send_by_headers(conn1, branch_1_blocks[:SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE], do_send_blocks=True)

            # send alternative branch - headers only
            send_by_headers(conn2, branch_2_blocks, do_send_blocks=False)

            # active tip is one before last block from branch 1 and branch 2 has status headers-only
            wait_for_tip(conn1, branch_1_blocks[-2].hash)
            wait_for_tip_status(conn1, branch_2_blocks[-1].hash, "headers-only")

            # we should entered the safe mode with UNKNOWN as alternative branch is more than 6 blocks ahead
            # and still in max range of SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE blocks
            try:
                conn1.rpc.getbalance()
                assert False, "Should not come to here, should raise exception in line above."
            except JSONRPCException as e:
                assert e.error["message"] == "Safe mode: Warning: The network does not appear to agree with the local blockchain! Still waiting for block data for more details."

            # add one more block to active chain
            send_by_headers(conn1, branch_1_blocks[SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE:], do_send_blocks=True)

            # active tip is last block from branch 1
            wait_for_tip(conn1, branch_1_blocks[-1].hash)

            # alternative chain is now enough blocks away so we should exit safe mode
            # NOTE: Safe mode level is updated after the new block has already become a tip.
            #       So we must wait because here node may still be in safe mode for a short while.
            def is_not_safemode():
                try:
                    conn1.rpc.getbalance()
                    return True
                except JSONRPCException as e:
                    return False
            wait_until(is_not_safemode, timeout=10, check_interval=0.2) # Only a small timeout is needed since safe mode level is changed shortly after the block has become tip.


if __name__ == '__main__':
    ExitSafeModeForDistantFork().main()
