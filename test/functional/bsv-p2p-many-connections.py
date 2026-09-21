#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin SV developers
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
More than FD_SETSIZE simultaneous P2P connections.

With the select()-based socket loop -maxconnections was silently clamped to
FD_SETSIZE minus some headroom (about 865) and any socket numbered >= 1024
was rejected. With poll() the only bound is RLIMIT_NOFILE. This test asks for
well over 1024 connections, opens them all, checks that a late arrival is
still accepted, and that the node shuts down cleanly afterwards.

The test process itself needs a descriptor per connection, so it raises its
own soft limit first and skips if the hard limit cannot accommodate it.
"""
import resource
import sys

from test_framework.mininode import P2PHandler, P2PEventHandler
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.transport import Connection
from test_framework.util import check_for_log_msg, p2p_port, wait_until

CONNECTIONS = 1100
# Outbound slots (8) plus the feeler plus headroom for the late connection.
MAX_CONNECTIONS = CONNECTIONS + 8 + 1 + 16
# Test process: one socket per connection plus its own housekeeping.
HARNESS_FDS = CONNECTIONS + 256
# bitcoind: connections, its core reserve, addnode slots and the default
# coins LevelDB budget (up to 8192) all come out of the same hard limit.
MIN_HARD_LIMIT = 16384


class P2PManyConnections(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        if not sys.platform.startswith('linux'):
            raise SkipTest("This test relies on Linux descriptor limits.")

        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        if hard != resource.RLIM_INFINITY and hard < MIN_HARD_LIMIT:
            raise SkipTest("Hard RLIMIT_NOFILE {} is too low for {} connections."
                           .format(hard, CONNECTIONS))
        if soft < HARNESS_FDS:
            resource.setrlimit(resource.RLIMIT_NOFILE, (HARNESS_FDS, hard))

        self.add_nodes(self.num_nodes)

    def run_test(self):
        args = ['-maxconnections={}'.format(MAX_CONNECTIONS),
                '-maxoutboundconnections=8']

        with self.run_node_with_connections("more than FD_SETSIZE connections", 0,
                                            args, CONNECTIONS):
            node = self.nodes[0]
            wait_until(lambda: len(node.getpeerinfo()) == CONNECTIONS, timeout=120)

            # -maxconnections was honoured as given, not clamped to FD_SETSIZE.
            assert not check_for_log_msg(node, "Reducing -maxconnections")
            assert not check_for_log_msg(node, "non-selectable socket")

            # A late connection, numbered well past FD_SETSIZE, still handshakes.
            late_handler = P2PEventHandler()
            late = P2PHandler(Connection('127.0.0.1', p2p_port(0), late_handler), node)
            late_handler.add_connection(late)
            try:
                late_handler.wait_for_verack()
                wait_until(lambda: len(node.getpeerinfo()) == CONNECTIONS + 1, timeout=60)
            finally:
                late.close()
            wait_until(lambda: len(node.getpeerinfo()) == CONNECTIONS, timeout=60)


if __name__ == '__main__':
    P2PManyConnections().main()
