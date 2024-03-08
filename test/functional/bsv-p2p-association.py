#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Test association and stream handling within P2P.
'''

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import (create_association_id, msg_createstream, mininode_lock,
                                     NetworkThread, NodeConn, NodeConnCB, wait_until)
from test_framework.util import assert_equal, connect_nodes, p2p_port
from test_framework.streams import StreamType
import time


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.recvAssocID = None
        self.recvStreamPolicies = None
        self.blocksSeen = {}
        self.last_reject = None
        self.last_streamack = None

    def on_version(self, conn, message):
        super().on_version(conn, message)
        self.recvAssocID = message.assocID

    def on_protoconf(self, conn, message):
        super().on_protoconf(conn, message)
        self.recvStreamPolicies = message.protoconf.stream_policies

    def on_block(self, conn, message):
        super().on_block(conn, message)
        message.block.rehash()
        self.blocksSeen[message.block.hash] = message.block

    def on_streamack(self, conn, message):
        super().on_streamack(conn, message)
        self.last_streamack = message

    def on_reject(self, conn, message):
        super().on_reject(conn, message)
        self.last_reject = message

    def seen_block(self, blockhash):
        return blockhash in self.blocksSeen


class P2PAssociation(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-whitelist=127.0.0.1'],
                           ['-whitelist=127.0.0.1'],
                           ['-whitelist=127.0.0.1', '-multistreams=0']]

    def setup_network(self):
        # Don't connect the nodes to each other initially
        self.setup_nodes()

    # Check the node has its peers setup correctly by parsing the results from getpeerinfo
    def check_peer_info(self, node, expecteds):
        peerinfos = node.getpeerinfo()
        if len(peerinfos) != len(expecteds):
            return False

        for i in range(len(peerinfos)):
            peerinfo = peerinfos[i]
            expected = expecteds[i]
            expectedPeerID = expected['id']
            expectedAssocID = expected['associd']
            expectedStreamPolicy = expected['streampolicy']

            peerinfoID = peerinfo['id']
            if peerinfoID != expectedPeerID:
                return False

            if expectedAssocID == '<UNKNOWN>':
                # Expecting a defined but unknown to us ID
                if peerinfo['associd'] == 'Not-Set':
                    return False
            else:
                if peerinfo['associd'] != expectedAssocID:
                    return False

            if peerinfo['streampolicy'] != expectedStreamPolicy:
                return False

            streams = peerinfo['streams']
            if len(streams) != len(expected['streams']):
                return False

            for j in range(len(streams)):
                stream = streams[j]
                expstream = expected['streams'][j]
                if stream['streamtype'] != expstream:
                    return False

        return True

    def run_test(self):
        # Create all the connections we will need to node0 at the start because they all need to be
        # setup before we call NetworkThread().start()

        # Create a P2P connection with no association ID (old style)
        oldStyleConnCB = TestNode()
        oldStyleConn = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], oldStyleConnCB, nullAssocID=True)
        oldStyleConnCB.add_connection(oldStyleConn)

        # Create a P2P connection with a new association ID
        newStyleConnCB = TestNode()
        newStyleConn = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleConnCB)
        newStyleConnCB.add_connection(newStyleConn)

        # Create a P2P connection with a new association ID and another connection that uses the same ID
        newStyleFirstConnCB = TestNode()
        newStyleFirstConn = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleFirstConnCB)
        newStyleFirstConnCB.add_connection(newStyleFirstConn)
        # By setting the assocID on this second NodeConn we prevent it sending a version message
        newStyleSecondConnCB = TestNode()
        newStyleSecondConn = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleSecondConnCB, assocID=newStyleFirstConn.assocID)
        newStyleSecondConnCB.add_connection(newStyleSecondConn)

        # Some connections we will use to test setup of DATA2, DATA3, DATA4 streams
        newStyleSecondConnCB_Data2 = TestNode()
        newStyleSecondConn_Data2 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleSecondConnCB_Data2, assocID=newStyleFirstConn.assocID)
        newStyleSecondConnCB_Data2.add_connection(newStyleSecondConn_Data2)
        newStyleSecondConnCB_Data3 = TestNode()
        newStyleSecondConn_Data3 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleSecondConnCB_Data3, assocID=newStyleFirstConn.assocID)
        newStyleSecondConnCB_Data3.add_connection(newStyleSecondConn_Data3)
        newStyleSecondConnCB_Data4 = TestNode()
        newStyleSecondConn_Data4 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleSecondConnCB_Data4, assocID=newStyleFirstConn.assocID)
        newStyleSecondConnCB_Data4.add_connection(newStyleSecondConn_Data4)

        # Some connections we will use to test error scenarios
        newStyleThirdConnCB = TestNode()
        badStreamConn1 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleThirdConnCB, assocID=create_association_id())
        newStyleThirdConnCB.add_connection(badStreamConn1)
        newStyleFourthConnCB = TestNode()
        badStreamConn2 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleFourthConnCB, assocID=newStyleFirstConn.assocID)
        newStyleFourthConnCB.add_connection(badStreamConn2)
        newStyleFifthConnCB = TestNode()
        badStreamConn3 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleFifthConnCB, assocID=newStyleFirstConn.assocID)
        newStyleFifthConnCB.add_connection(badStreamConn3)
        newStyleSixthConnCB = TestNode()
        badStreamConn4 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleSixthConnCB, assocID=newStyleFirstConn.assocID)
        newStyleSixthConnCB.add_connection(badStreamConn4)
        newStyleSeventhConnCB = TestNode()
        badStreamConn5 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], newStyleSeventhConnCB, assocID=newStyleFirstConn.assocID)
        newStyleSeventhConnCB.add_connection(badStreamConn5)

        # Start up network handling in another thread. This needs to be called
        # after the P2P connections have been created.
        NetworkThread().start()

        # Wait for all connections to come up to the required initial state
        oldStyleConnCB.wait_for_protoconf()
        newStyleConnCB.wait_for_protoconf()
        newStyleFirstConnCB.wait_for_protoconf()

        # Check initial state
        with mininode_lock: assert_equal(oldStyleConnCB.recvAssocID, None)
        with mininode_lock: assert_equal(oldStyleConnCB.recvStreamPolicies, b'BlockPriority,Default')
        with mininode_lock: assert_equal(newStyleConnCB.recvAssocID, newStyleConn.assocID)
        with mininode_lock: assert_equal(newStyleConnCB.recvStreamPolicies, b'BlockPriority,Default')
        with mininode_lock: assert_equal(newStyleFirstConnCB.recvAssocID, newStyleFirstConn.assocID)
        with mininode_lock: assert_equal(newStyleFirstConnCB.recvStreamPolicies, b'BlockPriority,Default')
        with mininode_lock: assert_equal(len(newStyleSecondConnCB.message_count), 0)
        with mininode_lock: assert_equal(len(newStyleSecondConnCB_Data2.message_count), 0)
        with mininode_lock: assert_equal(len(newStyleSecondConnCB_Data3.message_count), 0)
        with mininode_lock: assert_equal(len(newStyleSecondConnCB_Data4.message_count), 0)
        expected = [
            {
                'id'           : 0,                                 # oldStyleConn
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 1,                                 # newStyleConn
                'associd'      : str(newStyleConn.assocID),
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 2,                                 # newStyleFirstConn
                'associd'      : str(newStyleFirstConn.assocID),
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 3,                                 # newStyleSecondConn
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 4,                                 # newStyleSecondConn_Data2
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 5,                                 # newStyleSecondConn_Data3
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 6,                                 # newStyleSecondConn_Data4
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 7,                                 # badStreamConn1
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 8,                                 # badStreamConn2
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 9,                                 # badStreamConn3
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 10,                                # badStreamConn4
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 11,                                # badStreamConn5
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[0], expected), timeout=5)

        # Check a new block is recieved by all connections
        self.nodes[0].generate(1)
        tip = self.nodes[0].getbestblockhash()
        wait_until(lambda: oldStyleConnCB.seen_block(tip), lock=mininode_lock, timeout=5)
        wait_until(lambda: newStyleConnCB.seen_block(tip), lock=mininode_lock, timeout=5)
        wait_until(lambda: newStyleFirstConnCB.seen_block(tip), lock=mininode_lock, timeout=5)
        with mininode_lock: assert(not newStyleSecondConnCB.seen_block(tip))
        with mininode_lock: assert(not newStyleSecondConnCB_Data2.seen_block(tip))
        with mininode_lock: assert(not newStyleSecondConnCB_Data3.seen_block(tip))
        with mininode_lock: assert(not newStyleSecondConnCB_Data4.seen_block(tip))

        # Send create new stream message
        newStyleSecondConn.send_message(msg_createstream(stream_type=StreamType.DATA1.value, stream_policy=b"BlockPriority", assocID=newStyleFirstConn.assocID))
        expected = [
            {
                'id'           : 0,                                 # oldStyleConn
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 1,
                'associd'      : str(newStyleConn.assocID),         # newStyleConn
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 2,
                'associd'      : str(newStyleFirstConn.assocID),    # newStyleFirstConn & newStyleSecondConn
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1']
            },
            {
                'id'           : 4,                                 # newStyleSecondConn_Data2
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 5,                                 # newStyleSecondConn_Data3
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 6,                                 # newStyleSecondConn_Data4
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 7,                                 # badStreamConn1
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 8,                                 # badStreamConn2
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 9,                                 # badStreamConn3
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 10,                                # badStreamConn4
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 11,                                # badStreamConn5
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[0], expected), timeout=5)
        with mininode_lock: assert(newStyleSecondConnCB.last_streamack is not None)

        # Send create stream with wrong association ID
        badStreamConn1.send_message(msg_createstream(stream_type=StreamType.DATA2.value, assocID=badStreamConn1.assocID))
        # Should receive reject, no streamack
        wait_until(lambda: newStyleThirdConnCB.last_reject is not None, lock=mininode_lock, timeout=5)
        with mininode_lock: assert(newStyleThirdConnCB.last_streamack is None)
        assert("No node found with association ID" in str(newStyleThirdConnCB.last_reject.reason))
        # Connection will be closed
        wait_until(lambda: badStreamConn1.state == "closed", lock=mininode_lock, timeout=5)

        # Send create stream with missing association ID
        badStreamConn5.send_message(msg_createstream(stream_type=StreamType.DATA2.value, assocID=""))
        # Should receive reject, no streamack
        wait_until(lambda: newStyleSeventhConnCB.last_reject is not None, lock=mininode_lock, timeout=5)
        with mininode_lock: assert(newStyleSeventhConnCB.last_streamack is None)
        assert("Badly formatted message" in str(newStyleSeventhConnCB.last_reject.reason))
        # Connection will be closed
        wait_until(lambda: badStreamConn5.state == "closed", lock=mininode_lock, timeout=5)

        # Send create stream for unknown stream type
        badStreamConn2.send_message(msg_createstream(stream_type=9, assocID=badStreamConn2.assocID))
        # Should receive reject, no streamack
        wait_until(lambda: newStyleFourthConnCB.last_reject is not None, lock=mininode_lock, timeout=5)
        with mininode_lock: assert(newStyleFourthConnCB.last_streamack is None)
        assert("StreamType out of range" in str(newStyleFourthConnCB.last_reject.reason))
        # Connection will be closed
        wait_until(lambda: badStreamConn2.state == "closed", lock=mininode_lock, timeout=5)

        # Send create stream for existing stream type
        badStreamConn3.send_message(msg_createstream(stream_type=StreamType.GENERAL.value, assocID=badStreamConn3.assocID))
        # Should receive reject, no streamack
        wait_until(lambda: newStyleFifthConnCB.last_reject is not None, lock=mininode_lock, timeout=5)
        with mininode_lock: assert(newStyleFifthConnCB.last_streamack is None)
        assert("Attempt to overwrite existing stream" in str(newStyleFifthConnCB.last_reject.reason))
        # Connection will be closed
        wait_until(lambda: badStreamConn3.state == "closed", lock=mininode_lock, timeout=5)

        # Send create stream with unknown stream policy specified
        badStreamConn4.send_message(msg_createstream(stream_type=StreamType.GENERAL.value, stream_policy=b"UnknownPolicy", assocID=badStreamConn3.assocID))
        # Should receive reject, no streamack
        wait_until(lambda: newStyleSixthConnCB.last_reject is not None, lock=mininode_lock, timeout=5)
        with mininode_lock: assert(newStyleSixthConnCB.last_streamack is None)
        assert("Unknown stream policy name" in str(newStyleSixthConnCB.last_reject.reason))
        # Connection will be closed
        wait_until(lambda: badStreamConn4.state == "closed", lock=mininode_lock, timeout=5)

        # Check streams are in the expected state after all those errors
        expected = [
            {
                'id'           : 0,                                 # oldStyleConn
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 1,                                 # newStyleConn
                'associd'      : str(newStyleConn.assocID),
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 2,                                 # newStyleFirstConn & newStyleSecondConn
                'associd'      : str(newStyleFirstConn.assocID),
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1']
            },
            {
                'id'           : 4,                                 # newStyleSecondConn_Data2
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 5,                                 # newStyleSecondConn_Data3
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 6,                                 # newStyleSecondConn_Data4
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[0], expected), timeout=5)

        # See if we can establish all the possible stream types
        newStyleSecondConn_Data2.send_message(msg_createstream(stream_type=StreamType.DATA2.value, assocID=newStyleFirstConn.assocID))
        newStyleSecondConn_Data3.send_message(msg_createstream(stream_type=StreamType.DATA3.value, assocID=newStyleFirstConn.assocID))
        newStyleSecondConn_Data4.send_message(msg_createstream(stream_type=StreamType.DATA4.value, assocID=newStyleFirstConn.assocID))
        expected = [
            {
                'id'           : 0,                                 # oldStyleConn
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 1,
                'associd'      : str(newStyleConn.assocID),         # newStyleConn
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 2,                                 # newStyleFirstConn, newStyleSecondConn, newStyleSecondConn_Data2,
                'associd'      : str(newStyleFirstConn.assocID),    # newStyleSecondConn_Data3, newStyleSecondConn_Data4
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1', 'DATA2', 'DATA3', 'DATA4']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[0], expected), timeout=5)

        # Connect 2 nodes and check they establish the expected streams
        connect_nodes(self.nodes, 0, 1)
        expected0 = [
            {
                'id'           : 0,                                 # oldStyleConn
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 1,                                 # newStyleConn
                'associd'      : str(newStyleConn.assocID),
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 2,                                 # newStyleFirstConn, newStyleSecondConn, newStyleSecondConn_Data2,
                'associd'      : str(newStyleFirstConn.assocID),    # newStyleSecondConn_Data3, newStyleSecondConn_Data4
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1', 'DATA2', 'DATA3', 'DATA4']
            },
            {
                'id'           : 12,                                # A new association established to node1
                'associd'      : '<UNKNOWN>',
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[0], expected0), timeout=5)
        expected1 = [
            {
                'id'           : 0,                                 # An association to node0
                'associd'      : '<UNKNOWN>',
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[1], expected1), timeout=5)

        # Connect 2 nodes, one of which has streams disabled, and check they establish the expected streams
        connect_nodes(self.nodes, 0, 2)
        expected0 = [
            {
                'id'           : 0,                                 # oldStyleConn
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 1,                                 # newStyleConn
                'associd'      : str(newStyleConn.assocID),
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
            {
                'id'           : 2,                                 # newStyleFirstConn, newStyleSecondConn, newStyleSecondConn_Data2,
                'associd'      : str(newStyleFirstConn.assocID),    # newStyleSecondConn_Data3, newStyleSecondConn_Data4
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1', 'DATA2', 'DATA3', 'DATA4']
            },
            {
                'id'           : 12,                                # Association to node 1
                'associd'      : '<UNKNOWN>',
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1']
            },
            {
                'id'           : 14,                                # Old style association to node 2
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[0], expected0), timeout=5)
        expected2 = [
            {
                'id'           : 0,                                 # An association to node0
                'associd'      : 'Not-Set',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[2], expected2), timeout=5)

        # Make sure everyone sees all blocks over whatever stream
        self.nodes[0].generate(1)
        tip = self.nodes[0].getbestblockhash()
        wait_until(lambda: self.nodes[1].getbestblockhash() == tip, timeout=5)
        wait_until(lambda: self.nodes[2].getbestblockhash() == tip, timeout=5)

        self.nodes[1].generate(1)
        tip = self.nodes[1].getbestblockhash()
        wait_until(lambda: self.nodes[0].getbestblockhash() == tip, timeout=5)
        wait_until(lambda: self.nodes[2].getbestblockhash() == tip, timeout=5)

        self.nodes[2].generate(1)
        tip = self.nodes[2].getbestblockhash()
        wait_until(lambda: self.nodes[0].getbestblockhash() == tip, timeout=5)
        wait_until(lambda: self.nodes[1].getbestblockhash() == tip, timeout=5)

        # Add another node, configured to only support the Default stream policy
        self.add_node(3,
                      extra_args = ['-whitelist=127.0.0.1', '-multistreampolicies=Default'],
                      init_data_dir=True)
        self.start_node(3)

        # Check streampolicies field from getnetworkinfo
        assert_equal(self.nodes[0].getnetworkinfo()["streampolicies"], "BlockPriority,Default")
        assert_equal(self.nodes[1].getnetworkinfo()["streampolicies"], "BlockPriority,Default")
        assert_equal(self.nodes[2].getnetworkinfo()["streampolicies"], "BlockPriority,Default")
        assert_equal(self.nodes[3].getnetworkinfo()["streampolicies"], "Default")

        # Connect the new node to one of the existing nodes and check that they establish a Default association
        connect_nodes(self.nodes, 1, 3)
        expected1 = [
            {
                'id'           : 0,                                 # An association to node0
                'associd'      : '<UNKNOWN>',
                'streampolicy' : 'BlockPriority',
                'streams'      : ['GENERAL', 'DATA1']
            },
            {
                'id'           : 2,                                 # An association to node3
                'associd'      : '<UNKNOWN>',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[1], expected1), timeout=5)
        expected3 = [
            {
                'id'           : 0,                                 # An association to node1
                'associd'      : '<UNKNOWN>',
                'streampolicy' : 'Default',
                'streams'      : ['GENERAL']
            },
        ]
        wait_until(lambda: self.check_peer_info(self.nodes[3], expected3), timeout=5)


if __name__ == '__main__':
    P2PAssociation().main()
