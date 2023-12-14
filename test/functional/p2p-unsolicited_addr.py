#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Check that unsolicted ADDR messages don't get accepted and relayed.
"""

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time


# A connection handler that records recieved ADDR messages
class TestNode(NodeConnCB):
    def __init__(self, id):
        super().__init__()
        self.id = id
        self.gotAddr = False

    def on_addr(self, conn, message):
        self.gotAddr = True
        #print("Got ADDR message from peer {}".format(self.id))


# 1) Connect a number of real nodes together in a mesh.
# 2) Feed in an unsolicited ADDR msg to one of those nodes.
# 3) If that nodes accepts the ADDR, it will forward it to its other peer and
#    that peer will eventually also feed it back to us.
class UnsolictedAddr(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [["-whitelist=127.0.0.1"]] * self.num_nodes

    def setup_network(self):
        self.setup_nodes()
        connect_nodes_mesh(self.nodes)

    def run_test(self):
        # Create node connection handlers
        test_nodes = []
        connections = []
        for i in range(self.num_nodes):
            node = TestNode(i)
            connection = NodeConn('127.0.0.1', p2p_port(i), self.nodes[i], node)
            node.add_connection(connection)
            connections.append(connection)
            test_nodes.append(node)

        # Launch network handling thread
        NetworkThread().start()

        # Wait for all nodes to come up
        self.log.info("Waiting for veracks")
        for node in test_nodes:
            node.wait_for_verack()

        # Send unsolicted ADDR to node0
        self.log.info("Sending unsolicited ADDR")
        addrs = []
        for port in range(10000, 10010):
            addrs.append("104.20.31.65:%d" % port)

        maddr = msg_addr()
        for addr in addrs:
            ca = CAddress(addr.split(":")[0],int(addr.split(":")[1]))
            maddr.addrs.append(ca)
        connections[0].send_message(maddr)

        # The address sending logic using a Poisson distributed random delay, so we can't
        # guarantee that it would have sent after a set amount of time. Just wait a reasonably
        # long time to give us a decent chance.
        time.sleep(60)

        for node in test_nodes:
            assert(node.gotAddr == False)


if __name__ == '__main__':
    UnsolictedAddr().main()
