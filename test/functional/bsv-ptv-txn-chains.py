#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test reducing false-positive orphans, during ptv, by detecting a continuous transaction chain.
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.key import CECKey
from test_framework.script import CScript, OP_TRUE, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.util import assert_equal, wait_until, wait_for_ptv_completion
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx
import multiprocessing


class PTVTxnChains(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 600
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.locking_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])
        self.extra_args = [['-debug',
                            '-maxorphantxsize=10MB',
                            '-genesisactivationheight=%d' % self.genesisactivationheight]] * self.num_nodes

    def run_test(self):
        self.test.run()

    # Sign a transaction, using the key we know about.
    # This signs input 0 in tx, which is assumed to be spending output n in spend_tx
    def sign_tx(self, tx, spend_tx, n):
        scriptPubKey = bytearray(spend_tx.vout[n].scriptPubKey)
        sighash = SignatureHashForkId(
            spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript(
            [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    def check_mempool(self, rpc, should_be_in_mempool, timeout=20):
        wait_until(lambda: set(rpc.getrawmempool()) == {t.hash for t in should_be_in_mempool}, timeout=timeout)

    # Generating transactions in order so first transaction's output will be an input for second transaction
    def get_chained_transactions(self, spend, num_of_transactions, money_to_spend=5000000000):
        txns = []
        for _ in range(0, num_of_transactions):
            money_to_spend = money_to_spend - 1000  # one satoshi to fee
            tx = create_transaction(spend.tx, spend.n, b"", money_to_spend, self.locking_script)
            self.sign_tx(tx, spend.tx, spend.n)
            tx.rehash()
            txns.append(tx)
            spend = PreviousSpendableOutput(tx, 0)
        return txns

    # Create a required number of chains with equal length.
    def get_txchains_n(self, num_of_chains, chain_length, spend):
        if num_of_chains > len(spend):
            raise Exception('Insufficient number of spendable outputs.')
        txchains = []
        for x in range(0, num_of_chains):
            txchains += self.get_chained_transactions(spend[x], chain_length)
        return txchains

    def run_scenario1(self, conn, num_of_chains, chain_length, spend, timeout):
        # Create and send tx chains.
        txchains = self.get_txchains_n(num_of_chains, chain_length, spend)
        for tx in range(len(txchains)):
            conn.send_message(msg_tx(txchains[tx]))
        # Check if the validation queues are empty.
        wait_for_ptv_completion(conn, num_of_chains*chain_length, timeout=timeout)
        # Check if required transactions are accepted by the mempool.
        self.check_mempool(conn.rpc, txchains, timeout)

    def get_tests(self):
        rejected_txs = []

        def on_reject(conn, msg):
            rejected_txs.append(msg)
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

        num_of_threads = multiprocessing.cpu_count()

        # Scenario 1.
        # This test case shows that false-positive orphans are not created while processing a set of chains, where chainlength=10.
        # Each thread from the validaiton thread pool should have an assigned chain of txns to process.
        args = ['-maxorphantxsize=0', '-txnvalidationasynchrunfreq=100', '-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections('Scenario 1: {} chains of length 10. Storing orphans is disabled.'.format(num_of_threads),
                                            0,
                                            args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_threads, 10, out, timeout=20)

        # Scenario 2.
        # This test case shows that false-positive orphans are not created while processing a set of chains, where chainlength=20.
        # Each thread from the validaiton thread pool should have an assigned chain of txns to process.
        args = ['-maxorphantxsize=0', '-txnvalidationasynchrunfreq=0',
                '-limitancestorcount=20', '-checkmempool=0', '-persistmempool=0'
                '-maxstdtxvalidationduration=100']
        with self.run_node_with_connections('Scenario 2: {} chains of length 20. Storing orphans is disabled.'.format(num_of_threads),
                                            0,
                                            args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_threads, 20, out, timeout=30)

        # Scenario 3.
        # This scenario will cause 'too-long-validation-time' reject reason to happen - during ptv processing.
        # If a given task has got a chain of 50 txns to process and 10th txn is rejected with 'too-long-validation-time' rejection reason, then
        # all remaining txns from the chain are detected as false-positive orphans.
        # Due to a runtime environment it is not possible to estimate the number of such rejects.
        args = ['-maxorphantxsize=10', '-txnvalidationasynchrunfreq=0',
                '-limitancestorcount=50', '-checkmempool=0', '-persistmempool=0']
        with self.run_node_with_connections("Scenario 3: 100 chains of length 50. Storing orphans is enabled.",
                                            0,
                                            args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, 100, 50, out, timeout=60)


if __name__ == '__main__':
    PTVTxnChains().main()
