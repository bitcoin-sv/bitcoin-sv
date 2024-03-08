#!/usr/bin/env python3
# Copyright (c) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check the P2P connection handling after moving to use shared_ptrs.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import random


class P2PConnections(BitcoinTestFramework):

    def clear_disconnections(self):
        for i in range(self.num_nodes):
            self.disconnections[i] = []

    def set_test_params(self):
        self.num_nodes = 8
        self.setup_clean_chain = True
        self.extra_args = [[]] * self.num_nodes

        self.disconnections = {}
        self.clear_disconnections()

    def make_connections(self):
        # connect every node to every other node
        for i in range(self.num_nodes):
            for j in range(self.num_nodes):
                if(j != i):
                    connect_nodes_bi(self.nodes, i, j)

    def setup_network(self):
        self.setup_nodes()
        self.make_connections()
        sync_blocks(self.nodes)

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args, timewait=900)
        self.start_nodes()

    def print_connections(self):
        for i in range(self.num_nodes):
            print("Node {} connections: {}".format(i, self.nodes[i].getconnectioncount()))

    def do_disconnection(self):
        while True:
            # Pick 2 nodes we haven't already disconnected
            node1 = random.randint(0, self.num_nodes - 1)
            node2 = random.randint(0, self.num_nodes - 1)
            if node1 != node2:
                if node2 not in self.disconnections[node1]:
                    # Disconnect them
                    self.disconnections[node1].append(node2)
                    self.disconnections[node2].append(node1)
                    self.log.info("Disconnecting node {} and {}".format(node1, node2))
                    disconnect_nodes_bi(self.nodes, node1, node2)
                    break

    def do_reconnection(self):
        # Remake disconnected connections
        for node1, disconnects in self.disconnections.items():
            for node2 in disconnects:
                connect_nodes(self.nodes, node1, node2)

        # Reset map of disconnections to empty
        self.clear_disconnections()

    def check_connections(self):
        # Check the reported number of connections from each node is what we expect
        for i in range(self.num_nodes):
            num_disconnects = len(self.disconnections[i]) * 2
            expected_count = ((self.num_nodes - 1) * 2) - num_disconnects
            wait_until(lambda: self.nodes[i].getconnectioncount() == expected_count, timeout=10)

    # Test we haven't broken connection management
    def test_connections(self):
        self.log.info("Testing connections")

        # Initial condition check
        self.check_connections()

        for test in range(10):
            # Do some disconnections
            for i in range(10):
                self.do_disconnection()
            self.check_connections()

            # And put everything back again
            self.do_reconnection()

    def run_test(self):
        self.test_connections()


if __name__ == '__main__':
    P2PConnections().main()
