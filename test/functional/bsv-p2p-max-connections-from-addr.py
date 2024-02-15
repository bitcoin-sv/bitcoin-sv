#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# A test to limit p2p connections from the same address

from test_framework.mininode import NetworkThread, NodeConn, NodeConnCB
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, check_for_log_msg, p2p_port, wait_until


class P2PMaxConnectionsFromAddr(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        ip = '127.0.0.1'
        maxconnectionsfromaddr = 3
        args = [f'-maxconnectionsfromaddr={maxconnectionsfromaddr}']

        with self.run_node_with_connections("Test max connections from addr",
                                            0,
                                            args,
                                            maxconnectionsfromaddr,
                                            ip=ip):

            assert_equal(len(self.nodes[0].getpeerinfo()), maxconnectionsfromaddr)

            connCb = NodeConnCB()
            c = NodeConn(ip, p2p_port(0), self.nodes[0], connCb)
            connCb.add_connection(c)
            err = f"connection from {ip} dropped: too many connections from the same address"
            wait_until(lambda: check_for_log_msg(self.nodes[0], err))

            assert_equal(len(self.nodes[0].getpeerinfo()), maxconnectionsfromaddr)

        with self.run_node_with_connections("Test max connections from addr is not restricted",
                                            0,
                                            [f'-whitelist={ip}'] + args,
                                            maxconnectionsfromaddr + 1,
                                            ip=ip):

            assert_equal(len(self.nodes[0].getpeerinfo()), maxconnectionsfromaddr + 1)

        with self.run_node_with_connections("Test unrestricted connections from addr",
                                            0,
                                            [],
                                            20,
                                            ip=ip):

            assert_equal(len(self.nodes[0].getpeerinfo()), 20)


if __name__ == '__main__':
    P2PMaxConnectionsFromAddr().main()
