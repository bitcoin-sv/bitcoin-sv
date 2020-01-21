#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, SignatureHashForkId, hash160
from test_framework.script import OP_HASH160, OP_TRUE, OP_RETURN, OP_0, OP_1, OP_NUM2BIN, OP_DUP, OP_2DUP, OP_2DUP, OP_2DUP, OP_3DUP, OP_3DUP, OP_15, OP_CHECKMULTISIG, OP_EQUAL
from test_framework.script import SIGHASH_ANYONECANPAY, SIGHASH_FORKID, SIGHASH_NONE
from test_framework.blocktools import create_block, create_coinbase
from test_framework.key import CECKey
from enum import Enum
import datetime
import contextlib
import random

# This functional test was created to verify/check a behaviour of Prioritised Validation Queues (PVQ) depending on node's configuration.
#
# The Validation Thread Pool is defined by a number of ‘high’ and ‘low’ priority threads. By default (if not explicitly configured by config params),
# in a non trivial cases - HCT > 4 - the split is 75% for ‘high' priority threads and 25% for 'low’ priority threads.
# If there are no transactions in one of the queues, then all threads ('high' and 'low' priority threads)
# are assigned to process tasks from the other (non empty) queue.
#
# The following description clarifies how PVQ behaves in conjunction with designated timeouts.
# Long-term solution is going to be compliant with the points 1 & 2 (see Long-term section).
# For the time being, Short-term solution needs to take place which puts all received p2p txns into the 'high' priority queue.
# If the timeout occurs for the given txn, then it is being forwarded into the 'low' priority queue.
# This apporach avoids an expensive checks on txn's inputs by the networking thread.

# Short-term:
# 1/2. Any incoming p2p txn (standard or nonstandard) goes into the 'high' priority queue. If the timeout occurs,
#    the txn goes to the 'low' priority queue.
#   - maximumum txn validation duration for a 'high' priority task can be configured by '-maxstdtxvalidationduration' config param.
#   - maximumum txn validation duration for a 'low' priority task can be configured by '-maxnonstdtxvalidationduration' config param.

# Long-term:
# 1. Standard txns are handled by 'high' priority validation threads
#   - maximumum txn validation duration for a 'high' priority task can be configured by '-maxstdtxvalidationduration' config param.
# 2. Nonstandard txns are handled by 'low' priority validation threads
#   - maximumum txn validation duration for a 'low' priority task can be configured by '-maxnonstdtxvalidationduration' config param.

# 3. If a standard transaction (from the high-priority queue) takes too much time (it exceeds the timeout),
#    then it is forwarded to the low-priority queue (to be reprocessed later).
# 4. node0 is a testing node which receives txns via p2p.
# 5. Txns are propageted by callbacks to the node0.
# 6. Test cases without double sped issue
# 7. Test cases with double spend money
# 8. Tx reject message is created, when all the following conditions are met:
#    a) a low priority txn detected (with or without validation timeout)
#       or a high priority txn detected but only if validation timeout has not occurred
#    b) a non-internal reject code was returned from txn validation.


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

