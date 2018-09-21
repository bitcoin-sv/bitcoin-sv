#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import datetime
# Test the functionality -broadcastdelay if it work as expected. We create N transaction and broadcast between 2 nodes
# The average broadcast time is calculated :
#  - Without the functionality, the broadcast delay is random with an average of 5 second
#  - With the functionality, the broadcast delay is controlled by the option -broadcastdelay, and is deterministic. We test it greater than 10second
#
class BroadcastDelayTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self._nbSecondDelay = 10
        self._fixedDelayArgs = [['-broadcastdelay={}'.format(1000000*self._nbSecondDelay)], ['-broadcastdelay={}'.format(1000000*self._nbSecondDelay)]]
        self.nbTransTest = 3 ## Increase the number of transactions to test will increase the test quality

    def setup_network(self):
        self.add_nodes(self.num_nodes)
        self.start_node(0)
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all([self.nodes[0:1]])

    def run_test(self):
        avr_broadcast_random = self.test_average_broadcast()
        self.log.info("Test random broadcast, delay average {}.{}s".format(avr_broadcast_random.seconds,avr_broadcast_random.microseconds))
        assert(avr_broadcast_random.seconds < 7)## Random broadcast use Poisson distribution with average of 5 seconds. We test it less than 7 second

        ## Reset the network : disconnect all previous connection and stop all nodes
        disconnect_nodes(self.nodes[0],1)
        disconnect_nodes(self.nodes[1],0)
        self.stop_nodes()
        self.start_nodes(self._fixedDelayArgs)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all([self.nodes[0:1]])

        avr_broadcast_fixed = self.test_average_broadcast()
        self.log.info("Test fixed broadcast, delay average {}.{}s".format(avr_broadcast_fixed.seconds,avr_broadcast_fixed.microseconds))
        assert(avr_broadcast_fixed.seconds >= self._nbSecondDelay)

    def test_average_broadcast(self):
        ###  create transactions and sync between nodes
        begin_test = datetime.datetime.now()

        for iter in range(self.nbTransTest):
            txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.1)
            self.sync_all()

        end_test = datetime.datetime.now()
        elapsed_test = end_test - begin_test
        avg_sync_op = elapsed_test/ self.nbTransTest
        return avg_sync_op

if __name__ == '__main__':
    BroadcastDelayTest().main()
