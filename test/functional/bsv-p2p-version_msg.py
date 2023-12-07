#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Test P2P version message error handling.
'''

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import mininode_lock, NetworkThread, NodeConn, NodeConnCB, wait_until, msg_version, ser_string
from test_framework.util import assert_equal, connect_nodes, p2p_port, check_for_log_msg

import struct


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.last_reject = None

    def on_reject(self, conn, message):
        super().on_reject(conn, message)
        self.last_reject = message


class msg_version_bad(msg_version):
    def __init__(self):
        super().__init__()
        self.nExtraEntropy = 0x00000000111111FE

    def serialize(self):
        r = b"".join((
            struct.pack("<i", self.nVersion),
            struct.pack("<Q", self.nServices),
            struct.pack("<q", self.nTime),
            self.addrTo.serialize(),
            self.addrFrom.serialize(),
            struct.pack("<Q", self.nNonce),
            ser_string(self.strSubVer),
            struct.pack("<i", self.nStartingHeight),
            struct.pack("<b", self.nRelay),
            struct.pack("<Q", self.nExtraEntropy)
        ))
        return r


class P2PVersion(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        # Don't connect the nodes to each other initially
        self.setup_nodes()

    def run_test(self):
        # Create all the connections we will need to node0 at the start because they all need to be
        # setup before we call NetworkThread().start()

        # Create a P2P connection just so that the test framework is happy we're connected
        dummyCB = NodeConnCB()
        dummyConn = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], dummyCB, nullAssocID=True)
        dummyCB.add_connection(dummyConn)

        # By setting the assocID on this second NodeConn we prevent it sending a version message
        badConnCB = TestNode()
        badConn = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], badConnCB, assocID=0x01)
        badConnCB.add_connection(badConn)

        # Start up network handling in another thread. This needs to be called
        # after the P2P connections have been created.
        NetworkThread().start()

        # Check initial state
        dummyCB.wait_for_protoconf()
        with mininode_lock: assert_equal(len(badConnCB.message_count), 0)

        # Send a badly formatted version message
        badConn.send_message(msg_version_bad())
        # Connection will be closed with a reject
        wait_until(lambda: badConnCB.last_reject is not None, lock=mininode_lock, timeout=5)
        wait_until(lambda: badConn.state == "closed", lock=mininode_lock, timeout=5)

        # Check clear log message was generated
        assert check_for_log_msg(self, "Failed to process version: (Badly formatted association ID", "/node0")


if __name__ == '__main__':
    P2PVersion().main()