class PVQTimeoutTest(BitcoinTestFramework):

    # Create given number of spend money transcations without submitting them
    def make_transactions(self, txtype, num_txns, stxn_vin_size, create_double_spends=False):
        key = CECKey()
        key.set_secretbytes(b"horsebattery")
        key.set_compressed(True)
        # Each coin being spent will always result in at least 14 expensive ECDSA checks.
        # 0x7f03 33 OP_NUM2BIN creates a valid non-zero compressed pubkey.
        redeem_script=CScript([OP_1, key.get_pubkey(), 0x7f03, 33, OP_NUM2BIN,
            OP_DUP, OP_2DUP, OP_2DUP, OP_2DUP, OP_3DUP, OP_3DUP, OP_15, OP_CHECKMULTISIG])

        # Calculate how many found txns are needed to create a required spend money txns (num_txns)
        # - a fund txns are of type 1 - N (N=vouts_size_per_fund_txn)
        # - a spend money txns are of type M-1 (M inputs & 1 output)
        def estimate_fund_txns_number(num_txns, vouts_size_per_fund_txn):
            fund_txns_num = 1
            if num_txns >= vouts_size_per_fund_txn:
                if num_txns % vouts_size_per_fund_txn == 0:
                    fund_txns_num = num_txns // vouts_size_per_fund_txn
                else:
                    fund_txns_num = num_txns // vouts_size_per_fund_txn + 1
            return fund_txns_num * vouts_size_per_fund_txn

        # Create funding transactions that will provide funds for other transcations
        def make_fund_txn(node, out_value, num_vout_txns):
            # Create fund txn
            ftx = CTransaction()
            for i in range(num_vout_txns):
                ftx.vout.append(CTxOut(out_value, CScript([OP_HASH160, hash160(redeem_script), OP_EQUAL])))
            # fund the transcation:
            ftxHex = node.fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex'] 
            ftxHex = node.signrawtransaction(ftxHex)['hex']         
            ftx = FromHex(CTransaction(), ftxHex)        
            ftx.rehash()
            return ftx, ftxHex

        # Create a spend txn
        def make_spend_txn(txtype, fund_txn_hash, fund_txn_num_vouts, out_value):
            # Create txn
            spend_tx = CTransaction()
            for idx in range(fund_txn_num_vouts):
                spend_tx.vin.append(CTxIn(COutPoint(fund_txn_hash, idx), b''))
                sighash = SignatureHashForkId(redeem_script, spend_tx, idx, SIGHASH_ANYONECANPAY | SIGHASH_FORKID | SIGHASH_NONE, out_value)
                sig = key.sign(sighash) + bytes(bytearray([SIGHASH_ANYONECANPAY | SIGHASH_FORKID | SIGHASH_NONE]))
                spend_tx.vin[idx].scriptSig=CScript([OP_0, sig, redeem_script])
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
        out_value = 2000
        # Number of outputs in each fund txn
        fund_txn_num_vouts = stxn_vin_size 
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
        # Create spend transactions.
        #
        txtype_to_create = txtype
        spend_txs = []
        for i in range(len(fund_txns)):
            # If standard and non-standard txns are required then create equal (in size) sets.
            if TxType.std_and_nonstd == txtype:
                if i % 2:
                    txtype_to_create = TxType.standard
                else:
                    txtype_to_create = TxType.nonstandard
            # Create a spend money txn with fund_txn_num_vouts number of inputs.
            spend_tx = make_spend_txn(txtype_to_create, fund_txns[i].sha256, fund_txn_num_vouts, out_value)
            # Create double spend txns if required
            if create_double_spends and len(spend_txs) < num_txns // 2:
                # The first half of the array are double spend txns
                spend_tx.vin.append(CTxIn(COutPoint(fund_txns[len(fund_txns) - i - 1].sha256, 0), b''))
                sighash = SignatureHashForkId(redeem_script, spend_tx, stxn_vin_size, SIGHASH_ANYONECANPAY | SIGHASH_FORKID | SIGHASH_NONE, out_value)
                sig = key.sign(sighash) + bytes(bytearray([SIGHASH_ANYONECANPAY | SIGHASH_FORKID | SIGHASH_NONE]))
                spend_tx.vin[stxn_vin_size].scriptSig=CScript([OP_0, sig, redeem_script])
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

        rejected_txs = []
        def on_reject(conn, msg):
            rejected_txs.append(msg)

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

            # Assign nodes's handler
            connections[0].cb.on_reject = on_reject

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

        def runTestWithParams(sync_node_type, description, args, txtype, number_of_txns, stxn_vin_size,
                expected_txns_num, rejected_txns_num, number_of_peers, timeout, shuffle_txns=False):
            # In case of double spends only a half of the set is valid.
            create_double_spends = (doublespends() == sync_node_type)
            if create_double_spends:
                if number_of_txns % 2:
                    raise Exception('Incorrect size given to create a set of double spend txns')
            # Start the node0
            self.start_node(0, args)
            # Create a required number of spend money txns.
            result_txns = self.make_transactions(txtype, number_of_txns, stxn_vin_size, create_double_spends)
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
                # Delay checks by a second
                time.sleep(1)
                # Check if the validation queues are empty
                wait_until(lambda: self.nodes[0].rpc.getblockchainactivity()["transactions"] == 0, timeout=timeout)
                # Wait until the condition is met
                wait_until(lambda: len(self.nodes[0].getrawmempool()) == expected_txns_num, timeout=timeout)
                # Check a number of rejected txns
                if create_double_spends:
                    # Due to the fact that we do not use rejection cache for witness transactions or witness-stripped transactions
                    # (for instance when return code is REJECT_CONFLICT, reason=txn-mempool-conflict)
                    # A number of invalid txns with this reason can not be constant due to mt execution.
                    # Because of that, a constant value can not be used.
                    assert(len(rejected_txs) >= rejected_txns_num)
                else:
                    assert(len(rejected_txs) == rejected_txns_num)
                # Assert to confirm that the above wait is successfull (additional check for clarity)
                assert_equal(len(self.nodes[0].getrawmempool()), expected_txns_num)
            # Erase any rejected messages.
            del rejected_txs[:]

        #
        # No double spends: A definition of test cases dependent on node0's configuration.
        #
        test_cases = [
            # P2P-Scenario1_a
            # - 10 txns used, 1 peer connected to node0
            # - standard txns used
            # The test case produces rejected txns with a reason 'too-long-validation-time' for standard ('high' priority txns).
            # All standard txns are then forwarded to the non-standard validation queue where the validation timeout is longer (sufficient).
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario1_a [nodoublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=10000 "
                "-maxtxnvalidatorasynctasksrunduration=11000 "
                "-checkmempool=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=10000',
                 '-maxtxnvalidatorasynctasksrunduration=11000',
                 '-checkmempool=0'],
                TxType.standard,
                # A number of spend money txns used in the test
                10,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                10,
                # A number of tx reject messages created during a test
                0,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240
            ],

            # P2P-Scenario1_b
            # - 10 txns used, 1 peer connected to node0
            # - standard txns used
            # All standard txns are processed without a timeout as validation duration is set to a high value
            # (non of them is forwarded to the non-standard queue).
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario1_b [nodoublespends]: "
                "-maxstdtxvalidationduration=10000 "
                "-maxnonstdtxvalidationduration=10001 "
                "-maxtxnvalidatorasynctasksrunduration=11000 "
                "-checkmempool=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=10000',
                 '-maxnonstdtxvalidationduration=10001',
                 '-maxtxnvalidatorasynctasksrunduration=11000',
                 '-checkmempool=0'],
                TxType.standard,
                # A number of spend money txns used in the test
                10,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                10,
                # A number of tx reject messages created during a test
                0,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240
            ],

            # P2P-Scenario2
            # - 10 txns used, 1 peer connected to node0
            # - standard txns used
            # The test case produces rejected txns with a reason 'too-long-validation-time' for standard ('high' priority txns).
            # All standard txns are then forwarded to the non-standard validation queue where the validation timeout is longer.
            # The timeout set for non-standard txns is not sufficient to process txns (forwarded from the standard queue).
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario2 [nodoublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=10 "
                "-checkmempool=0 "
                "-broadcastdelay=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=10',
                 '-checkmempool=0',
                 '-broadcastdelay=0'],
                TxType.standard,
                # A number of spend money txns used in the test
                10,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                0,
                # A number of tx reject messages created during a test (created by 'low' priority tasks)
                10,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                60
            ],

            # P2P-Scenario3_a
            # - 10 txns used, 1 peer connected to node0
            # - nonstandard txns used
            # All txns go to the non-standard queue directly.
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario3_a [nodoublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=10000 "
                "-maxtxnvalidatorasynctasksrunduration=11000 "
                "-checkmempool=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=10000',
                 '-maxtxnvalidatorasynctasksrunduration=11000',
                 '-checkmempool=0'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                10,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                10,
                # A number of tx reject messages created during a test
                0,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240
            ],

            # P2P-Scenario3_b
            # - 10 txns used, 1 peer connected to node0
            # - nonstandard txns used
            # All txns go to the non-standard queue directly.
            # The test case triggers async tasks cancellation and moves them to the next iteration.
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario3_b [nodoublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=500 "
                "-maxtxnvalidatorasynctasksrunduration=501 "
                "-checkmempool=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=500',
                 '-maxtxnvalidatorasynctasksrunduration=501',
                 '-checkmempool=0'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                30,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                30,
                # A number of tx reject messages created during a test
                0,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240
            ],

            # P2P-Scenario4
            # - 10 txns used, 1 peer connected to node0
            # - standard and nonstandard txns used
            # All txns go to the standard queue first.
            [
                # Sync node scenario
                nodoublespends(),
                # Test case description
                "P2P-Scenario4 [nodoublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=10000 "
                "-maxtxnvalidatorasynctasksrunduration=11000 "
                "-checkmempool=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=10000',
                 '-maxtxnvalidatorasynctasksrunduration=11000',
                 '-checkmempool=0'],
                TxType.std_and_nonstd,
                # A number of spend money txns used in the test
                10, # (5 are std txns and 5 are non-standard txns)
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                10,
                # A number of tx reject messages created during a test
                0,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240
            ]
        ]
        # Execute test cases.
        for test_case in test_cases:
            runTestWithParams(test_case[0], test_case[1], test_case[2], test_case[3], test_case[4],
                    test_case[5], test_case[6], test_case[7], test_case[8], test_case[9])

        #
        # Double spends: A definition of test cases dependent on node0's configuration.
        #
        test_cases = [
            # P2P-Scenario1
            # - 10 txns used, 1 peer connected to node0
            # - standard txns used
            # - double spends used
            # The test case produces rejected txns with a reason 'too-long-validation-time' for standard ('high' priority txns).
            # All standard txns are then forwarded to the non-standard validation queue where the validation timeout is longer (sufficient).
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario1 [doublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=10000 "
                "-maxtxnvalidatorasynctasksrunduration=11000 "
                "-checkmempool=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=10000',
                 '-maxtxnvalidatorasynctasksrunduration=11000',
                 '-checkmempool=0'],
                TxType.standard,
                # A number of spend money txns used in the test
                10,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                5,
                # A number of tx reject messages created during a test.
                # The total number of rejected msgs should be 15. However, due to mt execution some of them are detected as
                # 'txn-double-spend-detected' (added to the reject cache) but others as 'txn-mempool-conflict' (witness transactions
                # not added to the reject cache).
                0,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240,
                # Shuffle input txns
                True
            ],

            # P2P-Scenario2
            # - 10 txns used, 1 peer connected to node0
            # - standard txns used
            # - double spends used
            # The test case produces rejected txns with a reason 'too-long-validation-time' for standard ('high' priority txns).
            # All standard txns are then forwarded to the non-standard validation queue where the validation timeout is longer.
            # The timeout set for non-standard txns is not sufficient to process txns (forwarded from the standard queue).
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario2 [doublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=10 "
                "-checkmempool=0 "
                "-broadcastdelay=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=10',
                 '-checkmempool=0',
                 '-broadcastdelay=0'],
                TxType.standard,
                # A number of spend money txns used in the test
                10,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                0,
                # A number of tx reject messages created during a test.
                10,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                60,
                # Shuffle input txns
                True
            ],

            # P2P-Scenario3
            # - 10 txns used, 1 peer connected to node0
            # - nonstandard txns used
            # - double spends used
            # All txns go to the non-standard queue directly.
            [
                # Sync node scenario
                doublespends(),
                # Test case description
                "P2P-Scenario3 [doublespends]: "
                "-maxstdtxvalidationduration=5 "
                "-maxnonstdtxvalidationduration=10000 "
                "-maxtxnvalidatorasynctasksrunduration=11000 "
                "-checkmempool=0",
                # Node's configuration
                ['-maxstdtxvalidationduration=5',
                 '-maxnonstdtxvalidationduration=10000',
                 '-maxtxnvalidatorasynctasksrunduration=11000',
                 '-checkmempool=0'],
                TxType.nonstandard,
                # A number of spend money txns used in the test
                10,
                # A number of inputs in the spend money txn
                10,
                # A number of accepted txns
                5,
                # A number of tx reject messages created during a test.
                # The total number of rejected txns is 15. However, due to mt execution some of them are detected as
                # 'txn-double-spend-detected' (added to the reject cache) but others as 'txn-mempool-conflict' (witness transactions
                # not added to the reject cache).
                0,
                # A number of peers connected to the node0
                1,
                # A timeout for the test case (if a number of txns used is large then the timeout needs to be increased)
                240,
                # Shuffle input txns
                True
            ]
        ]
        # Execute test cases.
        for test_case in test_cases:
            runTestWithParams(test_case[0], test_case[1], test_case[2], test_case[3], test_case[4],
                    test_case[5], test_case[6], test_case[7], test_case[8], test_case[9], test_case[10])

if __name__ == '__main__':
    PVQTimeoutTest().main()
