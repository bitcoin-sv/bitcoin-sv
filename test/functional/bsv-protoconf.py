#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time, math
from test_framework.blocktools import create_block, create_coinbase

class BsvProtoconfTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):

        # Testing scope: our maximal protocol message length is smaller than remote node's message length, remote node has to respect this.

        ELEMENTS_PER_1MiB = 29126
        ELEMENTS_PER_2MiB = 58254

        expected_inv_len = CInv.estimateMaxInvElements(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH) #29126 elements
        assert_equal(expected_inv_len, ELEMENTS_PER_1MiB)
        logger.info("Our max message size: {} B, which represents {} elements. ".format(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH, expected_inv_len))

        test_node = NodeConnCB()
        wanted_inv_lengths = []
        def on_getdata(conn, message):
            wanted_inv_lengths.append(len(message.inv))

        test_node.on_getdata = on_getdata

        connections = []
        connections.append(
            NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node))

        test_node.add_connection(connections[0])

        NetworkThread().start()  # Start up network handling in another thread

        def send_protoconf_default_msg_length(conn):
            conn.send_message(msg_protoconf(CProtoconf(1, LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH)))
        test_node.send_protoconf = send_protoconf_default_msg_length

        # 0. Prepare initial block. Needed so that GETDATA can be send back.
        self.nodes[0].generate(1)

        # 1. Receive bitcoind's protoconf and save max_recv_payload_length.
        test_node.wait_for_protoconf()
        max_recv_payload_length = test_node.last_message["protoconf"].protoconf.max_recv_payload_length

        maxInvElements = CInv.estimateMaxInvElements(max_recv_payload_length) #58254
        assert_equal(maxInvElements, ELEMENTS_PER_2MiB)
        logger.info("Received bitcoind max message size: {} B, which represents {} elements. ".format(max_recv_payload_length, maxInvElements))

        # 2. Send bitcoind Inv message.
        test_node.send_message(msg_inv([CInv(1, i) for i in range(0, maxInvElements)]))
        test_node.sync_with_ping()
        assert_equal(len(self.nodes[0].listbanned()), 0) #not banned

        # 2.1. Receive GetData.
        test_node.wait_for_getdata()

        # 2.2. We should receive 2 GetData messages with 1MB size (29126 elements) and 1 GetData message with 2 elements.
        assert_equal(wanted_inv_lengths[0], expected_inv_len)
        assert_equal(wanted_inv_lengths[1], expected_inv_len)
        assert_equal(wanted_inv_lengths[2], 2)
        assert_equal(len(wanted_inv_lengths), 3)

        ### TEST WITH maxInvElements - 1, maxInvElements and maxInvElements + 1
        # 1. Send bitcoind Inv message that is smaller than max_recv_payload_length.
        test_node.send_message(msg_inv([CInv(1, i) for i in range(0, maxInvElements - 1)]))
        test_node.sync_with_ping()
        assert_equal(len(self.nodes[0].listbanned()), 0) #not banned

        # 2. Send bitcoind Inv message that is equal to max_recv_payload_length.
        test_node.send_message(msg_inv([CInv(1, i) for i in range(0, maxInvElements)]))
        test_node.sync_with_ping()
        assert_equal(len(self.nodes[0].listbanned()), 0) #not banned

        # 3. Send bitcoind Inv message that is larger than max_recv_payload_length.
        test_node.send_message(msg_inv([CInv(1, i) for i in range(0, maxInvElements + 1)]))
        test_node.wait_for_disconnect()
        assert(self.nodes[0].closed)# disconnected
        assert_equal(len(self.nodes[0].listbanned()), 1) #banned
        logger.info("Banned nodes : {}".format(self.nodes[0].listbanned()))

if __name__ == '__main__':
    BsvProtoconfTest().main()
