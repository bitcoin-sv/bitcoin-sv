#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test entering and exiting of safe mode by large invalid branch where data of first branch block arrives last
Scenario:
1. Generate two branches. Main branch has 10 blocks, alternative branch has 30 blocks. First block of alternative
   branch is invalid.
2. Send whole main branch
3. Send only header of first block of alternative branch
4. Send alternative branch blocks 2 - 20 with data. Validate that node enters safe mode (with UNKNOWN safe mode level)
5. Send alternative branch blocks 21 - 30 but only headers. This should not change anything regarding safe mode.
6. Send alternative branch first block data. This should mark branch as invalid and change safe mode level to INVALID
7. Extend main branch by 15 blocks. This should cause that node exits safe mode because alternative branch is
   no longer SAFE_MODE_MIN_POW_DIFFERENCE blocks ahead
"""
from time import sleep

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.mininode import msg_block
from test_framework.test_framework import BitcoinTestFramework
from test_framework.cdefs import SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE


class TriggerSafeModeByIvalidChain(BitcoinTestFramework):

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

            branch_1_root, last_block_time = make_block(conn1, last_block_time=last_block_time)
            branch_1_blocks = [branch_1_root]
            for _ in range(10):
                new_block, last_block_time = make_block(conn1, branch_1_blocks[-1], last_block_time=last_block_time)
                branch_1_blocks.append(new_block)

            branch_2_root, last_block_time = make_block(conn2, makeValid=False, last_block_time=last_block_time)
            branch_2_blocks = [branch_2_root]
            for _ in range(30):
                new_block, last_block_time = make_block(conn2, branch_2_blocks[-1], last_block_time=last_block_time)
                branch_2_blocks.append(new_block)

            # send main branch that should be active tip
            send_by_headers(conn1, branch_1_blocks, do_send_blocks=True)

            # send block header of the first block of branch 2 but not send block itself
            send_by_headers(conn2, branch_2_blocks[:1], do_send_blocks=False)

            # send first half of the blocks from the second branch
            send_by_headers(conn2, branch_2_blocks[1:20], do_send_blocks=True)

            # active tip is last block from branch 1 and branch 2 has status headers-only
            wait_for_tip(conn1, branch_1_blocks[-1].hash)
            wait_for_tip_status(conn1, branch_2_blocks[19].hash, "headers-only")

            # we should entered the safe mode with UNKNOWN because we don't have data of the first block
            try:
                conn1.rpc.getbalance()
                assert False, "Should not come to here, should raise exception in line above."
            except JSONRPCException as e:
                self.log.info(e.error["message"])
                assert e.error["message"] == "Safe mode: Warning: The network does not appear to agree with the local blockchain! Still waiting for block data for more details."

            # send headers only for the rest of the second branch
            send_by_headers(conn2, branch_2_blocks[20:], do_send_blocks=False)

            # we should remain in the safe mode with UNKNOWN
            try:
                conn1.rpc.getbalance()
                assert False, "Should not come to here, should raise exception in line above."
            except JSONRPCException as e:
                self.log.info(e.error["message"])
                assert e.error["message"] == "Safe mode: Warning: The network does not appear to agree with the local blockchain! Still waiting for block data for more details."

            safe_mode_status = conn1.rpc.getsafemodeinfo()
            assert safe_mode_status["safemodeenabled"]
            conn1.rpc.ignoresafemodeforblock(safe_mode_status["forks"][0]["forkfirstblock"]["hash"])
            safe_mode_status_2 = conn1.rpc.getsafemodeinfo()
            assert not safe_mode_status_2["safemodeenabled"]
            conn1.rpc.reconsidersafemodeforblock(safe_mode_status["forks"][0]["tips"][0]["hash"])
            safe_mode_status_3 = conn1.rpc.getsafemodeinfo()
            assert safe_mode_status_3["safemodeenabled"]

            # send contents of first block of second branch
            # this block is invalid and should invalidate whole second branch
            conn2.send_message(msg_block(branch_2_blocks[0]))

            # make sure that block is processed before doing any aserts by waiting for reject
            # we cannot use sync_with_ping here because we sent invalid block and connection will be banned and closed
            conn2.cb.wait_for_reject()

            # active tip should still be from branch 1 and branch 2 should be invalid
            wait_for_tip(conn1, branch_1_blocks[-1].hash)
            wait_for_tip_status(conn1, branch_2_blocks[-1].hash, "invalid")

            # safe mode message should have now changed - we have invalid chain that triggers safe mode
            try:
                conn1.rpc.getbalance()
                assert False, "Should not come to here, should raise exception in line above."
            except JSONRPCException as e:
                self.log.info(e.error["message"])
                assert e.error["message"] == "Safe mode: Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade. A large invalid fork has been detected."

            # add more blocks to active chain so fork will no longer have more than SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE blocks
            new_block, last_block_time = make_block(conn1, branch_1_blocks[-1], last_block_time=last_block_time)
            branch_1_aditional_blocks = [new_block]
            for _ in range(20 - SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE):
                new_block, last_block_time = make_block(conn1, branch_1_aditional_blocks[-1], last_block_time=last_block_time)
                branch_1_aditional_blocks.append(new_block)

            # send additional blocks with data to active chain
            send_by_headers(conn1, branch_1_aditional_blocks, do_send_blocks=True)

            # check that active tip is from branch 1
            wait_for_tip(conn1, branch_1_aditional_blocks[-1].hash)

            # we are not in the Safe mode any more fork is no longer 6 blocks ahead of
            # active chain
            conn1.rpc.getbalance()


if __name__ == '__main__':
    TriggerSafeModeByIvalidChain().main()
