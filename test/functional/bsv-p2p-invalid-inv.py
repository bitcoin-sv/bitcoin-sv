#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Test disconnection from invalid inventory type.
'''

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import CInv, msg_inv
from test_framework.util import wait_until, check_for_log_msg

import time


class InvalidInv(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        with self.run_node_with_connections("Invalid inv type", 0, [], number_of_connections=1) as (conn,):

            # Send valid Inv types and check we don't disconnect
            conn.send_message(msg_inv([CInv(CInv.BLOCK, 0)]))
            wait_until(lambda: check_for_log_msg(self, "got block inv", "/node0"))
            conn.send_message(msg_inv([CInv(CInv.TX, 1)]))
            wait_until(lambda: check_for_log_msg(self, "got txn inv", "/node0"))

            time.sleep(2)
            assert(conn.cb.connected)

            # Send invalid Inv type
            conn.send_message(msg_inv([CInv(CInv.ERROR, 2)]))
            wait_until(lambda: check_for_log_msg(self, "Got invalid inv", "/node0"))

            # We will be disconnected
            conn.cb.wait_for_disconnect()


if __name__ == '__main__':
    InvalidInv().main()
