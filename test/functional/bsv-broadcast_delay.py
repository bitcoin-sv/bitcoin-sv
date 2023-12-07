#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import *
from test_framework.script import CScript, OP_TRUE
import datetime
import contextlib

# Test if the functionality -broadcastdelay works as expected.
# Create 2 connections (connection1 and connection2) to bitcoind node and measure how long it takes for connection2 to receive a transaction that connection1 sends to bitcoind.
# 1. -broadcastdelay is set to 0 to calculate the minimal delay that network and bitcoind need for this functionality. This should not be too large.
# 2. Then, -broadcastdelay is not set, which means the default is used which is 150ms.
#    Time for connection2 to receive a transaction should be around 150ms.
# 3. At last, functionality is tested with -broadcastdelay set to 1 second to test it with some larger times.


class NetworkThreadPinging(Thread):
    def __init__(self, conn):
        super().__init__()
        self.conn = conn

    def run(self):
        while True:
            conn = self.conn
            if conn is None:
                break
            conn.send_message(msg_ping())
            time.sleep(0.001)

    def stop(self):
        self.conn = None


class BroadcastDelayTest(BitcoinTestFramework):

    # ensure funding and returns  given number of transcations without submitting them
    def make_transactions(self, num_transactions):
        # Generate some blocks to have enough spendable coins
        node = self.nodes[0]
        node.generate(101)

        # Create funding transactions that will provide funds for other transcations
        out_value = 10000
        ftx = CTransaction()
        for i in range(num_transactions):
            ftx.vout.append(CTxOut(out_value, CScript([OP_TRUE])))

        # fund the transcation:
        ftxHex = node.fundrawtransaction(ToHex(ftx),{'changePosition' : len(ftx.vout)})['hex']
        ftxHex = node.signrawtransaction(ftxHex)['hex']
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()

        node.sendrawtransaction(ftxHex)

        node.generate(1) # ensure that mempool is empty to avoid 'too-long-mempool-chain' errors in next test

        # Create transactions that depends on funding transactions that has just been submitted:
        txs = []
        for i in range(num_transactions):
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(ftx.sha256, i), b''))
            tx.vout.append(CTxOut(out_value-1000, CScript([OP_TRUE])))
            tx.rehash()
            txs.append(tx)

        return txs

    def set_test_params(self):
        self.num_nodes = 1
        self.num_peers = 3

    def setup_network(self):
        self.setup_nodes()

    # submits requested number of transactions from txs and returns timings
    def syncNodesWithTransaction(self, num_transactions, txs, connection1, connection2):
        times = []
        for i in range(num_transactions):
            tx = txs.pop(0)
            begin_test = datetime.datetime.now()

            # node1 sends transaction to bitcoind
            connection1.cb.send_message(msg_tx(tx))
            # assert that node2 gets INV with previously sent transaction
            msg = [CInv(CInv.TX, tx.sha256)]
            connection2.cb.wait_for_inv(msg, check_interval=0.001) # Use small check interval to minimize errors in elapsed time measurement

            end_test = datetime.datetime.now()
            elapsed_test = end_test - begin_test
            times.append(elapsed_test)
        # calculate average of times without the first time (ignoring warm-up)
        return sum(times[1:], datetime.timedelta(0)) / len(times[1:])

    def run_test(self):
        @contextlib.contextmanager
        def run_pinging_connection(connection):
            # Connection3 is used here only for constantly pinging bitcoind node.
            # It is needed so that bitcoind is awake all the time (without 100ms sleeps).
            syncThr = NetworkThreadPinging(connection)
            try:
                syncThr.start()
                yield
            finally:
                syncThr.stop()
                syncThr.join()

        # Make some transactions to work with.
        txs = self.make_transactions(50)
        self.stop_node(0)

        # Use small poll timeout to minimize errors in elapsed time measurement
        NetworkThread.poll_timeout = 0.001

        num_txns_to_sync = 15
        # 1. Send 15 transactions with broadcast delay of 0 seconds to calculate average minimal broadcast delay.
        ### txnpropagationfreq and txnvalidationasynchrunfreq is set to 1ms to limit its effect on propagation test.
        with self.run_node_with_connections("calculating overhead", 0,
                                            ['-broadcastdelay=0', '-txnpropagationfreq=1', '-txnvalidationasynchrunfreq=1'], self.num_peers) as connections:
            with run_pinging_connection(connections[2]):
                min_propagation_delay = self.syncNodesWithTransaction(num_txns_to_sync, txs, connections[0], connections[1])
                self.log.info("Minimal propagation delay: %s", min_propagation_delay)
                assert(min_propagation_delay < datetime.timedelta(milliseconds=200)) # minimal propagation delay must not be too large

        # 2. Send 15 transactions with default broadcast delay (150ms) and calculate average broadcast delay
        with self.run_node_with_connections("calculating propagation delay (default)", 0,
                                            ['-txnpropagationfreq=1', '-txnvalidationasynchrunfreq=1'], self.num_peers) as connections:
            with run_pinging_connection(connections[2]):
                propagation_delay = self.syncNodesWithTransaction(num_txns_to_sync, txs, connections[0], connections[1])
                self.log.info("Propagation delay, expected 150ms: %s", propagation_delay)
                assert(propagation_delay < datetime.timedelta(milliseconds=250)) # allow variation of +-100ms
                assert(propagation_delay > datetime.timedelta(milliseconds=50))

        # 3. Send 15 transactions with broadcast delay 1s
        with self.run_node_with_connections("calculating propagation delay (1000ms)", 0, ['-broadcastdelay=1000'], self.num_peers) as connections:
            with run_pinging_connection(connections[2]):
                propagation_delay = self.syncNodesWithTransaction(num_txns_to_sync, txs, connections[0], connections[1])
                self.log.info("Propagation delay, expected 1000ms: %s", propagation_delay)
                assert(propagation_delay < datetime.timedelta(milliseconds=1500)) # allow variation of +-500ms
                assert(propagation_delay > datetime.timedelta(milliseconds=500))


if __name__ == '__main__':
    BroadcastDelayTest().main()
