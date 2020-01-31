#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, OP_TRUE, OP_RETURN
from test_framework.blocktools import create_block, create_coinbase
from enum import Enum
import datetime
import contextlib
import random

# This test is to verify/check a processing of P2P txns depending on node's configuration.
# It tries to emulate a behavior of a single node in a kind of end-to-end scenario (limited env).
# - node0 is a testing node which receives txns via p2p.
# - txns are propageted by callbacks to the node0.
# - test cases without double sped issue
# - test cases with double spend money

# This class might be moved into a different file.
class switch(object):
    def __init__(self, value):
        self.value = value
        self.fall = False

    def __iter__(self):
        """Return the match method once, then stop"""
        yield self.match
        raise StopIteration

    def match(self, *args):
        """Indicate whether or not to enter a case suite"""
        if self.fall or not args:
            return True
        elif self.value in args:
            self.fall = True
            return True
        else:
            return False

class TxType(Enum):
    standard = 1
    nonstandard = 2
    std_and_nonstd = 3 # standard and non-standard txns

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

class TxnValidatorP2PTxnsTest(BitcoinTestFramework):

    # Create given number of spend money transcations without submitting them
    def make_transactions(self, txtype, num_txns, create_double_spends=False):

        # Calculate how many found txns are needed to create a required spend money txns (num_txns)
        # - a fund txns are of type 1 - N (N=vouts_num_per_fund_txn)
        # - a spend money txns are of type 1-1 (one input & one output)
        def estimate_fund_txns_number(num_txns, vouts_num_per_fund_txn):
            fund_txns_num = 1
            if num_txns >= vouts_num_per_fund_txn:
                if num_txns % vouts_num_per_fund_txn == 0:
                    fund_txns_num = num_txns // vouts_num_per_fund_txn
                else:
                    fund_txns_num = num_txns // vouts_num_per_fund_txn + 1
            return fund_txns_num

        # Create funding transactions that will provide funds for other transcations
        def make_fund_txn(node, out_value, num_vout_txns):
            ftx = CTransaction()
            for i in range(num_vout_txns):
                ftx.vout.append(CTxOut(out_value, CScript([OP_TRUE])))
            # fund the transcation:
            ftxHex = node.fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex'] 
            ftxHex = node.signrawtransaction(ftxHex)['hex']         
            ftx = FromHex(CTransaction(), ftxHex)        
            ftx.rehash()
            return ftx, ftxHex

        # Create a spend txn
        def make_spend_txn(txtype, fund_txn_hash, out_value, vout_idx):
            spend_tx = CTransaction()
            spend_tx.vin.append(CTxIn(COutPoint(fund_txn_hash, vout_idx), b''))
            # Standard transaction
            if TxType.standard == txtype:
                spend_tx.vout.append(CTxOut(out_value-1000, CScript([OP_RETURN])))
            # Non-standard transaction
            elif TxType.nonstandard == txtype:
                spend_tx.vout.append(CTxOut(out_value-1000, CScript([OP_TRUE])))
            spend_tx.rehash()
            return spend_tx

        # 
        # Generate some blocks to have enough spendable coins 
        #
        node = self.nodes[0]
        node.generate(101)

        #
        # Estimate a number of required fund txns
        #
        out_value = 1000
        # Number of outputs in each fund txn
        fund_txn_num_vouts = 100
        fund_txns_num = estimate_fund_txns_number(num_txns, fund_txn_num_vouts)

        #
        # Create and send fund txns to the mempool
        #
        fund_txns = [] 
        for i in range(fund_txns_num):
            ftx, ftxHex = make_fund_txn(node, out_value, fund_txn_num_vouts)
            node.sendrawtransaction(ftxHex)
            fund_txns.append(ftx)
        # Ensure that mempool is empty to avoid 'too-long-mempool-chain' errors in next test
        node.generate(1)

        #
        # Create transactions that depend on funding transactions that has just been submitted:
        #
        txtype_to_create = txtype
        spend_txs = []
        for i in range(len(fund_txns)):
            for fund_txn_vout_idx in range(fund_txn_num_vouts):
                # If standard and non-standard txns are required then create equal (in size) sets.
                if TxType.std_and_nonstd == txtype:
                    if fund_txn_vout_idx % 2:
                        txtype_to_create = TxType.standard
                    else:
                        txtype_to_create = TxType.nonstandard
                spend_tx = make_spend_txn(txtype_to_create, fund_txns[i].sha256, out_value, fund_txn_vout_idx)
                if create_double_spends and len(spend_txs) < num_txns // 2:
                    # The first half of the array are double spend txns
                    spend_tx.vin.append(CTxIn(COutPoint(fund_txns[len(fund_txns) - i - 1].sha256, 0), b''))
                    spend_tx.rehash()
                spend_txs.append(spend_tx)
        return spend_txs

    def set_test_params(self):
        self.num_nodes = 1
        
    def setup_network(self):
        self.add_nodes(self.num_nodes)

    # Broadcast txns to the node0 (no double spends)
    def syncNodeWithTxnsNoDoubleSpends(self, txs, callback_connections):
        txnIdx = 0
        while True:
            for connIdx in range(len(callback_connections)):
                # A callback (peer == connIdx) sends transaction to bitcoind (node0)
                if txnIdx < len(txs):
                    callback_connections[connIdx].send_message(msg_tx(txs[txnIdx]))
                    txnIdx += 1
                else:
                    return

    # Broadcast txns to the node0 (with double spends)
    def syncNodeWithTxnsDoubleSpends(self, txs, callback_connections):
        for txnIdx in range(len(txs)):
            for connIdx in range(len(callback_connections)):
                # A callback (peer == connIdx) sends transaction to bitcoind (node0)
                callback_connections[connIdx].send_message(msg_tx(txs[txnIdx]))

    # A run test method to override.
    def run_test(self):

        def doublespends():
            return "doublespends"

        def nodoublespends():
            return "nodoublespends"


        def make_callback_connections(num_callbacks):
            return [NodeConnCB() for i in range(num_callbacks)]

        @contextlib.contextmanager
        def run_connection(title, callback_connections):
            logger.debug("Setup %s", title)

            # This connection is used here only for constantly pinging bitcoind node. 
            # It is needed so that bitcoind is awake all the time (without 100ms sleeps).
            pinging_connection = NodeConnCB()

            #
            # Create connections to the node0
            #
            connections = []
            for i in range(len(callback_connections)):
                connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], callback_connections[i]))
            # Add pinging connection as well            
            connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], pinging_connection))

            #
            # Link a given connection with a callback
            #
            for i in range(len(callback_connections)):
                callback_connections[i].add_connection(connections[i])
            pinging_connection.add_connection(connections[len(connections)-1])

            #
            # Start a network thread
            #
            thr = NetworkThread()
            thr.start()

            #
            # Wait for a verack message (callbacks & pinging node)
            #
            for i in range(len(callback_connections)):
                callback_connections[i].wait_for_verack()
            pinging_connection.wait_for_verack()

            #
            # Create and start NetworkThreadPinging
            #
            syncThr = NetworkThreadPinging(pinging_connection)
            syncThr.start()
            # Logging
            logger.debug("Before %s", title)
            yield
            logger.debug("After %s", title)
            # Wait for the pinging node to complete
            syncThr.stop()
            syncThr.join()

            #
            # Close all connections
            #
            for i in range(len(connections)):
                connections[i].close()
            # Delete connection's list
            del connections
            thr.join()
            # Disconnect node0
            disconnect_nodes(self.nodes[0], 1)
            # Stop the node0
            self.stop_node(0)
            logger.debug("Finished %s", title)

        # A switcher to control a type of sync operation for the node0 
        def sync_node_scenarios(sync_node_type, spend_txns, callback_connections):
            for case in switch(sync_node_type):
                if case(nodoublespends()):
                    self.syncNodeWithTxnsNoDoubleSpends(spend_txns, callback_connections)
                    break
                if case(doublespends()):
                    self.syncNodeWithTxnsDoubleSpends(spend_txns, callback_connections)
                    break
                if case(): # default
                    raise Exception('The value of sync_node_type={} is not defined'.format(sync_node_type))

        def runTestWithParams(sync_node_type, description, args, txtype, number_of_txns, number_of_peers, timeout, shuffle_txns=False):
            # In case of double spends only a half of the set will be valid.
            # A number of txns required to create a set of double spends should be an even number
            create_double_spends = (doublespends() == sync_node_type)
            expected_txns_num = number_of_txns
            if create_double_spends:
                if number_of_txns % 2:
                    raise Exception('Incorrect size given to create a set of double spend txns')
                expected_txns_num //= 2
            # Start the node0
            self.start_node(0, args)
            # Create a required number of spend money txns.
            result_txns = self.make_transactions(txtype, number_of_txns, create_double_spends)
            spend_txns = result_txns[0:number_of_txns]
            # Shuffle spend txns if required
            if shuffle_txns:
                random.shuffle(spend_txns)
            # Check if the test has got an enough amount of spend money transactions.
            assert_equal(len(spend_txns), number_of_txns)
            # Create a given number of callbacks to the node0.
            callback_connections = make_callback_connections(number_of_peers)
            assert_equal(len(callback_connections), number_of_peers)
            # Run all connections to the node0 (a network traffic simulation)
            with run_connection(description, callback_connections):
                # Broadcast txns to the node0
                sync_node_scenarios(sync_node_type, spend_txns, callback_connections)
                # Check if the validation queues are empty
                wait_until(lambda: self.nodes[0].rpc.getblockchainactivity()["transactions"] == 0, timeout=timeout)
                # Wait until the condition is met
                wait_until(lambda: len(self.nodes[0].getrawmempool()) == expected_txns_num, timeout=timeout)
                # Assert to confirm that the above wait is successfull (additional check for clarity)
                assert_equal(len(self.nodes[0].getrawmempool()), expected_txns_num)

        # To increase a number of txns taken from the queue by the Validator we need to change a default config.
        # This will allow us to observe and measure CPU utilization during validation (asynch mode) of p2p txns.

        #
        # No double spends: A definition of test cases dependent on node0's configuration.
        #
        test_cases = [
            # P2P-Scenario1
            # - 1K txns used, 1 peer connected to node0
            # 1. Checking how the node0 behaves with a default configuration
            # 2. From the Validator point of view the configuration might produce a following case (including fund & spend money txns):
            #    - Txnval-asynch: Got 28 new transactions
            #    - Txnval-asynch: Got 236 new transactions
            #    - Txnval-asynch: Got 762 new transactions
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario1 [nodoublespends]: "
                "-broadcastdelay (default), "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq (default) " # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",      # txn validator thread pool config
                # Node's configuration
                ['-broadcastdelay=150',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=100',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.standard,
                # A number of spend money txns used in the test
                1000,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                120
            ],

            # P2P-Scenario2
            # - 1K txns used, 10 peers connected to node0
            # - From the Validator point of view the configuration might produce a following case (including fund & spend money txns):
            #    - Txnval-asynch: Got 69 new transactions
            #    - Txnval-asynch: Got 468 new transactions
            #    - Txnval-asynch: Got 463 new transactions
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario2 [nodoublespends]: "
                "-broadcastdelay=0, "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq=200 "        # txn validator config
                "-numstdtxvalidationthreads=6 "           # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",       # txn validator thread pool config
                # Node's configuration
                ['-broadcastdelay=0',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=200',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                1000,
                # A number of peers connected to the node0
                10,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                120
            ],

            # P2P-Scenario3
            # - 5K txns used, 10 peers connected to node0
            # - From the Validator point of view the configuration might produce a following case:
            #    - Txnval-asynch: Got 156 new transactions
            #    - Txnval-asynch: Got 513 new transactions
            #    - Txnval-asynch: Got 1280 new transactions
            #    - Txnval-asynch: Got 2199 new transactions
            #    - Txnval-asynch: Got 852 new transactions
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario3 [nodoublespends]: "
                "-broadcastdelay=0, "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq=200 "       # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 "       # txn validator thread pool config
                "-persistmempool=0 ",
                # Node's configuration
                ['-broadcastdelay=0',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=200',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2',
                 '-persistmempool=0'],
                TxType.std_and_nonstd,
                # A number of spend money txns used in the test
                5000,
                # A number of peers connected to the node0
                10,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                360
            ]
        ]
        # Execute test cases.
        for test_case in test_cases:
            runTestWithParams(test_case[0], test_case[1], test_case[2], test_case[3], test_case[4], test_case[5], test_case[6])

        #
        # Double spends: A definition of test cases dependent on node0's configuration.
        #
        test_cases = [
            # P2P-Scenario1
            # - 1K txns used, 2 peers connected to node0
            # - Checking how the node0 behaves with a default configuration
            # - From the Validator point of view the configuration might produce a following case:
            #    - Txnval-asynch: Got 135 new transactions
            #    - Txnval-asynch: Got 142 new transactions
            #    - Txnval-asynch: Got 527 new transactions
            #    - Txnval-asynch: Got 816 new transactions
            #    - Txnval-asynch: Got 378 new transactions
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario1 [doublespends]: "
                "-broadcastdelay (default), "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq (default) " # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",      # txn validator thread pool config
                # Node's configuration
                ['-broadcastdelay=150',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=100',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                1000,
                # A number of peers connected to the node0
                2,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240,
                # Shuffle input txns
                False
            ],

            # P2P-Scenario2
            # - 1K txns used, 5 peers connected to node0
            # - From the Validator point of view the configuration might produce a following case:
            #    - Txnval-asynch: Got 94 new transactions
            #    - Txnval-asynch: Got 133 new transactions
            #    - Txnval-asynch: Got 227 new transactions
            #    - Txnval-asynch: Got 367 new transactions
            #    - Txnval-asynch: Got 179 new transactions
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario2 [doublespends]: "
                "-broadcastdelay=0, "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq=200"        # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",      # txn validator thread pool config
                # Node's configuration
                ['-broadcastdelay=0',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=200',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                1000,
                # A number of peers connected to the node
                5,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                720,
                # Shuffle input txns
                False
            ],
            
            # P2P-Scenario3
            # - 5K txns used, 20 peers connected to node0
            # - A config -txnvalidationasynchrunfreq=10 is set to increase a chance for double spend txn to be rejected
            #   by validation (not to put all double spend txns into the same validator's queue) what means
            #   to not be rejected by double spend detector when the txn is being queued.
            # - From the Validator point of view the configuration might produce a following case:
            #    - A number of queued transactions is between 1 and 21
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario3 [doublespends]: "
                "-broadcastdelay=0, "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq=10"         # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",      # txn validator thread pool config
                # Node's configuration
                ['-broadcastdelay=0',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=10',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                5000,
                # A number of peers connected to the node
                20,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                920,
                # Shuffle input txns
                False
            ],

            # P2P-Scenario4
            # - 5K txns used, a half of them are double spends, 1 peer connected to node0
            # - The first 2.5K txns are double spends
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario4 [doublespends]: "
                "-broadcastdelay=0, "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq (default)"  # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",      # txn validator thread pool config
                # Node's configuration
                ['-broadcastdelay=0',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=100',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.standard,
                # A number of spend money txns used in the test
                5000,
                # A number of peers connected to the node
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                920,
                # Shuffle input txns
                False
            ],

            # P2P-Scenario5
            # - 5K txns used, a half of them are double spends, 1 peer connected to node0
            # - All txns are shuffled as we want to achieve a random distribution of the input set.
            # - This test case disables mempool checks (for each and every txn) on the regtest
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario5 [doublespends]: "
                "-checkmempool=0"
                "-broadcastdelay=0, "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq (default)"  # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",      # txn validator thread pool config
                # Node's configuration
                ['-checkmempool=0',
                 '-broadcastdelay=0',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=100',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.std_and_nonstd,
                # A number of spend money txns used in the test
                5000,
                # A number of peers connected to the node
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240,
                # Shuffle input txns
                True
            ],

            # P2P-Scenario6
            # - 30K txns used, a half of them are double spends, 4 peer connected to node0
            # - The first 15K txns are double spends
            # - We don't wont to shuffle txns as it reduces a number of txns detected as double spends
            # - This test case disables mempool checks (for each and every txn) on the regtest
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario6 [doublespends]: "
                "-checkmempool=0"
                "-broadcastdelay=0, "
                "-txnpropagationfreq (default), "
                "-txnvalidationasynchrunfreq (default)"  # txn validator config
                "-numstdtxvalidationthreads=6 "          # txn validator thread pool config
                "-numnonstdtxvalidationthreads=2 ",      # txn validator thread pool config
                # Node's configuration
                ['-checkmempool=0',
                 '-broadcastdelay=0',
                 '-txnpropagationfreq=250',
                 '-txnvalidationasynchrunfreq=100',
                 '-numstdtxvalidationthreads=6',
                 '-numnonstdtxvalidationthreads=2'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                30000,
                # A number of peers connected to the node
                4,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                920,
                # Shuffle input txns
                False
            ]
        ]
        # Execute test cases.
        for test_case in test_cases:
            runTestWithParams(test_case[0], test_case[1], test_case[2], test_case[3], test_case[4], test_case[5], test_case[6], test_case[7])

if __name__ == '__main__':
    TxnValidatorP2PTxnsTest().main()
