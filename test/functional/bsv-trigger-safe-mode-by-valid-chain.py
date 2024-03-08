#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test entering and exiting of safe mode by large invalid branch where data of first branch block arrives last
Scenario:
1. Generate two branches. Main branch and alternative branch that has more than SAFE_MODE_DEFAULT_MIN_FORK_LENGTH blocks.
2. Validate that node enters safe mode with VALID safe mode level
3. Restart node and check that we are in safe mode after restart
4. Send some more blocks of main chain so that alternative branch is no longer within SAFE_MODE_DEFAULT_MAX_VALID_FORK_DISTANCE distance
5. Validate that node exited safe mode
"""
from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.mininode import msg_block, CBlock, CTxOut, msg_headers, CBlockHeader
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_equal, p2p_port
from test_framework.cdefs import SAFE_MODE_DEFAULT_MIN_FORK_LENGTH, SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE


class TriggerSafeModeByValidChain(BitcoinTestFramework):

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
            for _ in range(SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE):
                new_block, last_block_time = make_block(conn1, branch_1_blocks[-1], last_block_time = last_block_time)
                branch_1_blocks.append(new_block)

            branch_2_root, last_block_time = make_block(conn2, last_block_time = last_block_time)
            branch_2_blocks = [branch_2_root]
            for _ in range(SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 1):
                new_block, last_block_time = make_block(conn2, branch_2_blocks[-1], last_block_time = last_block_time)
                branch_2_blocks.append(new_block)

            # send main branch that should be active tip
            send_by_headers(conn1, branch_1_blocks[:SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 2], do_send_blocks=True)

            # send alternative branch
            send_by_headers(conn2, branch_2_blocks, do_send_blocks=True)

            # active tip is from branch 1 and brach 2 has status valid-headers
            wait_for_tip(conn1, branch_1_blocks[SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 1].hash)
            wait_for_tip_status(conn1, branch_2_blocks[-1].hash, "valid-headers")

            # we should entered the safe mode with VALID because there is a valid fork with SAFE_MODE_DEFAULT_MIN_VALID_FORK_POW pow
            # and last common block is less than SAFE_MODE_DEFAULT_MAX_VALID_FORK_DISTANCE from active tip
            assert conn1.rpc.getsafemodeinfo()["safemodeenabled"]

        with self.run_node_with_connections("Restart node in safe mode", 0, None, 1) as conn:
            conn1 = conn[0]

            # check that we are in safe mode after restart
            assert conn1.rpc.getsafemodeinfo()["safemodeenabled"]

            # send main branch that should be active tip
            send_by_headers(conn1, branch_1_blocks[SAFE_MODE_DEFAULT_MIN_FORK_LENGTH + 2:], do_send_blocks=True)

            # active tip is last block from branch 1
            wait_for_tip(conn1,branch_1_blocks[-1].hash)

            # we should exit safe mode because fork base is too far from active tip
            assert not conn1.rpc.getsafemodeinfo()["safemodeenabled"]


if __name__ == '__main__':
    TriggerSafeModeByValidChain().main()
