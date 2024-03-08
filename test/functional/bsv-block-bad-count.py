#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test: block P2P message with large tx count.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_for_log_msg


class MsgBlockBadCount():
    command = b"block"

    def serialize(self):
        r = b""
        r += bytes([0x2a] * 80) # block header and nonce
        r += bytes([0xff, 0xff, 0xff, 0xff,
                    0xff, 0xff, 0xff, 0xff,
                    0x01]) # large tx count (<= std::vector::max_size())
        return r


class BlockBadCountTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.nodes[0].generate(1)

        self.stop_node(0)
        with self.run_node_with_connections("Bad block list test", 0, [], 1) as (conn,):
            conn.send_message(MsgBlockBadCount())
            wait_until(lambda: check_for_log_msg(self, "reason: Over-long", "/node0"),
                       timeout=3)


if __name__ == '__main__':
    BlockBadCountTest().main()
