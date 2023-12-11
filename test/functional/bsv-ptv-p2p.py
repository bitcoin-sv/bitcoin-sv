#!/usr/bin/env python3
# Copyright (c) 2019-2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
The aim of this test is to verify correctness of Parallel Transaction Validation (PTV) functionality.
"""
#
# The test uses standard and non-standard transactions, including double spends.
# - a txn reject message is created when a double spend occurs but not for detected txn mempool conflicts
#
from test_framework.test_framework import ComparisonTestFramework
from test_framework.key import CECKey
from test_framework.script import CScript, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_TRUE, OP_CHECKSIG, OP_DROP, OP_ADD, OP_MUL
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.util import assert_equal, assert_greater_than, wait_until, wait_for_ptv_completion
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx, CTransaction, CTxIn, CTxOut, COutPoint
from test_framework.cdefs import DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS
import random
import time


class PTVP2PTest(ComparisonTestFramework):

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

        self.default_args = ['-debug', '-maxgenesisgracefulperiod=0', '-genesisactivationheight=%d' % self.genesisactivationheight]
        self.extra_args = [self.default_args] * self.num_nodes

    def run_test(self):
        self.test.run()

    def check_rejected(self, rejected_txs, should_be_rejected_tx_set):
        wait_until(lambda: {tx.data for tx in rejected_txs} == {o.sha256 for o in should_be_rejected_tx_set}, timeout=20)

    def check_mempool(self, rpc, should_be_in_mempool, timeout=20):
        wait_until(lambda: set(rpc.getrawmempool()) == {t.hash for t in should_be_in_mempool}, timeout=timeout)

    def check_mempool_with_subset(self, rpc, should_be_in_mempool, timeout=20):
        wait_until(lambda: {t.hash for t in should_be_in_mempool}.issubset(set(rpc.getrawmempool())), timeout=timeout)

    def check_intersec_with_mempool(self, rpc, txs_set):
        return set(rpc.getrawmempool()).intersection(t.hash for t in txs_set)

    def get_front_slice(self, spends, num):
        txs_slice = spends[0:num]
        del spends[0:num]
        return txs_slice

    # Sign a transaction, using the key we know about.
    # This signs input 0 in tx, which is assumed to be spending output n in spend_tx
    def sign_tx(self, tx, spend_tx, n):
        scriptPubKey = bytearray(spend_tx.vout[n].scriptPubKey)
        sighash = SignatureHashForkId(
            spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript(
            [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    # A helper function to generate new txs spending all outpoints from prev_txs set.
    def generate_transactons(self, prev_txs, unlocking_script, locking_script, num_of_ds_txs=0, fee=2000000, factor=10):
        gen_txs = []
        ds_txs = []
        for prev_tx in prev_txs:
            for n, vout in enumerate(prev_tx.vout):
                tx = CTransaction()
                out_val = vout.nValue - fee
                tx.vout.extend((CTxOut(out_val, locking_script),) * factor)
                tx.vin.append(CTxIn(COutPoint(prev_tx.sha256, n), unlocking_script, 0xffffffff))
                # Use the first unspent txn as a common input for all double spend transactions.
                if num_of_ds_txs and len(ds_txs) < num_of_ds_txs-1 and len(gen_txs):
                    tx.vin.append(CTxIn(COutPoint(prev_txs[0].sha256, 0), unlocking_script, 0xffffffff))
                    tx.calc_sha256()
                    ds_txs.append(tx)
                    continue
                tx.calc_sha256()
                gen_txs.append(tx)
        # To simplify further checks, move the first unspent txn to the ds_txs set.
        if num_of_ds_txs:
            ds_txs.append(gen_txs[0])
            del gen_txs[0]
        if len(ds_txs) != num_of_ds_txs:
            raise Exception('Cannot create required number of double spend txs.')
        return gen_txs, ds_txs

    # Generate transactions in order so the first transaction's output will be an input for the second transaction.
    def get_chained_txs(self, spend, num_of_txs, unlocking_script, locking_script, money_to_spend, factor):
        txns = []
        for _ in range(0, num_of_txs):
            if factor == 1:
                money_to_spend = money_to_spend - 1000
            # Create a new transaction.
            tx = create_transaction(spend.tx, spend.n, unlocking_script, money_to_spend, locking_script)
            # Extend the number of outputs to the required size.
            tx.vout.extend(tx.vout * (factor-1))
            # Sign txn.
            self.sign_tx(tx, spend.tx, spend.n)
            tx.rehash()
            txns.append(tx)
            # Use the first outpoint to spend in the second iteration.
            spend = PreviousSpendableOutput(tx, 0)

        return txns

    # Create a required number of chains with equal length.
    # - each tx is configured to have factor outpoints with the same locking_script.
    def get_txchains_n(self, num_of_chains, chain_length, spend, unlocking_script, locking_script, money_to_spend, factor):
        if num_of_chains > len(spend):
            raise Exception('Insufficient number of spendable outputs.')
        txchains = []
        for x in range(0, num_of_chains):
            txchains += self.get_chained_txs(spend[x], chain_length, unlocking_script, locking_script, money_to_spend, factor)

        return txchains

    # A helper function to create and send a set of tx chains.
    def generate_and_send_txchains_n(self, conn, num_of_chains, chain_length, spend, locking_script, money_to_spend=5000000000, factor=10, timeout=60):
        # Create and send txs. In this case there will be num_txs_to_create txs of chain length equal 1.
        txchains = self.get_txchains_n(num_of_chains, chain_length, spend, CScript(), locking_script, money_to_spend, factor)
        for tx in range(len(txchains)):
            conn.send_message(msg_tx(txchains[tx]))

        return txchains

    #
    # Pre-defined testing scenarios.
    #

    # This scenario is being used to generate and send a set of standard txs in test cases.
    def run_scenario1(self, conn, spend, num_txs_to_create, chain_length, locking_script, money_to_spend=2000000, factor=10, timeout=60):
        return self.generate_and_send_txchains_n(conn, num_txs_to_create, chain_length, spend, locking_script, money_to_spend, factor, timeout)

    # This scenario is being used to generate and send a set of non-standard txs in test cases.
    # - there will be num_txs_to_create txs of chain length equal 1.
    # - from a single spend 2499 txs can be created (due to value of the funding tx and value assigned to outpoints: 5000000000/2000000 = 2500)
    #   - The exact number of 2500 txs could be created by including '-limitfreerelay=1000' param in the node's config.
    #   - The value 2000000 meets requirements of sufficient fee per txn size (used in the test).
    def run_scenario2(self, conn, spend, num_txs_to_create, locking_script, num_ds_to_create=0, additional_txs=[], shuffle_txs=False, send_txs=True, money_to_spend=2000000, timeout=60):
        # A handler to catch reject messages.
        rejected_txs = []

        def on_reject(conn, msg):
            rejected_txs.append(msg)
            # A double spend reject message is the expected one to occur.
            assert_equal(msg.reason, b'txn-double-spend-detected')
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
        nonstd_txs, ds_txs = self.generate_transactons(txchains, CScript([OP_TRUE]), locking_script, num_ds_to_create)
        all_txs = nonstd_txs + ds_txs + additional_txs
        # Shuffle txs if it is required
        if shuffle_txs:
            random.shuffle(all_txs)
        # Send txs if it is required
        if send_txs:
            for tx in all_txs:
                conn.send_message(msg_tx(tx))
        # Return ds set if was requested.
        if len(ds_txs):
            return nonstd_txs+additional_txs, ds_txs, rejected_txs

        return nonstd_txs+additional_txs, rejected_txs

    # This scenario is being used to generate and send multiple subsets of non-standard txs in test cases.
    # - scenario2 is used to prepare the required size of the set
    # - each subset is created from a different funding txn
    #   - as a result, there is no intersection between subsets
    def run_scenario3(self, conn, spend, num_txs_to_create, locking_script, num_ds_to_create=0, shuffle_txs=False, money_to_spend=2000000, timeout=60):
        all_nonstd_txs = []
        all_ds_txs = []
        # Create the set of required txs.
        for tx in spend:
            nonstd_txs, ds_txs, rejected_txs = self.run_scenario2(conn, [tx], num_txs_to_create, locking_script, num_ds_to_create, [], shuffle_txs, False, money_to_spend, timeout)
            all_nonstd_txs += nonstd_txs
            all_ds_txs += ds_txs
        all_txs = all_nonstd_txs + all_ds_txs
        # Shuffle txs if it is required
        if shuffle_txs:
            random.shuffle(all_txs)
        # Send txs
        for tx in all_txs:
            conn.send_message(msg_tx(tx))
        # Return ds set if was required to create.
        if len(all_ds_txs):
            return all_nonstd_txs, all_ds_txs, rejected_txs

        return all_nonstd_txs, rejected_txs

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
        # - 5000 standard txs used (100 txn chains, each of length 50)
        # - 1 peer connected to node0
        #
        # The number of txs used in the test case.
        tc1_txchains_num=100
        tc1_tx_chain_length=50
        # Select funding transactions to use:
        # - tc1_txchains_num funding transactions are needed in this test case.
        spend_txs = self.get_front_slice(out, tc1_txchains_num)
        args = ['-checkmempool=0',
                '-persistmempool=0',
                '-limitancestorcount=50',
                '-txnvalidationasynchrunfreq=100',
                '-numstdtxvalidationthreads=6',
                '-numnonstdtxvalidationthreads=2']
        with self.run_node_with_connections('TC1: {} std txn chains used, each of length {}.'.format(tc1_txchains_num, tc1_tx_chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            std_txs = self.run_scenario1(conn, spend_txs, tc1_txchains_num, tc1_tx_chain_length, self.locking_script_1, 5000000000, 1)
            wait_for_ptv_completion(conn, tc1_txchains_num*tc1_tx_chain_length)
            # Check if required transactions are accepted by the mempool.
            self.check_mempool(conn.rpc, std_txs, timeout=30)
            assert_equal(conn.rpc.getmempoolinfo()['size'], tc1_txchains_num*tc1_tx_chain_length)

        #
        # Test Case 2 (TC2).
        #
        # - 2400 non-standard txs (with a simple locking script) used
        # - 1 peer connected to node0
        #
        # The number of txs used in the test case.
        tc2_txs_num=2400
        # Select funding transactions to use:
        # - one funding transaction is needed in this test case.
        spend_txs = self.get_front_slice(out, 1)
        args = ['-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('TC2: {} non-std txs used.'.format(tc2_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            nonstd_txs, rejected_txs = self.run_scenario2(conn, spend_txs, tc2_txs_num, self.locking_script_2)
            wait_for_ptv_completion(conn, tc2_txs_num)
            # Check if required transactions are accepted by the mempool.
            self.check_mempool(conn.rpc, nonstd_txs, timeout=30)
            assert_equal(len(rejected_txs), 0)
            assert_equal(conn.rpc.getmempoolinfo()['size'], tc2_txs_num)

        #
        # Test Case 3 (TC3).
        #
        # - 2400 valid non-standard txs (with a simple locking script) used
        #   - 100 double spend txs used
        # - 1 peer connected to node0
        # From the double spends set only 1 txn is accepted by the mempool.
        #
        # The number of txs used in the test case.
        tc3_txs_num=2400
        ds_txs_num=100
        # Select funding transactions to use:
        # - one funding transaction is needed in this test case.
        spend_txs = self.get_front_slice(out, 1)
        args = ['-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('TC3: {} non-std txs ({} double spends) used.'.format(tc3_txs_num, ds_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            nonstd_txs, ds_txs, _ = self.run_scenario2(conn, spend_txs, tc3_txs_num, self.locking_script_2, ds_txs_num)
            wait_for_ptv_completion(conn, len(nonstd_txs)+1)
            # All txs from the nonstd_txs result set should be accepted
            self.check_mempool_with_subset(conn.rpc, nonstd_txs, timeout=30)
            # There is one more transaction in the mempool, which is a random txn from the ds_txs set
            assert_equal(conn.rpc.getmempoolinfo()['size'], len(nonstd_txs)+1)
            # Only one txn is allowed to be in the mempool from the given ds set.
            assert_equal(len(self.check_intersec_with_mempool(conn.rpc, ds_txs)), 1)

        #
        # Test Case 4 (TC4).
        #
        # - 10 standard txs used (as additional input set)
        # - 2400 non-standard (with a simple locking script) txs used
        #   - 100 double spend txs used
        # - 1 peer connected to node0
        # All input txs are randomly suffled before sending.
        #
        # The number of txs used in the test case.
        tc4_1_txs_num=10
        tc4_2_txs_num=2400
        ds_txs_num=100
        # Select funding transactions to use:
        # - tc4_1_txs_num+1 funding transactions are needed in this test case.
        spend_txs = self.get_front_slice(out, tc4_1_txs_num)
        spend_txs2 = self.get_front_slice(out, 1)
        args = ['-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('TC4: {} std, {} nonstd txs ({} double spends) used (shuffled set).'.format(tc4_1_txs_num, tc4_2_txs_num, ds_txs_num),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            # Create some additional std txs to use.
            std_txs = self.get_txchains_n(tc4_1_txs_num, 1, spend_txs, CScript(), self.locking_script_1, 2000000, 10)
            # Create and send generated txs.
            std_and_nonstd_txs, ds_txs, _ = self.run_scenario2(conn, spend_txs2, tc4_2_txs_num, self.locking_script_2, ds_txs_num, std_txs, shuffle_txs=True)
            wait_for_ptv_completion(conn, len(std_and_nonstd_txs)+1)
            # All txs from the std_and_nonstd_txs result set should be accepted
            self.check_mempool_with_subset(conn.rpc, std_and_nonstd_txs, timeout=30)
            # There is one more transaction in the mempool. It is a random txn from the ds_txs set
            assert_equal(conn.rpc.getmempoolinfo()['size'], len(std_and_nonstd_txs)+1)
            # Only one txn is allowed to be accepted by the mempool, from the given double spends txn set.
            assert_equal(len(self.check_intersec_with_mempool(conn.rpc, ds_txs)), 1)

        #
        # Test Case 5 (TC5).
        #
        # - 24K=10x2400 non-standard txs (with a simple locking script) used
        #   - 1K=10x100 double spend txs used
        # - 1 peer connected to node0
        # From each double spend set only 1 txn is accepted by the mempool.
        # - Valid non-standard txs are sent first, then double spend txs (this approach maximises a ratio of 'txn-double-spend-detected' reject msgs)
        #
        # The number of txs used in a single subset.
        tc5_txs_num=2400
        ds_txs_num=100
        # The number of subsets used in the test case.
        tc5_num_of_subsets=10
        # Select funding transactions to use:
        # - tc5_num_of_subsets funding transaction are needed in this test case.
        spend_txs = self.get_front_slice(out, tc5_num_of_subsets)
        args = ['-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('TC5: {} non-std txs ({} double spends) used.'.format(tc5_txs_num*tc5_num_of_subsets, ds_txs_num*tc5_num_of_subsets),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            nonstd_txs, ds_txs, rejected_txs = self.run_scenario3(conn, spend_txs, tc5_txs_num, self.locking_script_2, ds_txs_num)
            wait_for_ptv_completion(conn, len(nonstd_txs)+tc5_num_of_subsets, check_interval=0.5)
            # All txs from the nonstd_txs result set should be accepted
            self.check_mempool_with_subset(conn.rpc, nonstd_txs, timeout=60)
            # There are tc5_num_of_subsets more transaction in the mempool (random txns from the ds_txs set)
            assert_equal(conn.rpc.getmempoolinfo()['size'], len(nonstd_txs)+tc5_num_of_subsets)
            # Only tc5_num_of_subsets txns are allowed to be in the mempool from the given ds set.
            assert_equal(len(self.check_intersec_with_mempool(conn.rpc, ds_txs)), tc5_num_of_subsets)

        #
        # Test Case 6 (TC6).
        #
        # - 24K=10x2400 non-standard txs (with a simple locking script) used
        #   - 1K=10x100 double spend txs used
        # - 1 peer connected to node0
        # From each double spends set only 1 txn is accepted by the mempool.
        # All input txs are randomly suffled before sending.
        # - the txs set is shuffeled first so it significantly decreases 'txn-double-spend-detected' reject msgs comparing to TC5
        # - in this case 'txn-mempool-conflict' reject reason will mostly occur
        #
        # The number of txs used in a single subset.
        tc6_txs_num=2400
        ds_txs_num=100
        # The number of subsets used in the test case.
        tc6_num_of_subsets=10
        # Select funding transactions to use:
        # - tc6_num_of_subsets funding transaction are needed in this test case.
        spend_txs = self.get_front_slice(out, tc6_num_of_subsets)
        args = ['-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('TC6: {} non-std txs ({} double spends) used (shuffled set).'.format(tc6_txs_num*tc6_num_of_subsets, ds_txs_num*tc6_num_of_subsets),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            nonstd_txs, ds_txs, rejected_txs = self.run_scenario3(conn, spend_txs, tc6_txs_num, self.locking_script_2, ds_txs_num, shuffle_txs=True)
            wait_for_ptv_completion(conn, len(nonstd_txs)+tc6_num_of_subsets, check_interval=0.5)
            # All txs from the nonstd_txs result set should be accepted
            self.check_mempool_with_subset(conn.rpc, nonstd_txs, timeout=60)
            # There are tc6_num_of_subsets more transaction in the mempool (random txns from the ds_txs set)
            assert_equal(conn.rpc.getmempoolinfo()['size'], len(nonstd_txs)+tc6_num_of_subsets)
            # Only tc6_num_of_subsets txns are allowed to be in the mempool from the given ds set.
            assert_equal(len(self.check_intersec_with_mempool(conn.rpc, ds_txs)), tc6_num_of_subsets)


if __name__ == '__main__':
    PTVP2PTest().main()
