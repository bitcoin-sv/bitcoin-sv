#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that sending blocks after already sending headers does not trigger exiting safe mode.
Test that sending another branch after already entering safe mode because of other branch does not trigger exiting safe mode.
Test with different ordering and with and without waiting between sending branches.
# In third case, safe mode should be activated twice:
# 1. In initialization phase: node has height 1, and it receives headers for 10 blocks --> safe mode is activated.
# 2. Waiting makes sure that first 10 blocks are received --> safe mode is disabled.
# 3. The node receives 20 new headers --> safe mode is activated again.
In cases 1 and 2, where there is no waiting between sending, there is no guarantee safe mode is disabled in between.
This also does not apply for case 4, where longer branch (20 headers) is sent first.
"""
from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.mininode import msg_block
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until
import glob
import shutil
import os


class StayInSafeMode(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def send_branches(self, first_branch, second_branch, wait):

        send_by_headers(first_branch['conn'], first_branch['blocks'], first_branch['do_send_blocks'])

        if wait:
            first_branch['conn'].cb.sync_with_ping()

        send_by_headers(second_branch['conn'], second_branch['blocks'], second_branch['do_send_blocks'])

    def run_test_case(self, description, order=1, wait=False, numberOfSafeModeLevelChanges=1):

        self.log.info("Running test case: %s", description)

        # Remove test folder to start building chain from the beginning for each case
        if os.path.exists(os.path.join(self.nodes[0].datadir, "regtest")):
            shutil.rmtree(os.path.join(self.nodes[0].datadir, "regtest"))

        with self.run_node_with_connections(description, 0, None, 3) as (conn1, conn2, conn3):
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

            if order == 1:
                self.send_branches({'conn': conn1, 'blocks': branch_1_blocks, 'do_send_blocks': True}, {'conn': conn2, 'blocks': branch_2_blocks, 'do_send_blocks': False}, wait)
            else:
                self.send_branches({'conn': conn2, 'blocks': branch_2_blocks, 'do_send_blocks': False}, {'conn': conn1, 'blocks': branch_1_blocks, 'do_send_blocks': True}, wait)

            # active tip is last block from branch 1 and branch 2 has status headers-only
            wait_for_tip(conn1, branch_1_blocks[-1].hash)
            wait_for_tip_status(conn2, branch_2_blocks[-1].hash, "headers-only")

            assert conn1.rpc.getsafemodeinfo()["safemodeenabled"], "We should be in the safe mode"

            def wait_for_log():
                safeModeChanges = 0
                line_text = "WARNING: Safe mode level changed"
                for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
                    if line_text in line:
                        self.log.info("Found line: %s", line)
                        safeModeChanges += 1
                        if safeModeChanges == numberOfSafeModeLevelChanges:
                            return True
                return False
            wait_until(wait_for_log)

            conn2.send_message(msg_block(branch_2_blocks[0]))
            conn2.cb.sync_with_ping()

            # send block from the third branch
            conn3.send_message(msg_block(branch_3_root))
            conn3.cb.sync_with_ping()

            # we should still be in safe mode
            assert conn1.rpc.getsafemodeinfo()["safemodeenabled"], "We should be in the safe mode"

    def run_test(self):
        self.run_test_case("Send active chain first and then headers from second branch. Do not wait between sending.", order=1, wait=False)
        self.run_test_case("Send headers from second branch first and then active chain. Do not wait between sending.", order=-1, wait=False)
        self.run_test_case("Send active chain first and then headers from second branch. Wait between sending.", order=1, wait=True, numberOfSafeModeLevelChanges=2)
        self.run_test_case("Send headers from second branch first and then active chain. Wait between sending.", order=-1, wait=True)


if __name__ == '__main__':
    StayInSafeMode().main()
