#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test P2P handling of blocktxn message with incomplete CompactSize payload.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_for_log_msg
from test_framework.mininode import ser_uint256


class msg_shortblocktxn():
    command = b"blocktxn"

    def __init__(self):
        self.blockhash = 0

    def serialize(self):
        # A good message will contain a blockhash followed by a list of txns.
        # Our bad message just contains the byte 0xff where the list of txns should be.
        r = b""
        r += ser_uint256(self.blockhash)
        r += bytes([0xff])
        return r


class TestShortBlocktxn(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.nodes[0].generate(1)

        self.stop_node(0)
        with self.run_node_with_connections("Short blocktxn test", 0, [], 1) as (conn,):
            conn.send_message(msg_shortblocktxn())
            wait_until(lambda: check_for_log_msg(self, "shorter than its ", "/node0"), timeout=10)


if __name__ == '__main__':
    TestShortBlocktxn().main()
