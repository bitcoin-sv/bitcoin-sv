#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, OP_TRUE
from test_framework.blocktools import create_block, create_coinbase
import datetime
import contextlib

# Test the functionality -broadcastdelay if it works as expected. 
# 2 connections (connection1 and connection2) are created to bitcoind node and test how long it takes for connection2 to receive a transaction that connection1 sends to bitcoind.
# 1. -broadcastdelay is set to 0 to calculate the overhead that network and bitcoind need for this functionality.
# 2. Then, -broadcastdelay is not set, which means the default is used which is 150ms. 
#    Time for connection2 to receive a transaction (minus overhead calculated in #1) should be around 150ms.
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
        ftxHex = node.fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex'] 
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
            tx.vout.append(CTxOut(out_value -1000, CScript([OP_TRUE])))
            tx.rehash()
            txs.append(tx)
            
        return txs


    def set_test_params(self):
        self.num_nodes = 1
        
    def setup_network(self):
        self.add_nodes(self.num_nodes)
        # txnpropagationfreq is set to 1ms to limit its effect on propagation test. 
        # Default value 1s is not suitable for this test, since it is much larger than 150ms.
        self.start_node(0, ['-broadcastdelay=0', '-txnpropagationfreq=1'])


    # submits requested number of transactions from txs and returns timings
    def syncNodesWithTransaction(self, num_transactions, txs, connection1, connection2): 
        times = []
        for i in range(1, num_transactions):
            tx = txs.pop(0)
            begin_test = datetime.datetime.now()

            # node1 sends transaction to bitcoind
            connection1.send_message(msg_tx(tx))
            # assert that node2 gets INV with previously sent transaction
            msg = [CInv(1, tx.sha256)]
            connection2.wait_for_inv(msg)

            end_test = datetime.datetime.now()
            elapsed_test = end_test - begin_test
            times.append(elapsed_test)

        # calculate average of times without the first time (ignoring warm-up)
        return sum(times[1:], datetime.timedelta(0)) / len(times[1:])

    def run_test(self):

        @contextlib.contextmanager
        def run_connection(connection1, connection2, title):
            logger.debug("setup %s", title)

            # Connection3 is used here only for constantly pinging bitcoind node. 
            # It is needed so that bitcoind is awake all the time (without 100ms sleeps).
            connection3 = NodeConnCB()

            connections = []
            connections.extend([NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], connection1),
                            NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], connection2),
                            NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], connection3)])

            connection1.add_connection(connections[0])
            connection2.add_connection(connections[1])
            connection3.add_connection(connections[2])

            thr = NetworkThread()
            thr.start()

            connection1.wait_for_verack()
            connection2.wait_for_verack()
            connection3.wait_for_verack()

            syncThr = NetworkThreadPinging(connection3)
            syncThr.start()

            logger.debug("before %s", title)
            yield
            logger.debug("after %s", title)
  
            syncThr.stop()
            syncThr.join()

            connections[0].close()
            connections[1].close()
            connections[2].close()
            del connections
            thr.join()

            disconnect_nodes(self.nodes[0],1)
            self.stop_node(0)

            logger.debug("finished %s", title)

        txs =  self.make_transactions(50)

        # 1. Send 15 transactions with broadcast delay of 0 seconds to calculate average overhead.
        connection1 = NodeConnCB()
        connection2 = NodeConnCB()
        
        with run_connection(connection1, connection2, "calculating overhead"):
            average_overhead = self.syncNodesWithTransaction(15, txs, connection1, connection2)
            self.log.info("Average overhead: %s", average_overhead)
  

        # 2. Send 15 transactions with default broadcast delay (150ms) and calculate average broadcast delay
        self.start_node(0, ['-txnpropagationfreq=1'])

        connection1 = NodeConnCB()
        connection2 = NodeConnCB()

        with run_connection(connection1, connection2, "calculating propagation delay (default)"):
            average_roundtrip = self.syncNodesWithTransaction(15, txs, connection1, connection2)
            propagation_delay = average_roundtrip - average_overhead
            self.log.info("Propagation delay, expected 150ms: %s", propagation_delay)
            assert(propagation_delay < datetime.timedelta(milliseconds=300))
            assert(propagation_delay > datetime.timedelta(milliseconds=30))


        # 3. Send 15 transactions with broadcast delay 1s
        self.start_node(0, ['-broadcastdelay=1000'])

        connection1 = NodeConnCB()
        connection2 = NodeConnCB()

        with run_connection(connection1, connection2, "calculating propagation delay (1000ms)"):
            average_roundtrip_1s_delay = self.syncNodesWithTransaction(15, txs, connection1, connection2)
            propagation_delay = average_roundtrip_1s_delay - average_overhead
            self.log.info("Propagation delay, expected 1000ms: %s", propagation_delay)
            assert(propagation_delay < datetime.timedelta(milliseconds=1500))
            assert(propagation_delay > datetime.timedelta(milliseconds=500))

 
if __name__ == '__main__':
    BroadcastDelayTest().main()
