#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
The aim of this test is to group an additional RPC test cases dependant on the PTV's interface configuration
- test a set of duplicates submitted through sendrawtransaction (a single txn submit via rpc)
- test if a newly generated block is a valid block
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.key import CECKey
from test_framework.script import CScript, OP_TRUE, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.util import assert_equal, assert_raises_rpc_error, wait_until, wait_for_ptv_completion
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx, ToHex


class PTVRPCTests(ComparisonTestFramework):

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

    # Test processing of duplicates submitted through rpc interface:
    # (a) the node receives the set of transactions via p2p interface and holds them in the PTV queues
    # (b) the sendrawtransaction rpc interface is used to submit and process each duplicate
    def run_scenario1(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30, pausedp2p=False):
        # Create tx chains.
        txchains = self.get_txchains_n(num_of_chains, chain_length, spend)
        # Send txns, one by one, through p2p interface.
        for tx in range(len(txchains)):
            conn.send_message(msg_tx(txchains[tx]))
        # Check if there is an expected number of transactions in the validation queues
        # - this scenario relies on ptv delayed processing
        # - ptv is required to be paused
        wait_until(lambda: conn.rpc.getblockchainactivity()["transactions"] == num_of_chains * chain_length, timeout=timeout)
        # The mempool must be empty.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # The orphan pool must be empty.
        assert_equal(conn.rpc.getorphaninfo()["size"], 0)
        # Process each transaction resubmitted through rpc interface - even though it's present in the PTV queues.
        for tx in range(len(txchains)):
            conn.rpc.sendrawtransaction(ToHex(txchains[tx]), allowhighfees, dontcheckfee)
        # At this stage there should be 'num_of_chains*chain_length' txns in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(txchains))
        if pausedp2p:
            # Check that paused P2P txs are still queued.
            assert_equal(conn.rpc.getblockchainactivity()["transactions"], num_of_chains * chain_length)

        return txchains

    # Test processing of duplicates submitted through rpc interface:
    # (a) the node receives the set of transactions via p2p interface, processes and detects them as orphans
    # (b) the sendrawtransaction rpc interface is used to submit and process each duplicate
    def run_scenario1_1(self, conn, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create tx chain.
        txchain = self.get_txchains_n(1, chain_length, spend)
        # Send chained transactions - one by one - through p2p interface (except the parent!).
        for tx in txchain[1:]:
            conn.send_message(msg_tx(tx))
        # The orphan pool must contain 'chain_length-1' orphan transactions.
        wait_until(lambda: conn.rpc.getorphaninfo()["size"] == chain_length-1, timeout=timeout)
        assert_equal(conn.rpc.getblockchainactivity()["transactions"], 0)
        # The mempool must be empty.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # Process each transaction resubmitted through rpc interface - even though it's present in the PTV queues.
        for tx in range(len(txchain)):
            conn.rpc.sendrawtransaction(ToHex(txchain[tx]), allowhighfees, dontcheckfee)
        # At this stage there should be 'chain_length' txns in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(txchain))
        # The p2p orphan pool must be empty.
        assert_equal(conn.rpc.getorphaninfo()["size"], 0)

        return txchain

    # Test processing of duplicates submitted through rpc interface:
    # (a) the node receives the set of transactions via p2p interface, processes and detects them as orphans
    # (b) the sendrawtransaction rpc interface is used to submit and process each duplicated orphan
    # (c) the parent tx from the chain is not submitted to the node
    def run_scenario1_2(self, conn, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create tx chain.
        txchain = self.get_txchains_n(1, chain_length, spend)
        # Send chained transactions - one by one - through p2p interface (except the parent!).
        for tx in txchain[1:]:
            conn.send_message(msg_tx(tx))
        # The orphan pool must contain 'chain_length-1' orphan transactions.
        wait_until(lambda: conn.rpc.getorphaninfo()["size"] == chain_length-1, timeout=timeout)
        assert_equal(conn.rpc.getblockchainactivity()["transactions"], 0)
        # The mempool must be empty.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # Process each transaction resubmitted through rpc interface - even though it's present in the PTV queues.
        # The parent tx from the chain is skipped.
        for tx in txchain[1:]:
            assert_raises_rpc_error(
                -25, "Missing inputs", conn.rpc.sendrawtransaction, ToHex(tx), allowhighfees, dontcheckfee)
        # At this stage the mempool must be empty.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # The p2p orphan pool must contain 'chain_length-1' transactions.
        assert_equal(conn.rpc.getorphaninfo()["size"], chain_length-1)

        return txchain

    # An extension to the scenario1.
    # - submit txns through p2p interface
    # - resubmit duplicates via rpc interface
    # - create a new block
    # - use invalidateblock to re-org back
    # - create a new block
    # - check if txns are present in the new block
    def run_scenario2(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=60, pausedp2p=False):
        # Create tx chains.
        txchains = self.run_scenario1(conn, num_of_chains, chain_length, spend, allowhighfees, dontcheckfee, timeout, pausedp2p)
        wait_for_ptv_completion(conn, len(txchains), timeout=timeout)
        # Check if txchains txns are in the mempool.
        self.check_mempool(conn.rpc, set(txchains), timeout=60)
        # Check if there is only num_of_chains * chain_length txns in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(txchains))
        # Generate a single block.
        mined_block1 = conn.rpc.generate(1)
        # Mempool should be empty, all txns in the block.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # Use invalidateblock to re-org back; all transactions should
        # end up unconfirmed and back in the mempool.
        conn.rpc.invalidateblock(mined_block1[0])
        # There should be exactly num_of_chains * chain_length txns in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(txchains))
        self.check_mempool(conn.rpc, set(txchains))
        # Generate another block, they should all get mined.
        mined_block2 = conn.rpc.generate(1)
        # Mempool should be empty, all txns confirmed.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # Check if txchains txns are included in the block.
        mined_block2_details = conn.rpc.getblock(mined_block2[0])
        assert_equal(mined_block2_details['num_tx'], len(txchains) + 1) # +1 for coinbase txn.
        assert_equal(len(set(mined_block2_details['tx']).intersection(t.hash for t in txchains)), len(txchains))

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

        # Scenario 1 (TS1).
        # This test case checks if resubmitted transactions (through synchronous sendrawtransaction interface) are processed.
        # We don't want to reject duplicates at the early stage of processing (before asynchronous txn validation is executed).
        # - 1K txs used
        # - 1K txns are sent first through the p2p interface (and not processed as ptv is paused)
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        # Node's config
        args = ['-txnvalidationasynchrunfreq=10000',
                '-maxstdtxnsperthreadratio=0', # Do not take any std txs for processing (from the ptv queues).
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS1: {} chains of length {}. Test duplicates resubmitted via rpc.'.format(num_of_chains, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, pausedp2p=True)
        # dontcheckfee=True
        with self.run_node_with_connections('TS1: {} chains of length {}. Test duplicates resubmitted via rpc (dontcheckfee=True).'.format(num_of_chains, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, dontcheckfee=True, pausedp2p=True)

        # Scenario 1_1 (TS1_1).
        # It's an extension to TS1. The RPC interface is used to resubmit and process duplicates (the entire tx chain).
        # - 100 chained txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        #
        # Test case config
        chain_length = 100
        # Node's config
        args = ['-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS1_1: {} chain of length {}. Test duplicates resubmitted via rpc.'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1_1(conn, chain_length, out)
        # dontcheckfee=True
        with self.run_node_with_connections('TS1_1: {} chain of length {}. Test duplicates resubmitted via rpc (dontcheckfee=True).'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1_1(conn, chain_length, out, dontcheckfee=True)

        # Scenario 1_2 (TS1_2).
        # It's an extension to TS1. The RPC interface is used to resubmit and process orphans (tests 'Missing inputs' error code).
        # - 100 chained txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        #
        # Test case config
        chain_length = 100
        # Node's config
        args = ['-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS1_2: {} chain of length {}. Test orphans resubmitted via rpc.'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1_2(conn, chain_length, out)
        # dontcheckfee=True
        with self.run_node_with_connections('TS1_2: {} chain of length {}. Test orphans resubmitted via rpc.'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1_2(conn, chain_length, out, dontcheckfee=True)

        # Scenario 2 (TS2).
        # It's an extension to TS1. Resubmit duplicates, then create a new block and check if it is a valid block.
        # - 100 txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 10
        # Node's config
        args = ['-txnvalidationasynchrunfreq=2000',
                '-blockcandidatevaliditytest=1', # on regtest it's enabled by default but for clarity let's add it explicitly.
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS2: {} chains of length {}. Test duplicates and generate a new block.'.format(num_of_chains, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario2(conn, num_of_chains, chain_length, out)


if __name__ == '__main__':
    PTVRPCTests().main()
