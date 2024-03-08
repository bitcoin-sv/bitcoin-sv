#!/usr/bin/env python3
# Copyright (c) 2019-2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
The aim of this test is to verify/check behaviour of Prioritised Validation Queues (PVQ).
"""

# Further details:
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
# 6. Tx reject message is created, when all the following conditions are met:
#    a) a low priority txn detected (with or without validation timeout)
#       or a high priority txn detected but only if validation timeout has not occurred
#    b) a non-internal reject code was returned from txn validation.

from test_framework.test_framework import ComparisonTestFramework
from test_framework.key import CECKey
from test_framework.script import CScript, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_TRUE, OP_CHECKSIG, OP_DROP, OP_ADD, OP_MUL
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.util import assert_equal, wait_until, wait_for_ptv_completion
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx, CTransaction, CTxIn, CTxOut, COutPoint
import random


class PVQTimeoutTest(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 600
        # The coinbase key used.
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        # Locking scripts used in the test.
        self.locking_script_1 = CScript([self.coinbase_pubkey, OP_CHECKSIG])
        self.locking_script_2 = CScript([1, 1, OP_ADD, OP_DROP])
        self.locking_script_3 = CScript([bytearray([42] * 250000), bytearray([42] * 200 * 1000), OP_MUL, OP_DROP])

        self.default_args = ['-debug', '-maxgenesisgracefulperiod=0', '-genesisactivationheight=%d' % self.genesisactivationheight]
        self.extra_args = [self.default_args] * self.num_nodes

    def run_test(self):
        self.test.run()

    def check_rejected(self, rejected_txs, should_be_rejected_tx_set):
        wait_until(lambda: {tx.data for tx in rejected_txs} == {o.sha256 for o in should_be_rejected_tx_set}, timeout=20)

    def check_mempool(self, rpc, should_be_in_mempool, timeout=20):
        wait_until(lambda: set(rpc.getrawmempool()) == {t.hash for t in should_be_in_mempool}, timeout=timeout)

    # Sign a transaction, using the key we know about.
    # This signs input 0 in tx, which is assumed to be spending output n in spend_tx
    def sign_tx(self, tx, spend_tx, n):
        scriptPubKey = bytearray(spend_tx.vout[n].scriptPubKey)
        sighash = SignatureHashForkId(
            spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript(
            [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    # A helper function to generate new txs spending all outpoints from prev_txs set.
    def generate_transactons(self, prev_txs, unlocking_script, locking_script, fee=2000000, factor=10):
        generated_txs = []
        for prev_tx in prev_txs:
            for n, vout in enumerate(prev_tx.vout):
                tx = CTransaction()
                out_val = vout.nValue - fee
                tx.vout.extend((CTxOut(out_val, locking_script),) * factor)
                tx.vin.append(CTxIn(COutPoint(prev_tx.sha256, n), unlocking_script, 0xffffffff))
                tx.calc_sha256()
                generated_txs.append(tx)

        return generated_txs

    # Generate transactions in order so the first transaction's output will be an input for the second transaction.
    def get_chained_txs(self, spend, num_of_txs, unlocking_script, locking_script, money_to_spend, vout_size):
        txns = []
        for _ in range(0, num_of_txs):
            # Create a new transaction.
            tx = create_transaction(spend.tx, spend.n, unlocking_script, money_to_spend, locking_script)
            # Extend the number of outputs to the required vout_size size.
            tx.vout.extend(tx.vout * (vout_size-1))
            # Sign txn.
            self.sign_tx(tx, spend.tx, spend.n)
            tx.rehash()
            txns.append(tx)
            # Use the first outpoint to spend in the second iteration.
            spend = PreviousSpendableOutput(tx, 0)

        return txns

    # Create a required number of chains with equal length.
    # - each tx is configured to have vout_size outpoints with the same locking_script.
    def get_txchains_n(self, num_of_chains, chain_length, spend, unlocking_script, locking_script, money_to_spend, vout_size):
        if num_of_chains > len(spend):
            raise Exception('Insufficient number of spendable outputs.')
        txchains = []
        for x in range(0, num_of_chains):
            txchains += self.get_chained_txs(spend[x], chain_length, unlocking_script, locking_script, money_to_spend, vout_size)

        return txchains

    # A helper function to create and send a set of tx chains.
    def generate_and_send_txchains_n(self, conn, num_of_chains, chain_length, spend, locking_script, money_to_spend=2000000, vout_size=10, timeout=60):
        # Create and send txs. In this case there will be num_txs_to_create txs of chain length equal 1.
        txchains = self.get_txchains_n(num_of_chains, chain_length, spend, CScript(), locking_script, money_to_spend, vout_size)
        for tx in range(len(txchains)):
            conn.send_message(msg_tx(txchains[tx]))
        # Check if the validation queues are empty.
        wait_for_ptv_completion(conn, num_of_chains*chain_length, timeout=timeout)

        return txchains

    #
    # Pre-defined testing scenarios.
    #

    # This scenario is being used to generate and send a set of standard txs in test cases.
    # - there will be num_txs_to_create txs of chain length equal 1.
    def run_scenario1(self, conn, spend, num_txs_to_create, locking_script, money_to_spend=2000000, vout_size=10, timeout=60):
        return self.generate_and_send_txchains_n(conn, num_txs_to_create, 1, spend, locking_script, money_to_spend, vout_size, timeout)

    # This scenario is being used to generate and send a set of non-standard txs in test cases.
    # - there will be num_txs_to_create txs of chain length equal 1.
    def run_scenario2(self, conn, spend, num_txs_to_create, locking_script, additional_txs=[], shuffle_txs=False, money_to_spend=2000000, timeout=60):
        # A handler to catch any reject messages.
        # - it is expected to get only 'too-long-validation-time' reject msgs.
        rejected_txs = []

        def on_reject(conn, msg):
            assert_equal(msg.reason, b'too-long-validation-time')
            rejected_txs.append(msg)
        conn.cb.on_reject = on_reject

        # Create and send tx chains with non-std outputs.
        # - one tx with vout_size=num_txs_to_create outpoints will be created
        txchains = self.generate_and_send_txchains_n(conn, 1, 1, spend, locking_script, money_to_spend, num_txs_to_create, timeout)

        # Check if required transactions are accepted by the mempool.
        self.check_mempool(conn.rpc, txchains, timeout)

        # Create a new block
        # - having an empty mempool (before submitting non-std txs) will simplify further checks.
        conn.rpc.generate(1)

        # Create and send transactions spending non-std outputs.
        nonstd_txs = self.generate_transactons(txchains, CScript([OP_TRUE]), locking_script)
        all_txs = nonstd_txs + additional_txs
        if shuffle_txs:
            random.shuffle(all_txs)
        for tx in all_txs:
            conn.send_message(msg_tx(tx))
        # Check if the validation queues are empty.
        conn.rpc.waitforptvcompletion()

        return nonstd_txs+additional_txs, rejected_txs

    def get_tests(self):
        # Shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        # Create a new block
        block(0, coinbase_pubkey=self.coinbase_pubkey)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        # Also, move block height on beyond Genesis activation.
        test = TestInstance(sync_every_block=False)
        for i in range(600):
            block(5000 + i, coinbase_pubkey=self.coinbase_pubkey)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # Collect spendable outputs now to avoid cluttering the code later on.
        out = []
        for i in range(200):
            out.append(self.chain.get_spendable_output())

        self.stop_node(0)

        #
        # Test Case 1 (TC1).
        #
        # - 10 standard txs used
        # - 1 peer connected to node0
        # All txs emplaced initially in the standard validation queue are processed and accepted by the mempool.
        # - None txn is rejected with a reason 'too-long-validation-time' (not moved into the non-std queue).
        #
        # The number of txs used in the test case.
        tc1_txs_num=10
        # Select funding transactions to use:
        # - tc1_txs_num funding transactions are needed in this test case.
        spend_txs = out[0:tc1_txs_num]
        args = ['-checkmempool=0', '-persistmempool=0',
                '-maxstdtxvalidationduration=500', # increasing max validation time ensures that timeout doesn't occur for standard txns, even on slower machines and on debug build
                '-maxnonstdtxnsperthreadratio=0'] # setting it to zero ensures that non-standard txs won't be processed (if there are any queued).
        with self.run_node_with_connections('TC1: {} txs detected as std and then accepted.'.format(tc1_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            std_txs = self.run_scenario1(conn, spend_txs, tc1_txs_num, self.locking_script_1)
            # Check if required transactions are accepted by the mempool.
            self.check_mempool(conn.rpc, std_txs, timeout=30)
            assert_equal(conn.rpc.getmempoolinfo()['size'], tc1_txs_num)

        #
        # Test Case 2 (TC2).
        #
        # - 10 non-standard txs (with a simple locking script) used.
        # - 1 peer connected to node0.
        # The test case creates rejected txns with a reason 'too-long-validation-time' for all txs initially emplaced into the standard queue.
        # - those rejects are not taken into account to create reject messages (see explanation - point 6)
        # All txns are then forwarded to the non-standard validation queue where the validation timeout is longer (sufficient).
        #
        # The number of txs used in the test case.
        tc2_txs_num=10
        # Select funding transactions to use:
        # - one funding transaction is needed in this test case.
        spend_txs = out[tc1_txs_num:tc1_txs_num+1]
        args = ['-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('TC2: {} txs with small bignums detected as non-std txs and then finally accepted.'.format(tc2_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            nonstd_txs, rejected_txs = self.run_scenario2(conn, spend_txs, tc2_txs_num, self.locking_script_2)
            wait_for_ptv_completion(conn, len(nonstd_txs))
            # No transactions should be rejected
            assert_equal(len(rejected_txs), 0)
            # Check if required transactions are accepted by the mempool.
            self.check_mempool(conn.rpc, nonstd_txs, timeout=30)
            assert_equal(conn.rpc.getmempoolinfo()['size'], tc2_txs_num)

        #
        # Test Case 3 (TC3).
        #
        # - 10 non-standard txs (with a complex locking script) used.
        # - 1 peer connected to node0
        # The test case creates rejected txns with a reason 'too-long-validation-time' for all txs initially emplaced into the standard queue.
        # - those rejects are not taken into account to create reject messages (see explanation - point 6)
        # All txns are then forwarded to the non-standard validation queue where the validation timeout is longer (sufficient).
        #
        # The number of txs used in the test case.
        tc3_txs_num=10
        # Select funding transactions to use:
        # - one funding transaction is needed in this test case.
        spend_txs = out[tc1_txs_num+1:tc1_txs_num+2]
        args = ['-checkmempool=0', '-persistmempool=0',
                '-maxnonstdtxvalidationduration=100000', # On slow/busy machine txn validation times have to be high
                '-maxtxnvalidatorasynctasksrunduration=100001', # This needs to mehigher then maxnonstdtxvalidationduration
                '-maxscriptsizepolicy=0', '-maxscriptnumlengthpolicy=250000']
        with self.run_node_with_connections('TC3: {} txs with large bignums detected as non-std txs and then finally accepted.'.format(tc3_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            nonstd_txs, rejected_txs = self.run_scenario2(conn, spend_txs, tc3_txs_num, self.locking_script_3)
            wait_for_ptv_completion(conn, len(nonstd_txs))
            # No transactions should be rejected
            assert_equal(len(rejected_txs), 0)
            # Check if required transactions are accepted by the mempool.
            self.check_mempool(conn.rpc, nonstd_txs, timeout=30)
            assert_equal(conn.rpc.getmempoolinfo()['size'], tc3_txs_num)

        #
        # Test Case 4 (TC4).
        #
        # - 10 non-standard txs (with a complex locking script) used.
        # - 1 peer connected to node0
        # The test case creates rejected txns with a reason 'too-long-validation-time' for all txs initially emplaced into the standard queue.
        # - those rejects are not taken into account to create reject messages (see explanation - point 6)
        # All txns are then forwarded to the non-standard validation queue.
        # - due to insufficient timeout config all txs are rejected again with 'too-long-validation-time' reject reason.
        # - reject messages are created for each and every txn.
        #
        # The number of txs used in the test case.
        tc4_txs_num=10
        # Select funding transactions to use:
        # - one funding transaction is needed in this test case.
        spend_txs = out[tc1_txs_num+2:tc1_txs_num+3]
        args = ['-checkmempool=0', '-persistmempool=0',
                '-maxscriptsizepolicy=0', '-maxscriptnumlengthpolicy=250000']
        with self.run_node_with_connections('TC4: {} txs with large bignums detected as non-std txs and then finally rejected.'.format(tc4_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            nonstd_txs, rejected_txs = self.run_scenario2(conn, spend_txs, tc4_txs_num, self.locking_script_3)
            wait_for_ptv_completion(conn, 0)
            # Check rejected transactions.
            self.check_rejected(rejected_txs, nonstd_txs)
            assert_equal(len(rejected_txs), tc4_txs_num)
            # The mempool should be empty at this stage.
            assert_equal(conn.rpc.getmempoolinfo()['size'], 0)

        #
        # Test Case 5 (TC5).
        #
        # - 100 standard txs used.
        # - 10 non-standard (with a simple locking script) txs used.
        # - 1 peer connected to node0.
        # This test case is a combination of TC1 & TC2
        # - the set of std and non-std txs is shuffled before sending it to the node.
        #
        # The number of txs used in the test case.
        tc5_1_txs_num=100
        tc5_2_txs_num=10
        # Select funding transactions to use:
        # - tc5_1_txs_num+1 funding transactions are needed in this test case.
        spend_txs = out[tc1_txs_num+3:tc1_txs_num+3+tc5_1_txs_num]
        spend_txs2 = out[tc1_txs_num+3+tc5_1_txs_num:tc1_txs_num+4+tc5_1_txs_num]
        args = ['-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('TC5: The total of {} std and nonstd txs processed and accepted.'.format(tc5_1_txs_num+tc5_2_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            std_txs = self.get_txchains_n(tc5_1_txs_num, 1, spend_txs, CScript(), self.locking_script_1, 2000000, 10)
            std_and_nonstd_txs, rejected_txs = self.run_scenario2(conn, spend_txs2, tc5_2_txs_num, self.locking_script_2, std_txs, shuffle_txs=True)
            wait_for_ptv_completion(conn, len(std_and_nonstd_txs))
            # Check if required transactions are accepted by the mempool.
            self.check_mempool(conn.rpc, std_and_nonstd_txs, timeout=30)
            assert_equal(conn.rpc.getmempoolinfo()['size'], tc5_1_txs_num+tc5_2_txs_num)


if __name__ == '__main__':
    PVQTimeoutTest().main()
