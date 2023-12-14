#!/usr/bin/env python3
# Copyright (c) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check the P2P transaction propagation changes.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time


class P2PTxnPropagation(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 8
        self.num_txns = 200
        self.setup_clean_chain = True
        self.extra_args = [['-broadcastdelay=5000']] * self.num_nodes

    def setup_network(self):
        self.setup_nodes()
        for n in range(1, self.num_nodes):
            connect_nodes(self.nodes, 0, n)

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args, timewait=900)
        self.start_nodes()

    def make_txns(self, fee, num):
        for t in range(num):
            random_transaction(self.nodes[0:1], Decimal("0.0001"), fee, Decimal("0.0"), 0)

    # Test basic transaction propogation
    def test_basic_transactions(self):
        self.log.info("Testing basic transaction propagation")

        # Get fee to use for transactions so we can be sure they are accepted by all nodes
        fee = self.nodes[0].getnetworkinfo()['relayfee']

        for test in range(10):
            # Add some random txns to node0's mempool
            self.make_txns(fee, 10)

            # Make sure everyone reaches agreement still
            sync_mempools(self.nodes, wait=1)

    # For checking if the txn queues for all peers have drained yet
    def check_queue_drains(self):
        peerinfo = self.nodes[0].getpeerinfo()
        for peer in peerinfo:
            txninvsize = peer['txninvsize']
            if(txninvsize > 0):
                return False
        return True

    # For checking the txns make it into all peers mempools
    def check_final_mempool(self):
        for n in range(0, self.num_nodes):
            mempoolsize = self.nodes[n].getmempoolinfo()['size']
            if(mempoolsize != self.num_txns):
                return False
        return True

    # Test full transaction propogation
    def test_full_transactions(self):
        self.log.info("Testing full transaction propagation")

        # Get fee to use for transactions so we can be sure they are accepted by all nodes
        fee = self.nodes[0].getnetworkinfo()['relayfee']

        # Check initially there are no queued txns
        for node in self.nodes:
            peerinfo = node.getpeerinfo()
            for peer in peerinfo:
                txninvsize = peer['txninvsize']
                assert_equal(txninvsize, 0)

        # Feed in some transactions and verify they make it to the mempool & txn propagation queue.
        # This check is racy I know, but I think it's a good assertion that given the
        # limits on the rate of transaction propagation, the queue will not have totally drained
        # in the time between submitting them and checking the queue length here.
        self.make_txns(fee, self.num_txns)
        mempoolsize = self.nodes[0].getmempoolinfo()['size']
        assert_equal(mempoolsize, self.num_txns)
        peerinfo = self.nodes[0].getpeerinfo()
        for peer in peerinfo:
            txninvsize = peer['txninvsize']
            if(txninvsize > 0):
                assert(txninvsize <= self.num_txns)

        # Verify the txn propagation queue drains out
        wait_until(self.check_queue_drains)

        # Verify all txns make it to all peers
        wait_until(self.check_final_mempool)
        sync_mempools(self.nodes)

    def run_test(self):
        # Make some coins to spend
        self.log.info("Mining blocks...")
        self.nodes[0].generate(501)
        sync_blocks(self.nodes)

        self.test_full_transactions()
        self.test_basic_transactions()


if __name__ == '__main__':
    P2PTxnPropagation().main()
