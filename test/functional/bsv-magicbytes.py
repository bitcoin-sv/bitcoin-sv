#!/usr/bin/env python3
# Copyright (c) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check the P2P connection with magicbytes parameter.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import connect_nodes_bi, wait_until


class MagicBytes(BitcoinTestFramework):

    def set_test_params(self):
        self._magicbyte='0a0b0c0d'
        self.num_nodes = 2
        self._extra_args_same = [['-magicbytes={}'.format(self._magicbyte)]]*self.num_nodes
        self._extra_args_diff = [[],['-magicbytes={}'.format(self._magicbyte)]]
        self.extra_args = self._extra_args_same

    def setup_network(self):
        self.setup_nodes()
        connect_nodes_bi(self.nodes,0,1)

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args, timewait=900)
        self.start_nodes()

    def _print_connections(self):
        for i in range(self.num_nodes):
            print("Node {} connections: {}".format(i, self.nodes[i].getconnectioncount()))

    def run_test(self):

        self.log.info("Testing connections with the same magicbytes")
        wait_until(lambda: self.nodes[0].getconnectioncount() == 2)
        wait_until(lambda: self.nodes[1].getconnectioncount() == 2)

        self.log.info("Testing connections with different magicbytes")
        self.extra_args = self._extra_args_diff
        self.stop_nodes()
        self.nodes.clear()
        self.setup_network()
        assert(self.nodes[0].getconnectioncount() == 0)
        assert(self.nodes[1].getconnectioncount() == 0)


if __name__ == '__main__':
    MagicBytes().main()
