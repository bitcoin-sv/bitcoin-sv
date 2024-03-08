#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import create_block, create_coinbase
from test_framework.util import *
import time
import os


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()


class msg_garbage():
    command = b"inv"

    def __init__(self):
        pass

    def serialize(self):
        r = b""
        r += ser_uint256_vector([0xff, 0xff, 0xff, 0xff])
        return r

    def __repr__(self):
        return "garbage"#(announce=%s, version=%lu)" % (self.announce, self.version)


class SpamLog(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-whitelist=127.0.0.1', '-debugexclude=net']]

    def run_test(self):
        node = self.nodes[0]
        self.test_node = TestNode()
        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.test_node))
        self.test_node.add_connection(connections[0])

        NetworkThread().start()

        self.log.info("Wait for verack")
        self.test_node.wait_for_verack()

        garbage = msg_garbage()
        self.log.info("Spamming")
        start_time = time.time()
        while (time.time() - start_time) < 5:
            self.test_node.send_message(garbage)

        # Check size of generated log file (arbitrary check it's below 100k)
        logfile = "{}/node0/regtest/bitcoind.log".format(self.options.tmpdir)
        size = os.path.getsize(logfile)
        self.log.info("Logile size = {}".format(size))
        assert(size < 1000000)


if __name__ == '__main__':
    SpamLog().main()
