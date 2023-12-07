#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework import mininode
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time
from test_framework.blocktools import create_block, create_coinbase


class BsvProtoconfViolationTest(BitcoinTestFramework):

    def add_options(self, parser):
        parser.add_option("--testbinary", dest="testbinary",
                          default=os.getenv("BITCOIND", "bitcoind"),
                          help="bitcoind binary to test")

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        test_node = mininode.NodeConnCB()

        connections = []
        connections.append(
            mininode.NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node))

        test_node.add_connection(connections[0])

        mininode.NetworkThread().start()  # Start up network handling in another thread

        # 1. Test that protoconf is sent after verack
        test_node.wait_for_verack()
        test_node.wait_for_protoconf()

        logger.info("Received time of verack: {} ".format(test_node.msg_timestamp["verack"]))
        logger.info("Received time of protoconf: {} ".format(test_node.msg_timestamp["protoconf"]))

        logger.info("Received msg_index of verack: {} ".format(test_node.msg_index["verack"]))
        logger.info("Received msg_index of protoconf: {} ".format(test_node.msg_index["protoconf"]))

        assert_greater_than(test_node.msg_index["protoconf"], test_node.msg_index["verack"])

        # 2. Test that protoconf can only be sent once (if is sent twice --> disconnection)
        assert_equal(len(self.nodes[0].listbanned()), 0)# Before, there are zero banned node

        # First protoconf was already sent from mininode.
        # Another protoconf message will cause disconnection (but not banning).
        test_node.send_message(mininode.msg_protoconf())
        test_node.wait_for_disconnect()

        assert(self.nodes[0].closed) # disconnected
        assert_equal(len(self.nodes[0].listbanned()), 0) # After, there are also zero banned node


if __name__ == '__main__':
    BsvProtoconfViolationTest().main()
