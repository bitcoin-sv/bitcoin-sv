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
                                "dontcheckfee": true|false,
                                "listunconfirmedancestors" : true|false
                            },
                            {
                                "hex": "hexstring2",
                                "allowhighfees": true|false,
                                "dontcheckfee": true|false,
                                "listunconfirmedancestors" : true|false
                            },
                            {
                                "hex": "hexstring3",
                                "allowhighfees": true|false,
                                "dontcheckfee": true|false,
                                "listunconfirmedancestors" : true|false
                            },
                            ...
                        ]
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.key import CECKey
from test_framework.script import CScript, OP_TRUE, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.blocktools import create_coinbase, create_block
from test_framework.util import assert_equal, assert_greater_than_or_equal, assert_raises_rpc_error, wait_until
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx, msg_block, ToHex
import random
import itertools


class RPCSendRawTransactions(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 600
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.wrong_key = CECKey()
        self.wrong_key.set_secretbytes(b"horseradish")
        self.locking_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])
        self.default_args = ['-debug', '-maxgenesisgracefulperiod=0', '-genesisactivationheight=%d' % self.genesisactivationheight]
        self.extra_args = [self.default_args] * self.num_nodes
        self.private_key = CECKey()
        self.private_key.set_secretbytes(b"fatstacks")
        self.public_key = self.private_key.get_pubkey()

    def run_test(self):
        self.test.run()

    # Sign a transaction, using the key we know about.
    # This signs input 0 in tx, which is assumed to be spending output n in spend_tx
    def sign_tx(self, tx, spend_tx, n, *, key):
        scriptPubKey = bytearray(spend_tx.vout[n].scriptPubKey)
        sighash = SignatureHashForkId(
            spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript(
            [key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    def check_mempool(self, rpc, should_be_in_mempool, timeout=20):
        wait_until(lambda: set(rpc.getrawmempool()) == {t.hash for t in should_be_in_mempool}, timeout=timeout)

    # Generating transactions in order so first transaction's output will be an input for second transaction
    def get_chained_transactions(self, spend, num_of_transactions, *, money_to_spend=5000000000, bad_chain=False):
        ok = []
        bad = []
        orphan = []
        bad_transaction = num_of_transactions // 2 if bad_chain else num_of_transactions
        for i in range(0, num_of_transactions):
            money_to_spend = money_to_spend - 1000  # one satoshi to fee
            tx = create_transaction(spend.tx, spend.n, b"", money_to_spend, self.locking_script)
            self.sign_tx(tx, spend.tx, spend.n,
                         key = self.coinbase_key if i != bad_transaction else self.wrong_key)
            tx.rehash()
            txns = ok if i < bad_transaction else bad if i == bad_transaction else orphan
            txns.append(tx)
            spend = PreviousSpendableOutput(tx, 0)
        return ok, bad, orphan

    # Create a required number of chains with equal length.
    def get_txchains_n(self, num_of_chains, chain_length, spend, *, num_of_bad_chains):
        assert(0 <= num_of_bad_chains <= num_of_chains)
        if num_of_chains > len(spend):
            raise Exception('Insufficient number of spendable outputs.')
        txok = []
        txbad = []
        txorphan = []
        bad_chain_marker = ([True] * num_of_bad_chains +
                            [False] * (num_of_chains - num_of_bad_chains))
        random.shuffle(bad_chain_marker)
        for x, bad_chain in enumerate(bad_chain_marker):
            ok, bad, orphan = self.get_chained_transactions(spend[x], chain_length, bad_chain=bad_chain)
            txok.extend(ok)
            txbad.extend(bad)
            txorphan.extend(orphan)
        return txok, txbad, txorphan

    # Test an expected valid results, depending on node's configuration.
    def run_scenario1(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, listunconfirmedancestors=False, useRpcWithDefaults=False, shuffle_txs=False, timeout=30):
        # Create and send tx chains.
        txchains, bad, orphan = self.get_txchains_n(num_of_chains, chain_length, spend, num_of_bad_chains=0)
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
                rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee, 'listunconfirmedancestors': listunconfirmedancestors})
        # Submit bulk tranactions.
        result = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        if listunconfirmedancestors:
            assert_equal(len(result), 1)
            assert_equal(len(result['unconfirmed']), num_of_chains*chain_length)
            first_in_chain = 0
            expected_ancestors = []
            # A map of transactions and their known parents to be checked in 'vin'
            parentsMap = {}
            parentTxId = ""
            # All transactions and their unconfirmed ancestors are part of sendrawtransactions inputs
            # First transaction in each chain should not have any unconfirmed ancestors
            # Next transactions in the chain have increasing number of unconfirmed ancestors
            for tx in result['unconfirmed']:
                if len(tx['ancestors']) == 0:
                    first_in_chain += 1
                    # reset, since this is a new chain
                    expected_ancestors = []
                    parentsMap = {}
                    parentTxId = ""
                else:
                    # we expect to have increasing number of unconfirmed ancestors by each transaction in this chain
                    assert_equal(len(tx['ancestors']), len(expected_ancestors))
                    for ancestor in tx['ancestors']:
                        assert(ancestor['txid'] in expected_ancestors)
                        # each ancestor has 1 input
                        assert_equal(len(ancestor['vin']), 1)
                        # check input
                        if ancestor['txid'] in parentsMap:
                            assert_equal(ancestor['vin'][0]['txid'], parentsMap[ancestor['txid']])
                expected_ancestors.append(tx['txid'])
                if parentTxId:
                    parentsMap[tx['txid']] = parentTxId
            # Each chain should have one transaction (first in chain) without any unconfirmed ancestors
            assert_equal(first_in_chain, num_of_chains)
        else:
            # There should be no rejected transactions.
            assert_equal(len(result), 0)
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

        #
        # listunconfirmedancestors: Invalid value
        #
        assert_raises_rpc_error(
            -8, "listunconfirmedancestors: Invalid value", conn.rpc.sendrawtransactions, [{'hex': rawtx['hex'], 'listunconfirmedancestors': -1}])
        assert_raises_rpc_error(
            -8, "listunconfirmedancestors: Invalid value", conn.rpc.sendrawtransactions, [{'hex': rawtx['hex'], 'listunconfirmedancestors': 'dummy_value'}])

    # Submit transactions (via rpc interface) which are already known:
    # (a) received earlier through the p2p interface and not processed yet
    # (b) txchains resubmitted through the rpc interface
    def run_scenario3(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create and send txchains.
        txchains, bad, orphan = self.get_txchains_n(num_of_chains, chain_length, spend, num_of_bad_chains=0)
        # Prepare inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        for tx in range(len(txchains)):
            # Collect txn input data for bulk submit through rpc interface.
            rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            # Send a txn, one by one, through p2p interface.
            conn.send_message(msg_tx(txchains[tx]))
        # Check if there is an expected number of transactions enqueued in the validation queues for asynchronous processing.
        wait_until(lambda: conn.rpc.getblockchainactivity()["transactions"] == num_of_chains * chain_length, timeout=timeout)
        assert_equal(conn.rpc.getorphaninfo()["size"], 0)
        # Submit the batch through rpc interface.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        # All transactions must be in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(rpc_txs_bulk_input))

    # Submit transactions (via rpc interface) which are already known:
    # (a) received earlier through the p2p interface and detected as p2p orphans
    # (b) submit the txchain (without the parent tx) to check 'missing-inputs' reject reason
    def run_scenario3_1(self, conn, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create and send txchain.
        txchain, bad, orphan = self.get_txchains_n(1, chain_length, spend, num_of_bad_chains=0)
        # Prepare inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        # Skip the parent transaction from the chain.
        for tx in txchain[1:]:
            # Collect txn input data for bulk submit through rpc interface.
            rpc_txs_bulk_input.append({'hex': ToHex(tx), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            # Send a txn, one by one, through p2p interface.
            conn.send_message(msg_tx(tx))
        # Check if there is an expected number of transactions in the orphan p2p buffer.
        wait_until(lambda: conn.rpc.getorphaninfo()["size"] == chain_length-1, timeout=timeout)
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # Submits the txchain without the parent transaction.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        assert_equal(len(rejected_txns), 1)
        assert_equal(len(rejected_txns['invalid']), len(rpc_txs_bulk_input))
        for tx in rejected_txns["invalid"]:
            assert_equal(tx['reject_code'], 16)
            assert_equal(tx['reject_reason'], 'missing-inputs')
        # The mempool must be empty.
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # The p2p orphan buffer must contain 'chain_length-1' transactions.
        assert_equal(conn.rpc.getorphaninfo()["size"], chain_length-1)

    # Submit transactions (via rpc interface) which are already known:
    # (a) received earlier through the p2p interface and detected as p2p orphans
    # (b) submit the entire txchain (with the parent transaction) to check its acceptance
    def run_scenario3_2(self, conn, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create and send txchain.
        txchain, bad, orphan = self.get_txchains_n(1, chain_length, spend, num_of_bad_chains=0)
        # Prepare inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        # Skip the parent transaction from the chain.
        for tx in txchain[1:]:
            # Collect txn input data for bulk submit through rpc interface.
            rpc_txs_bulk_input.append({'hex': ToHex(tx), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            # Send a txn, one by one, through p2p interface.
            conn.send_message(msg_tx(tx))
        # Check if there is an expected number of transactions in the p2p orphan pool.
        wait_until(lambda: conn.rpc.getorphaninfo()["size"] == chain_length-1, timeout=timeout)
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)
        # Insert the parent tx at the front of the input array.
        rpc_txs_bulk_input.insert(0, {'hex': ToHex(txchain[0]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
        # Submit the batch through rpc interface.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        # All transactions must be in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(rpc_txs_bulk_input))
        # The p2p orphan pool must be empty.
        assert_equal(conn.rpc.getorphaninfo()["size"], 0)

    # Test duplicated input data set submitted through the rpc interface.
    # - input data are shuffled
    def run_scenario4(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create and send tx chains.
        txchains, bad, orphan = self.get_txchains_n(num_of_chains, chain_length, spend, num_of_bad_chains=0)
        # Prepare duplicated inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        for tx in range(len(txchains)):
            rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
        # Shuffle inputs.
        random.shuffle(rpc_txs_bulk_input)
        # Submit bulk input.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        # Check if there are rejected invalid transactions.
        assert_equal(len(rejected_txns), 1)
        assert_greater_than_or_equal(len(rejected_txns['invalid']), num_of_chains)
        # Check if required transactions are accepted by the mempool.
        self.check_mempool(conn.rpc, txchains, timeout)

    # test an attempt to submit bad transactions in a chain through the rpc interface
    def run_scenario5(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, reverseOrder=False, timeout=30):
        # Create and send tx chains.
        num_of_bad_chains = num_of_chains // 3
        ok, bad, orphan = self.get_txchains_n(num_of_chains, chain_length, spend, num_of_bad_chains=num_of_bad_chains)
        # Prepare inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        txs = ok + bad + orphan
        if reverseOrder:
            txs.reverse()
        for tx in txs:
            # Collect txn input data for bulk submit through rpc interface.
            rpc_txs_bulk_input.append({'hex': ToHex(tx), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
        # Submit a batch of txns through rpc interface.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)
        for k,v in rejected_txns.items():
            self.log.info("====== rejected_txns[%s] = %s", k, v)
        assert_equal(len(rejected_txns), 1)
        assert_equal(len(rejected_txns['invalid']), len(bad) + len(orphan))

        def reject_reason(x):
            return x['reject_reason']
        invalid = {k: list(v)
                   for k,v in itertools.groupby(sorted(rejected_txns['invalid'],
                                                       key=reject_reason),
                                                key=reject_reason)
                   }
        self.log.info("invalid: %s", invalid)
        missing_inputs = invalid.pop('missing-inputs')
        assert_equal({x["txid"] for x in missing_inputs}, {x.hash for x in orphan})
        reason, rejected = invalid.popitem()
        assert_equal(reason.startswith('mandatory-script-verify-flag-failed '), True)
        assert_equal({x["txid"] for x in rejected}, {x.hash for x in bad})
        # Valid transactions should be in the mempool.
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(ok))

    def make_block(self, txs, parent_hash, parent_height, parent_time):
        """ creates a block with given transactions"""
        block = create_block(int(parent_hash, 16),
                             coinbase=create_coinbase(pubkey=self.public_key, height=parent_height + 1),
                             nTime=parent_time + 1)
        block.vtx.extend(txs)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.calc_sha256()
        block.solve()

        return block

    # Test an attempt to submit transactions (via rpc interface) which are already mined
    def run_scenario6(self, conn, num_of_chains, chain_length, spend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create and send tx chains.
        txchains, bad, orphan = self.get_txchains_n(num_of_chains, chain_length, spend, num_of_bad_chains=0)
        to_mine = []
        # Prepare inputs for sendrawtransactions
        rpc_txs_bulk_input = []
        for tx in range(len(txchains)):
            # Collect txn input data for bulk submit through rpc interface.
            rpc_txs_bulk_input.append({'hex': ToHex(txchains[tx]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            if tx < len(txchains) // 2:
                # First half of txns will be mined in a block submitted through p2p interface.
                to_mine.append(txchains[tx])

        root_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
        root_hash = root_block_info["hash"]
        root_height = root_block_info["height"]
        root_time = root_block_info["time"]

        # create the block
        block = self.make_block(to_mine, root_hash, root_height, root_time)
        conn.send_message(msg_block(block))
        wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, check_interval=0.3)

        # Check if there is an expected number of transactions in the mempool
        assert_equal(conn.rpc.getmempoolinfo()['size'], 0)

        # Submit a batch of txns through rpc interface.
        rejected_txns = conn.rpc.sendrawtransactions(rpc_txs_bulk_input)

        # There should be to_mine rejected transactions.
        assert_equal(len(rejected_txns['invalid']), len(to_mine))
        # bitcoind knows about the outputs of the last already mined transaction
        assert_equal(len([tx for tx in rejected_txns['invalid']
                          if tx['reject_reason'] == 'txn-already-known']), 1)
        # bitcoind knows nothing about the previous already mined transactions so it considers them orphans
        assert_equal(len([tx for tx in rejected_txns['invalid']
                          if tx['reject_reason'] == 'missing-inputs']), len(to_mine) - 1)
        # No transactions that were already mined should be in the mempool. The rest should be
        assert_equal(conn.rpc.getmempoolinfo()['size'], len(txchains) - len(to_mine))

    # Submit a transaction (via rpc interface) which is already known
    def run_scenario7(self, conn, spend, rpcsend, allowhighfees=False, dontcheckfee=False, timeout=30):
        # Create the following txchain: tx1 (parent) -> tx2 (child).
        # (a) tx1 - parent - is at the index 0
        # (b) tx2 - child - is at the index 1
        txchain, _, _ = self.get_txchains_n(1, 2, spend, num_of_bad_chains=0)

        # Send tx2 (the child tx) to the node through p2p interface.
        assert_equal(conn.rpc.getorphaninfo()["size"], 0)
        conn.send_message(msg_tx(txchain[1]))
        wait_until(lambda: conn.rpc.getorphaninfo()["size"] == 1, timeout=timeout)

        # Get block's basic data.
        root_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
        root_hash = root_block_info["hash"]
        root_height = root_block_info["height"]
        root_time = root_block_info["time"]

        # The parent tx1 transaction will be mined in a block, then the block will be relayed to the node.
        to_mine = []
        to_mine.append(txchain[0])

        # create the block
        block = self.make_block(to_mine, root_hash, root_height, root_time)
        conn.send_message(msg_block(block))
        wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, check_interval=0.3)

        # Check if tx2 (the child tx) has not been removed by the BlockConnect operation.
        assert_equal(conn.rpc.getorphaninfo()["size"], 1)

        # Submit tx2 (the child tx) through rpc interface.
        if "sendrawtransactions" == rpcsend._service_name:
            # Prepare the input data.
            rpc_txs_bulk_input = []
            # Add tx2 to the input data request.
            rpc_txs_bulk_input.append({'hex': ToHex(txchain[1]), 'allowhighfees': allowhighfees, 'dontcheckfee': dontcheckfee})
            rejected_txns = rpcsend(rpc_txs_bulk_input)
            # Check if tx2 has not been rejected by the node.
            assert_equal(len(rejected_txns), 0)
        elif "sendrawtransaction" == rpcsend._service_name:
            assert_equal(rpcsend(ToHex(txchain[1]), allowhighfees, dontcheckfee), txchain[1].hash)
        else:
            raise Exception("Unsupported rpc method!")
        # tx2 should be the only transaction in the mempool.
        assert_equal(conn.rpc.getrawmempool(), [txchain[1].hash])

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
        # - listunconfirmedancestors=False (default)
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
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, useRpcWithDefaults=True, timeout=20)

        # Scenario 2 (TS2).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with default params.
        # - 1K txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        # - listunconfirmedancestors=False (default)
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
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, useRpcWithDefaults=True, shuffle_txs=True, timeout=20)

        # Scenario 3 (TS3).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with default params.
        # - 10K txs used
        # - allowhighfees=False (default)
        # - dontcheckfee=False (default)
        # - listunconfirmedancestors=False (default)
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
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, useRpcWithDefaults=True, timeout=30)

        # Scenario 4 (TS5).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with explicitly declared default params.
        # - 1K txs used
        # - allowhighfees=False (an explicit default value)
        # - dontcheckfee=False (an explicit default value)
        # - listunconfirmedancestors=False (an explicit default value)
        # - txn chains are in ordered sequence (no orphans should be detected during processing)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        allowhighfees=False
        dontcheckfee=False
        listunconfirmedancestors=False
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS4: {} chains of length {}. allowhighfees={}, dontcheckfee={}, listunconfirmedancestors{}.'.format(num_of_chains,
                                                                                                                                                 chain_length,
                                                                                                                                                 str(allowhighfees),
                                                                                                                                                 str(dontcheckfee),
                                                                                                                                                 str(listunconfirmedancestors)),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, allowhighfees, dontcheckfee, listunconfirmedancestors, timeout=20)

        # Scenario 5 (TS5).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, with non-default params.
        # - 1K txs used
        # - allowhighfees=True
        # - dontcheckfee=True
        # - listunconfirmedancestors=True
        # - txn chains are in ordered sequence (no orphans should be detected during processing)
        #
        # Test case config
        num_of_chains = 10
        chain_length = 100
        allowhighfees=True
        dontcheckfee=True
        listunconfirmedancestors=True
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS5: {} chains of length {}. allowhighfees={}, dontcheckfee={}, listunconfirmedancestors{}.'.format(num_of_chains,
                                                                                                                                                 chain_length,
                                                                                                                                                 str(allowhighfees),
                                                                                                                                                 str(dontcheckfee),
                                                                                                                                                 str(listunconfirmedancestors)),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario1(conn, num_of_chains, chain_length, out, allowhighfees, dontcheckfee, listunconfirmedancestors, timeout=20)

        # Scenario 6 (TS6).
        #
        # Checks if the synchronous rpc request takes priority over tx-duplicates enqueued for asynchronous validation.
        #
        # 1. The set of transactions is present in the PTV queues (waiting to be processed).
        # 2. The same set of transactions is resubmitted through the rpc interface.
        #    (a) the synchronous interface validates them even though they are scheduled to be processed asynchronously
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
        with self.run_node_with_connections('TS6: {} chains of length {}. Process duplicates received through rpc'.format(num_of_chains,
                                                                                                                          chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario3(conn, num_of_chains, chain_length, out, timeout=30)

        # Scenario 6_1 (TS6_1).
        #
        # Checks if the synchronous rpc request takes priority over tx-duplicates present in the p2p orphan pool.
        #
        # 1. The chain of orphaned transactions is present in the p2p orphan pool (chained txs without the parent tx).
        # 2. The same chain is resubmitted through the rpc interface.
        #    (a) the synchronous interface validates them even though they are already present in the p2p orphan pool
        #
        # Test case config
        chain_length = 10
        # Node's config
        args = ['-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS6_1: {} chains of length {}. Process duplicates received through rpc'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario3_1(conn, chain_length, out, timeout=30)
        # dontcheckfee=True
        with self.run_node_with_connections('TS6_1: {} chains of length {}. Process duplicates received through rpc (dontcheckfee=True)'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario3_1(conn, chain_length, out, dontcheckfee=True, timeout=30)

        # Scenario 6_2 (TS6_2).
        #
        # This test case proves that a chain of transactions can be fully processed by the synchronous rpc interface,
        # even if a part of the chain is already present in the node's p2p orphan pool.
        #
        # 1. The chain of orphaned transactions is present in the p2p orphan pool (chained txs without the parent tx).
        # 2. The same chain - and the parent tx - are resubmitted through the rpc interface.
        #    (a) the synchronous interface validates them even though the orphans are already present in the p2p orphan pool
        #    (b) duplicates are removed from the p2p orphan pool at the end of batch processing
        #
        # Test case config
        chain_length = 10
        # Node's config
        args = ['-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS6_2: {} chains of length {}. Process duplicates received through rpc'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario3_2(conn, chain_length, out, timeout=30)
        # dontcheckfee=True
        with self.run_node_with_connections('TS6_2: {} chains of length {}. Process duplicates received through rpc (dontcheckfee=True)'.format(1, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario3_2(conn, chain_length, out, dontcheckfee=True, timeout=30)

        #====================================================================
        # Invalid test cases and non-empty rejects
        # - test invalid data
        # - test rejected transactions
        # - test duplicates
        #====================================================================

        # Scenario 7 (TS7).
        #
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS7: Invalid conditions',
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario2(conn, timeout=20)

        # Scenario 8 (TS8).
        # This test case checks a bulk submit of duplicated txs, through rpc sendrawtransactions interface.
        # - 2K txs used (1K are duplicates)
        # - the rpc input data set is shuffled
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
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario4(conn, num_of_chains, chain_length, out, timeout=20)

        # Scenario 9 (TS9).
        #
        # This test case checks bulk submit of chains with bad transactions in
        # the middle of the chain, through the rpc sendrawtransactions
        # interface.
        #
        # Test case config
        num_of_chains = 10
        chain_length = 10
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS9: {} chains of length {}. Reject known transactions'.format(num_of_chains, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario5(conn, num_of_chains, chain_length, out, reverseOrder=False, timeout=30)

        # Scenario 10 (TS10).
        #
        # This test case checks bulk submit of chains with bad transactions in
        # the middle of the chain, through the rpc sendrawtransactions
        # interface.
        # Essentially the same as Scenario 9 but txns are submitted in reversed order.
        #
        # Test case config
        num_of_chains = 10
        chain_length = 10
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-maxorphantxsize=0',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS10: {} chains of length {}. Reject known transactions'.format(num_of_chains, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario5(conn, num_of_chains, chain_length, out, reverseOrder=True, timeout=30)

        # Scenario 11 (TS11).
        # This test case checks a bulk submit of txs, through rpc sendrawtransactions interface, where some submitted transactions are already mined.
        #
        # Test case config
        num_of_chains = 1
        chain_length = 10
        # Node's config
        args = ['-txnvalidationasynchrunfreq=100',
                '-limitancestorcount=100',
                '-checkmempool=0',
                '-persistmempool=0']
        with self.run_node_with_connections('TS11: {} chains of length {}. Pre-mined txs. Default params for rpc call.'.format(num_of_chains, chain_length),
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario6(conn, num_of_chains, chain_length, out, timeout=20)

        # Scenario 12 (TS12).
        #
        # This test case checks the following scenario:
        #
        # 1. Create txchain: tx1 (parent) -> tx2 (child)
        # 2. Submit tx2 child tx to the node through the p2p interface.
        #    (a) due to the missing parent the node detects tx2 as an orphan tx and puts it into the p2p orphan pool
        # 3. The node gets a new block from the network which contains tx1 parent transaction.
        #    (a) processes the new block and updates the tip
        #    (b) tx2 child tx remains in the node's p2p orphan pool.
        # 4. tx2 is resubmitted to the node through the rpc interface.
        #    (a) the node processes tx2 and adds it into the mempool
        #    (b) removes tx2 duplicate from the p2p orphan pool
        # 5. Check if tx2 is the only transaction in the mempool.
        #
        # The above scenario tests both sendrawtransaction(s) rpc interfaces (including 'dontcheckfee' flag).
        #
        # Test case config
        args = ['-checkmempool=0',
                '-persistmempool=0']
        out = out[1:] # skip the spent coin from the previous test case
        tc_desc = '{} chain of length {}. Child tx2 is detected as p2p orphan tx, then parent tx1 is received in the next block, then tx2 is resubmitted through rpc'.format(1, 2)
        with self.run_node_with_connections('TS12a: ' + tc_desc,
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario7(conn, out, conn.rpc.sendrawtransactions, timeout=20) # dontcheckfee=False
        # dontcheckfee=True
        out = out[1:] # skip the spent coin from the previous test case
        with self.run_node_with_connections('TS12b: ' + tc_desc,
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario7(conn, out, conn.rpc.sendrawtransactions, dontcheckfee=True, timeout=20)
        out = out[1:] # skip the spent coin from the previous test case
        with self.run_node_with_connections('TS12c: ' + tc_desc,
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario7(conn, out, conn.rpc.sendrawtransaction, timeout=20) # dontcheckfee=False
        # dontcheckfee=True
        out = out[1:] # skip the spent coin from the previous test case
        with self.run_node_with_connections('TS12d: ' + tc_desc,
                                            0,
                                            args + self.default_args,
                                            number_of_connections=1) as (conn,):
            # Run test case.
            self.run_scenario7(conn, out, conn.rpc.sendrawtransaction, dontcheckfee=True, timeout=20)


if __name__ == '__main__':
    RPCSendRawTransactions().main()
