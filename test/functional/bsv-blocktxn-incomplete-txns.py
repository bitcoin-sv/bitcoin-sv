#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test deserialisation of a blocktxn P2P message with incomplete txns in the list.
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
        r += ser_uint256(self.blockhash)

        # 2 txns reported in the list
        r += bytes([0x02])
        # Only 1 complete txn
        r += bytes([0x00, 0x00, 0x00, 0x00, # Txn 1 version
                    0x00,                   # Txn 1 vin
                    0x00,                   # Txn 1 vout
                    0x00, 0x00, 0x00, 0x00, # Txn 1 locktime
                    0x00, 0x00, 0x00, 0x00  # Txn 2 Version
                                            # Rest of txn2 is missing
                    ])
        return r


class TestIncompleteTxnList(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.nodes[0].generate(1)

        self.stop_node(0)
        with self.run_node_with_connections("Bad blocktxn test", 0, [], 1) as (conn,):
            conn.send_message(msg_badblocktxn())

            wait_until(lambda: check_for_log_msg(self, "parsing error: index out of bounds", "/node0"), timeout=10)


if __name__ == '__main__':
    TestIncompleteTxnList().main()
