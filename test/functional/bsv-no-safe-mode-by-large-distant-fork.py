#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that large distant fork does not cause safe mode.
Scenario:
1. Generate two long branches. Second branch has invalid first block.
2. Send first branch and check that it is active.
3. Send headers only of second branch with more POW.
4. Validate that active tip is from first branch and second branch tip has status headers-only.
5. Validate that node is not in safe mode
6. Send data of second branch.
7. Validate that second branch tip is now invalid and active tip is from first branch.
8. Validate that node is not in safe mode.
"""
from time import sleep

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.mininode import msg_block, CBlock, CTxOut, msg_headers, CBlockHeader
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.cdefs import SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE, SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE
from test_framework.util import wait_until, assert_equal


class NoSafeModeByLargeDistantFork(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        MAX_FORK_DISTANCE = 10
        MIN_FORK_LENGTH = 3
        MIN_FORK_DIFFERENCE = 1

        args= [f"-safemodemaxforkdistance={MAX_FORK_DISTANCE}",
               f"-safemodeminforklength={MIN_FORK_LENGTH}",
               f"-safemodeminblockdifference={MIN_FORK_DIFFERENCE}",]

        with self.run_node_with_connections("Preparation", 0, args, 2) as (conn1, conn2):
            last_block_time = 0
            conn1.rpc.generate(1)

            branch_1_root, last_block_time = make_block(conn1, last_block_time = last_block_time)
            branch_1_blocks = [branch_1_root]
            for _ in range(MAX_FORK_DISTANCE):
                new_block, last_block_time = make_block(conn1, branch_1_blocks[-1], last_block_time = last_block_time)
                branch_1_blocks.append(new_block)

            branch_2_root, last_block_time = make_block(conn2, makeValid=False, last_block_time = last_block_time)
            branch_2_blocks = [branch_2_root]
            for _ in range(MAX_FORK_DISTANCE + MIN_FORK_DIFFERENCE + 1):
                new_block, last_block_time = make_block(conn2, branch_2_blocks[-1], last_block_time = last_block_time)
                branch_2_blocks.append(new_block)

            # send first branch that should be active tip
            send_by_headers(conn1, branch_1_blocks, do_send_blocks=True)
            wait_for_tip(conn1, branch_1_blocks[-1].hash)

            # send second branch with more POW
            send_by_headers(conn2, branch_2_blocks, do_send_blocks=False)
            wait_for_tip(conn1, branch_1_blocks[-1].hash)
            wait_for_tip_status(conn1, branch_2_blocks[-1].hash, "headers-only")

            # we should not be in safe mode (distance to the fork is too large)
            assert not conn1.rpc.getsafemodeinfo()["safemodeenabled"]

            conn1.rpc.invalidateblock(branch_1_blocks[-1].hash)
            wait_for_tip(conn1, branch_1_blocks[-2].hash)
            # here we have shortened distance from the active tip to the fork root so the safe mode should be activated
            assert conn1.rpc.getsafemodeinfo()["safemodeenabled"]

            conn1.rpc.reconsiderblock(branch_1_blocks[-1].hash)
            wait_for_tip(conn1, branch_1_blocks[-1].hash)
            # returning to the old state (distance to the fork is too large)
            assert not conn1.rpc.getsafemodeinfo()["safemodeenabled"]

            # From time to time this test can run faster than expected and
            # the older blocks for batch 2 headers are not yet requested.
            # In that case they will be rejected due to being too far away
            # form the tip. In that case we need to send them again once they
            # are requested.
            def on_getdata(conn, msg):
                for i in msg.inv:
                    if i.type != 2: # MSG_BLOCK
                        error_msg = f"Unexpected data requested {i}"
                        self.log.error(error_msg)
                        raise NotImplementedError(error_msg)
                    for block in branch_2_blocks:
                        if int(block.hash, 16) == i.hash:
                            conn.send_message(msg_block(block))
                            break

            conn2.cb.on_getdata = on_getdata

            # send sencond branch full blocks
            for block in branch_2_blocks:
                conn2.send_message(msg_block(block))

            tips = conn2.rpc.getchaintips()

            # second branch should now be invalid
            wait_for_tip_status(conn1, branch_2_blocks[-1].hash, "invalid")
            wait_for_tip(conn1, branch_1_blocks[-1].hash)

            # we should not be in safe mode
            assert not conn1.rpc.getsafemodeinfo()["safemodeenabled"]


if __name__ == '__main__':
    NoSafeModeByLargeDistantFork().main()
