#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that sending blocks after already sending headers does not trigger exiting safe mode.
Test that sending another branch after already entering safe mode because of other branch does not trigger exiting safe mode.
"""
from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.mininode import msg_block, CBlock, CTxOut, msg_headers, CBlockHeader
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_equal
from test_framework.cdefs import SAFE_MODE_MAX_FORK_DISTANCE, SAFE_MODE_MIN_POW_DIFFERENCE

class StayInSafeMode(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        with self.run_node_with_connections("Preparation", 0, None, 3) as (conn1, conn2, conn3):
            last_block_time = 0
            conn1.rpc.generate(1)

            branch_1_root, last_block_time = make_block(conn1, last_block_time=last_block_time)
            branch_1_blocks = [branch_1_root]
            for _ in range(10):
                new_block, last_block_time = make_block(conn1, branch_1_blocks[-1], last_block_time=last_block_time)
                branch_1_blocks.append(new_block)

            branch_2_root, last_block_time = make_block(conn2, last_block_time=last_block_time)
            branch_2_blocks = [branch_2_root]
            for _ in range(20):
                new_block, last_block_time = make_block(conn2, branch_2_blocks[-1], last_block_time=last_block_time)
                branch_2_blocks.append(new_block)

            branch_3_root, last_block_time = make_block(conn3, last_block_time=last_block_time)

            # send main branch that should be active tip
            send_by_headers(conn1, branch_1_blocks, do_send_blocks=True)

            # send headers from the second branch
            send_by_headers(conn2, branch_2_blocks, do_send_blocks=False)

            # active tip is last block from branch 1 and branch 2 has status headers-only
            wait_for_tip(conn1, branch_1_blocks[-1].hash)
            wait_for_tip_status(conn2, branch_2_blocks[-1].hash, "headers-only")

            # we should have entered the safe mode
            try:
                conn1.rpc.getbalance()
                assert False, "Should not come to here, should raise exception in line above."
            except JSONRPCException as e:
                assert e.error["message"] == "Safe mode: Warning: The network does not appear to fully agree! We received headers of a large fork. Still waiting for block data for more details."

            conn2.send_message(msg_block(branch_2_blocks[0]))
            conn2.cb.sync_with_ping()

            # send block from the third branch
            conn3.send_message(msg_block(branch_3_root))
            conn3.cb.sync_with_ping()

            # we should still be in safe mode
            try:
                conn1.rpc.getbalance()
                assert False, "Should not come to here, should raise exception in line above."
            except JSONRPCException as e:
                assert e.error["message"] == "Safe mode: Warning: The network does not appear to fully agree! We received headers of a large fork. Still waiting for block data for more details."

            
if __name__ == '__main__':
    StayInSafeMode().main()