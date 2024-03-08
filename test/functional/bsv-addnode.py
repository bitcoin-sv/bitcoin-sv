#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test the handling of the addnode RPC. Check the number of peers addnodable
is configurable.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, p2p_port, wait_until


class AddNodeTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 5

        self.defaultNodeArgs = ['-maxaddnodeconnections=2']
        self.increasedNodeArgs = ['-maxaddnodeconnections=3']
        self.extra_args = [self.defaultNodeArgs] * self.num_nodes

    def add_node_conn(self, from_node_num, to_node_num):
        ip_port = "127.0.0.1:" + str(p2p_port(to_node_num))
        self.nodes[from_node_num].addnode(ip_port, "add")

        def conn_up(subver):
            for peer in self.nodes[from_node_num].getpeerinfo():
                if subver in peer['subver']:
                    if peer['version'] != 0:
                        return True
            return False

        # Poll until version handshake complete
        subver = "testnode%d" % to_node_num
        try:
            wait_until(lambda: conn_up(subver), timeout=10)
        except Exception as e:
            return False

        return True

    # Returns number of peers added by addnode from the getpeerinfo RPC output
    def count_addnode_peers(self, node_index):
        return sum(peer['addnode'] for peer in self.nodes[node_index].getpeerinfo())

    def run_test(self):
        # Get out of IBD
        self.nodes[0].generate(1)
        self.sync_all()

        # Restart node0 to reset connection counts
        self.stop_node(0)
        self.start_node(0, extra_args=self.defaultNodeArgs)

        # Take us up to the initial addnode limit of 2 for node0
        self.log.info("Taking node0 up to the addnode limit")
        for i in range(2, 4):
            assert(self.add_node_conn(0, i))
        assert_equal(self.count_addnode_peers(0), 2)
        assert_equal(len(self.nodes[0].getaddednodeinfo()), 2)

        # Can't add another without going over the addnode limit
        self.log.info("Taking node0 over the addnode limit")
        assert(not self.add_node_conn(0, 4))
        assert_equal(self.count_addnode_peers(0), 2)
        assert_equal(len(self.nodes[0].getaddednodeinfo()), 3)

        # Restart node0 with an increased addnode connection limit
        self.log.info("Restarting node0 with a higher addnode limit")
        self.stop_node(0)
        self.start_node(0, extra_args=self.increasedNodeArgs)

        # Can now addnode up to the new higher limit
        self.log.info("Taking node0 up to the new higher addnode limit")
        for i in range(2, 5):
            assert(self.add_node_conn(0, i))
        assert_equal(self.count_addnode_peers(0), 3)
        assert_equal(len(self.nodes[0].getaddednodeinfo()), 3)


if __name__ == '__main__':
    AddNodeTest().main()
