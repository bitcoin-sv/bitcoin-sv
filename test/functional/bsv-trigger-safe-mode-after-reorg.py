#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test entering and exiting of safe mode by large invalid branch where data of first branch block arrives last
Scenario:
1. Generate two valid branches.
2. Send first branch and check that it is active
3. Send second branch that has more pow.
4. Wait for reorg to finish and check that second branch is active and first is valid fork.
5. Validate that node enters safe mode.
6. Send additional blocks of second branch. Base of first fork is now too far from active tip to cause safe mode.
7. Validate that node is no longer in safe mode.
"""
from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.mininode import msg_block, CBlock, CTxOut, msg_headers, CBlockHeader
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_equal
from test_framework.cdefs import SAFE_MODE_DEFAULT_MIN_FORK_LENGTH, SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE


class TriggerSafeModeAfterReorg(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        with self.run_node_with_connections("Preparation", 0, None, 2) as (conn1, conn2):
            last_block_time = 0
            conn1.rpc.generate(1)

            branch_1_root, last_block_time = make_block(conn1, last_block_time = last_block_time)
            branch_1_blocks = [branch_1_root]
            for _ in range(SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 1):
                new_block, last_block_time = make_block(conn1, branch_1_blocks[-1], last_block_time = last_block_time)
                branch_1_blocks.append(new_block)

            branch_2_root, last_block_time = make_block(conn2, last_block_time = last_block_time)
            branch_2_blocks = [branch_2_root]
            for _ in range(SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE):
                new_block, last_block_time = make_block(conn2, branch_2_blocks[-1], last_block_time = last_block_time)
                branch_2_blocks.append(new_block)

            # send first branch that should be active tip
            send_by_headers(conn1, branch_1_blocks, do_send_blocks=True)

            # wait for active tip
            wait_for_tip(conn1, branch_1_blocks[-1].hash)

            # send second branch with more POW
            send_by_headers(conn2, branch_2_blocks[:SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 3], do_send_blocks=True)

            # active tip is from branch 2 and branch 1 has status valid-fork
            wait_for_tip(conn1, branch_2_blocks[SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 2].hash)
            wait_for_tip_status(conn1, branch_1_blocks[-1].hash, "valid-fork")

            # we should entered the safe mode with VALID because there is a valid fork with SAFE_MODE_DEFAULT_MIN_VALID_FORK_POW pow
            # and last common block is less than SAFE_MODE_DEFAULT_MAX_VALID_FORK_DISTANCE from active tip
            assert conn1.rpc.getsafemodeinfo()["safemodeenabled"]

            # send more blockst of second branch
            send_by_headers(conn1, branch_2_blocks[SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 3:], do_send_blocks=True)

            # active tip is last block from branch 2
            wait_for_tip(conn1,branch_2_blocks[-1].hash)

            # we should exit safe mode because fork base is too far from active tip
            assert not conn1.rpc.getsafemodeinfo()["safemodeenabled"]


if __name__ == '__main__':
    TriggerSafeModeAfterReorg().main()
