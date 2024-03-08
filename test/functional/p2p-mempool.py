#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2019  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.mininode import msg_mempool
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
import time

# This test checks different cases of handling mempool requests.
# If a peer is not whitelisted:
#     If rejectmempoolrequest=true (default value), mempool request is always rejected.
#     If rejectmempoolrequest=false mempool request is rejected only if peerbloomfilters=0.
# Is a peer is whitelisted, mempool request is never rejected.


class P2PMempoolTests(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.num_peers = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        def runTestWithParams(description, args, expectedReject):
            with self.run_node_with_connections(description, 0, args, self.num_peers) as connections:
                # request mempool
                connections[0].cb.send_message(msg_mempool())
                if not expectedReject:
                    time.sleep(1)
                    # mininode must not be disconnected at this point
                    assert_equal(len(self.nodes[0].getpeerinfo()), 1)
                else:
                    connections[0].cb.wait_for_disconnect()
                    # mininode must be disconnected at this point
                    assert_equal(len(self.nodes[0].getpeerinfo()), 0)

        test_cases = [
            ["Scenario: peerbloomfilters=0, rejectMempoolRequests=false, not whitelisted", ['-peerbloomfilters=0', '-rejectmempoolrequest=0'], True],
            ["Scenario: peerbloomfilters=1, rejectMempoolRequests=false, not whitelisted", ['-peerbloomfilters=1', '-rejectmempoolrequest=0'], False],
            ["Scenario: peerbloomfilters=0, rejectMempoolRequests=true (default), not whitelisted", ['-peerbloomfilters=0'], True],
            ["Scenario: peerbloomfilters=1, rejectMempoolRequests=true (default), not whitelisted", ['-peerbloomfilters=1'], True],
            ["Scenario: peerbloomfilters=0, rejectMempoolRequests=false, whitelisted", ['-peerbloomfilters=0', '-rejectmempoolrequest=0', '-whitelist=127.0.0.1'], False],
            ["Scenario: peerbloomfilters=1, rejectMempoolRequests=false, whitelisted", ['-peerbloomfilters=1', '-rejectmempoolrequest=0', '-whitelist=127.0.0.1'], False],
            ["Scenario: peerbloomfilters=0, rejectMempoolRequests=true (default), whitelisted", ['-peerbloomfilters=0', '-whitelist=127.0.0.1'], False],
            ["Scenario: peerbloomfilters=1, rejectMempoolRequests=true (default), whitelisted", ['-peerbloomfilters=1', '-whitelist=127.0.0.1'], False]
        ]

        for test_case in test_cases:
            runTestWithParams(test_case[0], test_case[1], test_case[2])


if __name__ == '__main__':
    P2PMempoolTests().main()
