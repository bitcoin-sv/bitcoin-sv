#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test a new rpc interface sendrawtransactions which allows a bulk submit of transactions
- an extension to the sendrawtransaction rpc call (an interface to submit a single txn)
1. It executes a batch of txs utilising the validation thread pool.
2. As an input the rpc call expects to get a json array of objects,
   provided in the following form:

    sendrawtransactions [
                            {
                                "hex": "hexstring1",
                                "allowhighfees": true|false,
                                "dontcheckfee": true|false
                            },
                            {
                                "hex": "hexstring2",
                                "allowhighfees": true|false,
                                "dontcheckfee": true|false
                            },
                            {
                                "hex": "hexstring3",
                                "allowhighfees": true|false,
                                "dontcheckfee": true|false
                            },
                            ...
                        ]
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.key import CECKey
from test_framework.script import CScript, OP_TRUE, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.util import assert_equal, assert_raises_rpc_error, wait_until
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx, ToHex
import random

class RPCSendRawTransactions(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 600
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.locking_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])
        self.default_args = ['-debug', '-maxgenesisgracefulperiod=0', '-genesisactivationheight=%d' % self.genesisactivationheight]
        self.extra_args = [self.default_args] * self.num_nodes

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

    # Test an expected valid results, depending on node's configuration.
    def run_scenario1(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, useRpcWithDefaults=False, shuffle_txs=False, timeout=30):
        # Create and send tx chains.
        txchains = self.get_txchains_n(num_of_chains, chain_length, spend)
        # Shuffle txs if it is required
        if shuffle_txs:
            random.shuffle(txchains)
        # Prepare inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        for tx in range(len(txchains)):
            # Collect txn input data for bulk submit through rpc interface.
            if useRpcWithDefaults:
                rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx])})
            else:
                rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
        # Submit bulk tranactions.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        # There should be no rejected transactions.
        assert_equal(len(rejected_txns), 0)
        # Check if required transactions are accepted by the mempool.
        self.check_mempool(conn.rpc, txchains, timeout)

    # Test an expected invalid results and invalid input data conditions.
    def run_scenario2(self, conn, timeout=30):
        #
        # sendrawtransactions with missing input #
        #
        inputs = [
            {'txid': "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout': 1}]
        # won't exists
        outputs = {conn.rpc.getnewaddress(): 4.998}
        rawtx = conn.rpc.createrawtransaction(inputs, outputs)
        rawtx = conn.rpc.signrawtransaction(rawtx)

        rejected_txns = conn.rpc.sendrawtransactions([{'hex': rawtx['hex']}])

        assert_equal(len(rejected_txns['invalid']), 1)
        # Reject invalid
        assert_equal(rejected_txns['invalid'][0]['reject_code'], 16)
        assert_equal(rejected_txns['invalid'][0]['reject_reason'], "missing-inputs")
        # No transactions should be in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)

        #
        # An empty json array of objects.
        #
        assert_raises_rpc_error(
            -8, "Invalid parameter: An empty json array of objects", conn.rpc.sendrawtransactions, [])

        #
        # An empty json object.
        #
        assert_raises_rpc_error(
            -8, "Invalid parameter: An empty json object", conn.rpc.sendrawtransactions, [{}])
        
        #
        # Missing the hex string of the raw transaction.
        #
        assert_raises_rpc_error(
            -8, "Invalid parameter: Missing the hex string of the raw transaction", conn.rpc.sendrawtransactions, [{'dummy_str': 'dummy_value'}])
        assert_raises_rpc_error(
            -8, "Invalid parameter: Missing the hex string of the raw transaction", conn.rpc.sendrawtransactions, [{'hex': -1}])

        #
        # TX decode failed.
        #
        assert_raises_rpc_error(
            -22, "TX decode failed", conn.rpc.sendrawtransactions, [{'hex': '050000000100000000a0ce6e35'}])

        #
        # allowhighfees: Invalid value
        #
        assert_raises_rpc_error(
            -8, "allowhighfees: Invalid value", conn.rpc.sendrawtransactions, [{'hex': rawtx['hex'], 'allowhighfees': -1}])
        assert_raises_rpc_error(
            -8, "allowhighfees: Invalid value", conn.rpc.sendrawtransactions, [{'hex': rawtx['hex'], 'allowhighfees': 'dummy_value'}])

        #
        # dontcheckfee: Invalid value
        #
        assert_raises_rpc_error(
            -8, "dontcheckfee: Invalid value", conn.rpc.sendrawtransactions, [{'hex': rawtx['hex'], 'dontcheckfee': -1}])
        assert_raises_rpc_error(
            -8, "dontcheckfee: Invalid value", conn.rpc.sendrawtransactions, [{'hex': rawtx['hex'], 'dontcheckfee': 'dummy_value'}])

    # Test an attempt to submit transactions (via rpc interface) which are already known
    #   - received earlier through the p2p interface and not processed yet
    def run_scenario3(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create and send tx chains.
        txchains = self.get_txchains_n(num_of_chains, chain_length, spend)
        # Prepare inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        for tx in range(len(txchains)):
            # Collect txn input data for bulk submit through rpc interface.
            rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            # Send a txn, one by one, through p2p interface.
            conn.send_message(msg_tx(txchains[tx]))
        # Check if there is an expected number of transactions in the validation queues
        # - this scenario relies on ptv delayed processing
        wait_until(lambda: conn.rpc.getblockchainactivity()["transactions"] == num_of_chains * chain_length, timeout=timeout)
        # Submit a batch of txns through rpc interface.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        # There should be num_of_chains * chain_length rejected transactions.
        # - there are num_of_chains*chain_length known transactions
        #   - due to the fact that all were received through the p2p interface
        #   - all are waiting in the ptv queues
        assert_equal(len(rejected_txns['known']), num_of_chains * chain_length)
        # No transactions should be in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)

    # Test duplicated input data set submitted through the rpc interface.
    # - input data are shuffled
    def run_scenario4(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create and send tx chains.
        txchains = self.get_txchains_n(num_of_chains, chain_length, spend)
        # Prepare duplicated inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        for tx in range(len(txchains)):
            rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
        # Shuffle inputs.
        random.shuffle(rpc_txs_bulk_input)
        # Submit bulk input.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        # There should be rejected known transactions.
        assert_equal(len(rejected_txns), 1)
        assert_equal(len(rejected_txns['known']), num_of_chains * chain_length)
        assert(set(rejected_txns['known']) == {t.hash for t in txchains})
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

        #====================================================================
        # Valid test cases.
        # - a bulk submit of txns through sendrawtransactions rpc interface
        # - all transactions are valid and accepted by the mempool
        #====================================================================

        # Scenario 1 (TS1).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with default params.
        # - 1K txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        # - txn chains are in ordered sequence (no orphans should be detected during processing)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS1: {} chains of length {}. Default params for rpc call.'.format(num_of_chains, chain_length),
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, useRpcWithDefaults=True, timeout=20)

        # Scenario 2 (TS2).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with default params.
        # - 1K txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        # - txn chains are shuffled (orphans should be detected during processing)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        # Node's config
        args = ['-txnvalidationasynchrunfreq=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS2: {} chains of length {}. Shuffled txs. Default params for rpc call.'.format(num_of_chains, chain_length),
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, useRpcWithDefaults=True, shuffle_txs=True, timeout=20)

        # Scenario 3 (TS3).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with default params.
        # - 10K txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        # - txn chains are in ordered sequence (no orphans should be detected during processing)
        #
        # Test case config
        num_of_chains = 100
        chain_length = 100
        # Node's config
        args = ['-txnvalidationasynchrunfreq=0',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS3: {} chains of length {}. Default params for rpc call.'.format(num_of_chains, chain_length),
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, useRpcWithDefaults=True, timeout=30)

        # Scenario 4 (TS5).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with explicitly declared default params.
        # - 1K txs used
        # - allowhighfees=False (an explicit default value)
        # - dontcheckfee=False (an explicit default value)
        # - txn chains are in ordered sequence (no orphans should be detected during processing)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        allowhighfees=False
        dontcheckfee=False
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS4: {} chains of length {}. allowhighfees={}, dontcheckfee={}.'.format(num_of_chains, chain_length, str(allowhighfees), str(dontcheckfee)),
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, allowhighfees, dontcheckfee, timeout=20)

        # Scenario 5 (TS5).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with non-default params.
        # - 1K txs used
        # - allowhighfees=True
        # - dontcheckfee=True
        # - txn chains are in ordered sequence (no orphans should be detected during processing)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        allowhighfees=True
        dontcheckfee=True
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS5: {} chains of length {}. allowhighfees={}, dontcheckfee={}.'.format(num_of_chains, chain_length, str(allowhighfees), str(dontcheckfee)),
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, allowhighfees, dontcheckfee, timeout=20)


        #====================================================================
        # Invalid test cases and non-empty rejects
        # - test invalid data
        # - test rejected transactions
        # - test duplicates
        #====================================================================

        # Scenario 6 (TS6).
        #
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS6: Invalid conditions',
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario2(conn, timeout=20)

        # Scenario 7 (TS7).
        #
        # Test case config
        num_of_chains = 10
        chain_length = 10
        # Node's config
        args = ['-txnvalidationasynchrunfreq=10000',
                '-maxstdtxnsperthreadratio=0', # Do not take any std txs for processing (from the ptv queues).
                '-maxnonstdtxnsperthreadratio=0', # Do not take any non-std txs for processing (from the ptv queues).
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS7: {} chains of length {}. Reject known transactions'.format(num_of_chains, chain_length),
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario3(conn, num_of_chains, chain_length, out, timeout=30)

        # Scenario 8 (TS8).
        # This test case checks a bulk submit of duplicated txs, through rpc sendrawtransactions interface.
        # - 2K txs used (1K are detected as duplicates - known transactions in the result set)
        # - rpc input data set is shuffled
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        # Node's config
        args = ['-txnvalidationasynchrunfreq=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS8: {} chains of length {}. Test duplicated inputs.'.format(num_of_chains, chain_length),
                0, args + self.default_args, number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario4(conn, num_of_chains, chain_length, out, timeout=20)

if __name__ == '__main__':
    RPCSendRawTransactions().main()
