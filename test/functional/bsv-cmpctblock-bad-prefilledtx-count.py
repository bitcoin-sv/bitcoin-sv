#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test: cmpctblock P2P message with large prefilled tx count.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_for_log_msg


class MsgCmpctBlockBadPrefilledTxCount():
    command = b"cmpctblock"

    def serialize(self):
        r = b""
        r += bytes([0xff] * 88) # blockheader and nonce
        r += bytes([0])          # shortid count
        r += bytes([0xff, 0xff, 0xff, 0xff,
                    0xff, 0xff, 0xff, 0xff,
                    0x01]) # large prefilled tx count (<= std::vector::max_size())
        return r


class CmpctBlockBadPrefilledTxCountTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.nodes[0].generate(1)

        self.stop_node(0)
        with self.run_node_with_connections("Bad cmpctblock prefilled tx", 0, [], 1) as (conn,):
            conn.send_message(MsgCmpctBlockBadPrefilledTxCount())
            wait_until(lambda: check_for_log_msg(self, "reason: Over-long", "/node0"),
                       timeout=3)


if __name__ == '__main__':
    CmpctBlockBadPrefilledTxCountTest().main()
