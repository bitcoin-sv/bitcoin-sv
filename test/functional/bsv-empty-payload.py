#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test P2P handling of message with empty payload.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_for_log_msg

import time


class msg_emptypayload():
    command = b"reject"

    def __init__(self):
        pass

    def serialize(self):
        return b""


class TestEmptyPayload(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.nodes[0].generate(1)

        self.stop_node(0)
        with self.run_node_with_connections("Empty P2P payload test", 0, [], 1) as (conn,):
            conn.send_message(msg_emptypayload())
            wait_until(lambda: check_for_log_msg(self, "Unparseable reject message received", "/node0"), timeout=10)
            time.sleep(2)


if __name__ == '__main__':
    TestEmptyPayload().main()
