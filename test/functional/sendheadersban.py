#!/usr/bin/env python3
# Copyright (c) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

import time
from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.block_announced = False
        self.last_blockhash_announced = None


class TestSendHeadersBanScore(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes=1

    def run_test(self):
        #assert_equal(self.nodes[0].getconnectioncount(), 2)
        self._test_sendheadersban()

    def _test_sendheadersban(self):

        # Setup the p2p connections and start up the network thread.
        inv_node = TestNode()
        test_node = TestNode()

        self.p2p_connections = [inv_node, test_node]

        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], inv_node))
        # Set nServices to 0 for test_node, so no block download will occur outside of
        # direct fetching
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node, services=0))
        inv_node.add_connection(connections[0])
        test_node.add_connection(connections[1])

        NetworkThread().start()  # Start up network handling in another thread

        # Test logic begins here
        inv_node.wait_for_verack()
        test_node.wait_for_verack()

        # Ensure verack's have been processed by our peer
        inv_node.sync_with_ping()
        test_node.sync_with_ping()

        peerInfoPreSendHeaderMsg=self.nodes[0].getpeerinfo()
        #print ([peer['banscore'] for peer in peerInfoPreSendHeaderMsg])
        #print (peerInfoPreSendHeaderMsg[1]['banscore'])
        for i in range(10):
            test_node.send_message(msg_sendheaders())
            time.sleep(1)
        peerInfoPostSendHeaderMsg=self.nodes[0].getpeerinfo()
        #print (peerInfoPostSendHeaderMsg[1]['banscore'])
        assert_greater_than(peerInfoPostSendHeaderMsg[1]['banscore'],peerInfoPreSendHeaderMsg[1]['banscore'])

        return


if __name__ == '__main__':
    TestSendHeadersBanScore().main()
