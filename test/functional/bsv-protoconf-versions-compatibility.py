#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework import mininode
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time
import struct
import contextlib
from test_framework.blocktools import create_block, create_coinbase


# New class that represents an invalid Protoconf msg with 0 fields.
class CProtoconfWithZeroFields():

    def __init__(self):
        self.number_of_fields = 0

    def deserialize(self, f):
        self.number_of_fields = struct.unpack("<i", f.read(4))[0]

    def serialize(self):
        r = b""
        r += struct.pack("<i", self.number_of_fields)
        return r

    def __repr__(self):
        return "CProtoconf(number_of_fields=%064x)" \
            % (self.number_of_fields)


# New class that represents Protoconf upgraded with a new field,
# not implemented by current version of bitcoind
class CProtoconfWithNewField(mininode.CProtoconf):

    def __init__(self, number_of_fields=2, max_recv_payload_length=0, new_property=0):
        super().__init__(number_of_fields, max_recv_payload_length)
        self.new_property = new_property

    def deserialize(self, f):
        super().deserialize(f)
        self.new_property = struct.unpack("<i", f.read(4))[0]

    def serialize(self):
        r = super().serialize()
        r += struct.pack("<i", self.new_property)
        return r

    def __repr__(self):
        return "CProtoconfWithNewField(number_of_fields=%064x max_recv_payload_length=%064x new_property=%064x)" \
            % (self.number_of_fields, self.max_recv_payload_length, self.new_property)


# msg that represents largest protoconf message, whereas size is configurable
class msg_protoconf_largest():
    command = b"protoconf"

    def __init__(self, size=mininode.LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH):
        self.data = b"a" * size

    def deserialize(self, f):
        self.data = mininode.deser_string(f)

    def serialize(self):
        return self.data

    def __repr__(self):
        return "msg_protoconf(data=%s)" % (repr(self.data))


class BsvProtoconfVersionsCompatibility(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):

        @contextlib.contextmanager
        def run_connection(test_node, title):
            logger.debug("setup %s", title)
            connections = []
            connections.append(
                mininode.NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node))

            test_node.add_connection(connections[0])
            thr = mininode.NetworkThread()
            thr.start()  # Start up network handling in another thread

            logger.debug("before %s", title)
            yield
            logger.debug("after %s", title)

            connections[0].close()
            del connections
            thr.join()

            logger.debug("finished %s", title)

        ELEMENTS_PER_1MiB = 29126
        ELEMENTS_PER_2MiB = 58254

        # 1. test
        # Send protoconf with 0 fields. Bitcoind should disconnect the node, since minimum number of fields is 1
        test_node = mininode.NodeConnCB()

        def send_protoconf(conn):
            conn.send_message(mininode.msg_protoconf(CProtoconfWithZeroFields()))
        test_node.send_protoconf = send_protoconf

        with run_connection(test_node, "0 fields"):
            test_node.wait_for_verack()
            test_node.wait_for_disconnect()
            assert(self.nodes[0].closed)
            assert_equal(len(self.nodes[0].listbanned()), 0)

        # 2. test
        # Send protoconf with 1B of max_recv_payload_length. Node should be disconnected, since minimum message size is 1MiB
        test_node = mininode.NodeConnCB()

        def send_protoconf_1B(conn):
            conn.send_message(mininode.msg_protoconf(mininode.CProtoconf(1, 1)))
        test_node.send_protoconf = send_protoconf_1B

        with run_connection(test_node, "too small protoconf"):
            test_node.wait_for_verack()
            test_node.wait_for_disconnect()
            assert(self.nodes[0].closed)
            assert_equal(len(self.nodes[0].listbanned()), 0)

        # 3. test
        # Send protoconf with numberOfFields=2. max_recv_payload_length should be parsed correctly.
        test_node = mininode.NodeConnCB()

        def send_protoconf_2Fields(conn):
            conn.send_message(mininode.msg_protoconf(CProtoconfWithNewField(2, MESSAGE_LENGTH_1MiB_PLUS_1_ELEMENT, 5)))
        test_node.send_protoconf = send_protoconf_2Fields

        wanted_inv_lengths = []

        def on_getdata(conn, message):
            wanted_inv_lengths.append(len(message.inv))
        test_node.on_getdata = on_getdata

        # Set MESSAGE_LENGTH_1MiB_PLUS_1_ELEMENT to one that is slightly larger than 1MiB
        # 1MiB -- 29126 elements --> work with 29127 elements
        # In that way it is sure that bitcoind does not just take default (1MiB value).
        MESSAGE_LENGTH_1MiB_PLUS_1_ELEMENT = 1 * 1024 * 1024 + 4 + 32
        with run_connection(test_node, "2 fields"):
            expected_inv_len = mininode.CInv.estimateMaxInvElements(MESSAGE_LENGTH_1MiB_PLUS_1_ELEMENT) #29127 elements
            assert_equal(expected_inv_len, ELEMENTS_PER_1MiB + 1)
            logger.info("Our max message size: {} B, which represents {} elements. ".format(MESSAGE_LENGTH_1MiB_PLUS_1_ELEMENT, expected_inv_len))

            # 3.0. Prepare initial block. Needed so that GETDATA can be send back.
            self.nodes[0].generate(1)

            # 3.1. Receive bitcoind's protoconf (currently 2MiB) and send it Inv message
            test_node.wait_for_protoconf()
            max_recv_payload_length = test_node.last_message["protoconf"].protoconf.max_recv_payload_length
            assert_equal(max_recv_payload_length, mininode.MAX_PROTOCOL_RECV_PAYLOAD_LENGTH)
            maxInvElements = mininode.CInv.estimateMaxInvElements(max_recv_payload_length)
            logger.info("Received bitcoind max message size: {} B, which represents {} elements. ".format(max_recv_payload_length, maxInvElements))

            # 3.2. Send bitcoind Inv message (should be 2MiB)
            test_node.send_message(mininode.msg_inv([mininode.CInv(mininode.CInv.TX, i) for i in range(0, maxInvElements)]))

            # 3.3. Receive GetData.
            test_node.wait_for_getdata()
            test_node.sync_with_ping()

            # 3.4. We should receive 2 GetData messages with (1 * 1024 * 1024 + 4 + 32)B size (29127 elements).
            assert_equal(wanted_inv_lengths[0], expected_inv_len)
            assert_equal(wanted_inv_lengths[1], expected_inv_len)
            assert_equal(len(wanted_inv_lengths), 2)

        ########
        # 4.test
        # Send protoconf that is LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH of size
        test_node = mininode.NodeConnCB()

        def send_largest_protoconf(conn):
            # send protoconf of size LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH
            conn.send_message(msg_protoconf_largest(mininode.LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH))
        test_node.send_protoconf = send_largest_protoconf

        with run_connection(test_node, "largest protoconf"):
            test_node.wait_for_verack()
            test_node.sync_with_ping()
            assert_equal(len(self.nodes[0].listbanned()), 0)

        # 5.test
        # Send protoconf that is larger that LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH
        test_node = mininode.NodeConnCB()

        def send_oversized_protoconf(conn):
            # send protoconf of size LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH + 1
            conn.send_message(msg_protoconf_largest(mininode.LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH+1))
        test_node.send_protoconf = send_oversized_protoconf

        with run_connection(test_node, "oversized protoconf"):
            test_node.wait_for_verack()
            test_node.wait_for_disconnect()
            assert(self.nodes[0].closed)
            assert_equal(len(self.nodes[0].listbanned()), 1)


if __name__ == '__main__':
    BsvProtoconfVersionsCompatibility().main()
