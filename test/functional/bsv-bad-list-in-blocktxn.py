#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test deserialization of a blocktxn P2P message with no items in the list.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_for_log_msg
from test_framework.mininode import ser_uint256


class msg_badblocktxn():
    command = b"blocktxn"

    def __init__(self):
        self.blockhash = 0

    def serialize(self):
        r = b""
        # Good block hash
        r += ser_uint256(self.blockhash)
        # Txn list length 0x3a, then just 4 0x00 bytes
        r += bytes([0x3a, 0x00, 0x00, 0x00, 0x00])
        return r


class TestBadTxnList(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.nodes[0].generate(1)

        self.stop_node(0)
        with self.run_node_with_connections("Bad blocktxn list test", 0, [], 1) as (conn,):
            conn.send_message(msg_badblocktxn())

            wait_until(lambda: check_for_log_msg(self, "read(): end of data: iostream error", "/node0"), timeout=60)


if __name__ == '__main__':
    TestBadTxnList().main()
