#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test P2P handling of empty command in message.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_for_log_msg

import struct


class msg_emptycmdmsg():
    command = b'\x00'

    def __init__(self):
        self.payload = 1

    def serialize(self):
        return struct.pack("<I", self.payload)


class TestEmptyCmd(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.nodes[0].generate(1)

        self.stop_node(0)
        with self.run_node_with_connections("Empty P2P command test", 0, [], 1) as (conn,):
            conn.send_message(msg_emptycmdmsg())
            wait_until(lambda: check_for_log_msg(self, "Unknown command \"\" from peer=0", "/node0"), timeout=10)


if __name__ == '__main__':
    TestEmptyCmd().main()
