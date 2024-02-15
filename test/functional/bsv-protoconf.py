#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.mininode import CInv, LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH, logger, NodeConn, NodeConnCB, \
    NetworkThread, msg_protoconf, CProtoconf, msg_inv
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than,  p2p_port
from math import ceil


class BsvProtoconfTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def start_node_with_protoconf(self, maxprotocolrecvpayloadlength, recvinvqueuefactor=0):
        """
        This method starts the node and creates a connection to the node.
        It takes care of exchanging protoconf messages between node and python connection.
        Max size of bitcoin nodes' inv message is determined by maxprotocolrecvpayloadlength.
        Max size of python connections' inv message is LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH.
        If called with parameter recvinvqueuefactor it also sets this setting on the node.

        :returns test_node
        """

        # Start the node with maxprotocolrecvpayloadlength, recvinvqueuefactor (if set)
        start_params = []
        if maxprotocolrecvpayloadlength:
            start_params.append("-maxprotocolrecvpayloadlength={} ".format(maxprotocolrecvpayloadlength))
        if recvinvqueuefactor:
            start_params.append("-recvinvqueuefactor={} ".format(recvinvqueuefactor))
        self.start_node(0, start_params)

        # Create a connection and connect to the node
        test_node = NodeConnCB()
        test_node.wanted_inv_lengths = []

        def on_getdata(conn, message):
            test_node.wanted_inv_lengths.append(len(message.inv))
        test_node.on_getdata = on_getdata
        # Send protoconf message from python node to bitcoind node

        def send_protoconf_default_msg_length(conn):
            conn.send_message(msg_protoconf(CProtoconf(1, LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH)))
        test_node.send_protoconf = send_protoconf_default_msg_length
        connections = [NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node)]
        test_node.add_connection(connections[0])
        NetworkThread().start()  # Start up network handling in another thread

        # Prepare initial block. Needed so that GETDATA can be send back.
        self.nodes[0].generate(1)

        # Receive bitcoind's protoconf and save max_recv_payload_length.
        test_node.wait_for_protoconf()
        test_node.max_recv_payload_length = test_node.last_message["protoconf"].protoconf.max_recv_payload_length

        return test_node

    def run_maxprotocolrecvpayloadlength_test(self, n_of_inv_to_send, maxprotocolrecvpayloadlength=0):
        """
        This method sends INV message with n_of_inv_to_send elements to the node.
        It checks if node respects our settings for size of inv message, sends getdata for all inv elements and respects
        our limits send in protoconf message.
        n_of_inv_to_send should not be bigger than CInv.estimateMaxInvElements(maxprotocolrecvpayloadlength)
        """
        # start node, protoconf messages, ...
        test_node = self.start_node_with_protoconf(maxprotocolrecvpayloadlength)

        # Check if maxprotocolrecvpayloadlength is not set and use default value from bitcoind
        if not maxprotocolrecvpayloadlength:
            maxprotocolrecvpayloadlength = 2 * LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH

        # Assert received max payload length from bitcoind node to be the same as we set in kwargs
        assert_equal(test_node.max_recv_payload_length, maxprotocolrecvpayloadlength)
        # Calculate maximum number of elements that bitcoind node is willing to receive
        maxInvElements = CInv.estimateMaxInvElements(test_node.max_recv_payload_length)
        logger.info(
            "Received bitcoind max message size: {} B, which represents {} elements. ".format(test_node.max_recv_payload_length, maxInvElements))

        # Calculate our max size for inv message we can accept and how many element it contains.
        # Remote node has to respect our settings.
        expected_inv_len = CInv.estimateMaxInvElements(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH)
        logger.info("Our max message size: {} B, which represents {} elements. ".format(
            LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH, expected_inv_len))

        # Send inv message with specified number of elements
        logger.info("Sending inv message with: {} elements.  Max allowed : {}".format(n_of_inv_to_send, maxInvElements))
        test_node.send_message(msg_inv([CInv(CInv.TX, i) for i in range(0, n_of_inv_to_send)]))
        test_node.sync_with_ping()
        assert_equal(len(self.nodes[0].listbanned()), 0)
        test_node.wait_for_getdata()
        logger.info("Received GetData from bitcoind.")

        # We should receive GetData messages with 1MB size (29126 elements = CInv.estimateMaxInvElements(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH))
        # and last GetData message with remaining elements.
        max_elements_recieved_per_message = CInv.estimateMaxInvElements(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH)
        number_of_getdata_messages = ceil(n_of_inv_to_send / max_elements_recieved_per_message)
        # number of received messages should be the same as we calculated above
        assert_equal(len(test_node.wanted_inv_lengths), number_of_getdata_messages)
        for i in range(0, number_of_getdata_messages - 1):
            assert_equal(test_node.wanted_inv_lengths[i], expected_inv_len)
        remained_for_last_getdata = n_of_inv_to_send - (number_of_getdata_messages - 1) * expected_inv_len
        # last message should contain the exact number of inv elements left
        assert_equal(test_node.wanted_inv_lengths[number_of_getdata_messages - 1], remained_for_last_getdata)
        self.stop_node(0)
        logger.info("maxprotocolrecvpayloadlength test finished successfully\n")

    def run_ban_test(self, maxprotocolrecvpayloadlength=0):
        """
        This method tests banning our connection when sending inv message with too many elements (violates limit set by
        maxprotocolrecvpayloadlength.
        Setting maxprotocolrecvpayloadlength very high results in long execution times and high memory usage.
        Minimal value for maxprotocolrecvpayloadlength is 1MiB.

        We send 3 protoconf messages:
        -> 1.) max_elements-1   ->  not banned
        -> 2.) max_elements     ->  not banned
        -> 3.) max_elements+1   ->  banned
        """
        # start node, protoconf messages, ...
        test_node = self.start_node_with_protoconf(maxprotocolrecvpayloadlength)

        maxInvElements = CInv.estimateMaxInvElements(test_node.max_recv_payload_length)
        logger.info(
            "Received bitcoind max message size: {} B, which represents {} elements. ".format(test_node.max_recv_payload_length,
                                                                                              maxInvElements))
        ### TEST WITH maxInvElements - 1, maxInvElements and maxInvElements + 1
        # 1. Send bitcoind Inv message that is smaller than max_recv_payload_length.
        logger.info("Sending inv message with: {} elements. Max allowed : {}".format(maxInvElements-1, maxInvElements))
        test_node.send_message(msg_inv([CInv(CInv.TX, i) for i in range(0, maxInvElements - 1)]))
        test_node.sync_with_ping()
        assert_equal(len(self.nodes[0].listbanned()), 0)  # not banned

        # 2. Send bitcoind Inv message that is equal to max_recv_payload_length.
        logger.info("Sending inv message with: {} elements. Max allowed : {}".format(maxInvElements, maxInvElements))
        test_node.send_message(msg_inv([CInv(CInv.TX, maxInvElements+i) for i in range(0, maxInvElements)]))
        test_node.sync_with_ping()
        assert_equal(len(self.nodes[0].listbanned()), 0)  # not banned

        # 3. Send bitcoind Inv message that is larger than max_recv_payload_length.
        logger.info("Sending inv message with: {} elements. Max allowed : {}".format(maxInvElements + 1, maxInvElements))
        logger.info("Expecting to be banned...")
        test_node.send_message(msg_inv([CInv(CInv.TX, 2*maxInvElements+i) for i in range(0, maxInvElements + 1)]))
        test_node.wait_for_disconnect()
        assert (self.nodes[0].closed)  # disconnected
        assert_equal(len(self.nodes[0].listbanned()), 1)  # banned
        logger.info("Banned nodes : {}".format(self.nodes[0].listbanned()))
        self.nodes[0].setban("127.0.0.1", "remove")  # remove ban
        self.stop_node(0)
        logger.info("ban test finished successfully\n")

    def run_recvinvqueuefactor_test(self, maxprotocolrecvpayloadlength, recvinvqueuefactor):
        """
        This method sends 4*recvinvqueuefactor inv messages (with max elements allowed by maxprotocolrecvpayloadlength).
        Node should save all this messages and request getdata.
        After sending additional inv message node doesn't request all invs sent to it anymore.
        """
        # start node, protoconf messages, ...
        test_node = self.start_node_with_protoconf(maxprotocolrecvpayloadlength, recvinvqueuefactor)

        maxInvElements = CInv.estimateMaxInvElements(test_node.max_recv_payload_length)

        # Send enough inv messages to fill the queue in bitcoind
        for n in range(0, 4*recvinvqueuefactor):
            test_node.send_message(msg_inv([CInv(CInv.TX, n*maxInvElements+i) for i in range(0, maxInvElements)]))
            test_node.sync_with_ping()
            assert_equal(len(self.nodes[0].listbanned()), 0)
            test_node.wait_for_getdata()

        # check if we have all the inv messages
        assert_equal(sum(test_node.wanted_inv_lengths), maxInvElements*4*recvinvqueuefactor)

        # send additional inv messages
        test_node.send_message(msg_inv([CInv(CInv.TX, 4*recvinvqueuefactor * maxInvElements + i) for i in range(0, maxInvElements)]))
        test_node.sync_with_ping()
        assert_equal(len(self.nodes[0].listbanned()), 0)
        test_node.wait_for_getdata()

        # check that the number of inv messages doesn't match anymore (we annouced more invs that the node can remember and ask for)
        assert_greater_than(maxInvElements*(4*recvinvqueuefactor+1), sum(test_node.wanted_inv_lengths))
        self.stop_node(0)
        logger.info("recvinvqueuefactor test finished successfully\n")

    def run_test(self):
        ONE_MiB = 1048576
        # Send 1 MiB inv with maxprotocolrecvpayloadlength set to defaults
        self.run_maxprotocolrecvpayloadlength_test(CInv.estimateMaxInvElements(ONE_MiB))
        # Send 3 MiB inv with maxprotocolrecvpayloadlength set to 500 MiB
        self.run_maxprotocolrecvpayloadlength_test(CInv.estimateMaxInvElements(3*ONE_MiB), 500*ONE_MiB)
        # Run ban test sends 3 INVs with sizes (max-1, max, max+1). In the last attempt it should be banned
        self.run_ban_test(2*ONE_MiB)
        # Send many INV messages and check when they fill up queue in bitcoind and some of them get missing
        self.run_recvinvqueuefactor_test(ONE_MiB, 2)


if __name__ == '__main__':
    BsvProtoconfTest().main()
