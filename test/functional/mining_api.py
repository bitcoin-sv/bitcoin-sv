#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test mining RPCs

Spin up some nodes, feed in some transactions, use the mining API to
mine some blocks, verify all nodes accept the mined blocks.

Also tests rpc call getminingcandidate to correctly return number of transactions and
the candidate size
"""

from test_framework.blocktools import create_coinbase, merkle_root_from_merkle_proof, solve_bad, create_block_from_candidate
from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import CBlock, ToHex
from test_framework.util import connect_nodes_bi, create_confirmed_utxos, satoshi_round, assert_raises_rpc_error, assert_equal, wait_until, sync_blocks, sync_mempools
from decimal import Decimal
import math
import time


def get_any_unspent(listunspent, threshold_amount=0.0):
    if len(listunspent) == 0:
        raise AssertionError(
            'Could not find any utxo')
    for utx in listunspent:
        if utx['amount'] >= threshold_amount:
            return utx
    raise AssertionError(
        'Could not find any unspent with threshold={}'.format(threshold_amount))


# Split some UTXOs into some number of spendable outputs
def split_utxos(fee, node, count, utxos, nodes):
    # Split each UTXO into this many outputs
    split_into = max(2, math.ceil(count / len(utxos)))

    # Addresses we send them all to
    addrs = []
    for i in range(split_into):
        addrs.append(node.getnewaddress())

    # Calculate fee we need (based on assuming each outpoint consumes about 70 bytes)
    fee = satoshi_round(Decimal(max(fee, 70 * split_into * 0.00000001)))

    while count > 0:
        utxo = utxos.pop()
        inputs = []
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        outputs = {}
        send_value = utxo['amount'] - fee
        if send_value <= 0:
            raise Exception("UTXO value is less than fee")

        for i in range(split_into):
            addr = addrs[i]
            outputs[addr] = satoshi_round(send_value / split_into)
        count -= split_into

        raw_tx = node.createrawtransaction(inputs, outputs)
        signed_tx = node.signrawtransaction(raw_tx)["hex"]
        node.sendrawtransaction(signed_tx)

        # Mine all the generated txns into blocks
        while (node.getmempoolinfo()['size'] > 0):
            node.generate(1)

        # If running multiple nodes they have to be synced regularly to prevent simultaneous syncing of blocks and txs
        if nodes is not None:
            sync_blocks(nodes)

    utxos = node.listunspent()
    return utxos


# Feed some UTXOs into a nodes mempool
def fill_mempool(fee, node, utxos):
    addr = node.getnewaddress()
    num_sent = 0
    for utxo in utxos:
        inputs = []
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        outputs = {}
        send_value = utxo['amount'] - fee
        outputs[addr] = satoshi_round(send_value)

        raw_tx = node.createrawtransaction(inputs, outputs)
        signed_tx = node.signrawtransaction(raw_tx)["hex"]
        node.sendrawtransaction(signed_tx)

        num_sent += 1
        if num_sent % 10000 == 0:
            print("Num sent: {}".format(num_sent))


# Connect each node to each other node
def connect_nodes_mesh(nodes):
    for i in range(len(nodes)):
        for j in range(i + 1, len(nodes)):
            connect_nodes_bi(nodes, i, j)


# The main test class
class MiningTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = False
        self.extra_args = [['-whitelist=127.0.0.1']] * self.num_nodes

    def setup_network(self):
        super().setup_network()

        # Connect the nodes up
        connect_nodes_mesh(self.nodes)

    def test_api_errors(self, blockNode, otherNode):
        candidate = blockNode.getminingcandidate()

        block = CBlock()
        block.nVersion = candidate["version"]
        block.hashPrevBlock = int(candidate["prevhash"], 16)
        block.nTime = candidate["time"]
        block.nBits = int(candidate["nBits"], 16)
        block.nNonce = 0

        # Submit with wrong ID
        self.log.info("Submitting to wrong node with unknown ID")
        assert_raises_rpc_error(-22, "Block candidate ID not found",
                                otherNode.submitminingsolution,
                                {'id': candidate['id'], 'nonce': block.nNonce})

        # Omit nonce
        self.log.info("Submitting without nonce")
        assert_raises_rpc_error(-22, "nonce not found",
                                blockNode.submitminingsolution,
                                {'id': candidate['id']})

        # Bad coinbase
        self.log.info("Submitting with bad coinbase")
        assert_raises_rpc_error(-22, "coinbase decode failed",
                                blockNode.submitminingsolution,
                                {'id': candidate['id'],
                                 'nonce': block.nNonce,
                                 'coinbase': 'ALoadOfRubbish'})

        # Bad POW
        self.log.info("Submitting with bad POW")
        coinbase_tx = create_coinbase(height=int(candidate["height"]) + 1)
        coinbase_tx.rehash()
        block.vtx = [coinbase_tx]
        block.hashMerkleRoot = merkle_root_from_merkle_proof(coinbase_tx.sha256, candidate["merkleProof"])
        solve_bad(block)
        submitResult = blockNode.submitminingsolution({'id': candidate['id'],
                                                       'nonce': block.nNonce,
                                                       'coinbase': '{}'.format(ToHex(coinbase_tx))})
        assert submitResult == 'high-hash'

    def _send_transactions_to_node(self, node, num_trasactions):
        # Create UTXOs to build a bunch of transactions from
        self.relayfee = Decimal("250") / 100000000
        utxos = create_confirmed_utxos(self.relayfee, node, 100, nodes=self.nodes)
        self.sync_all()

        # Create a lot of transactions from the UTXOs
        newutxos = split_utxos(self.relayfee, node, num_trasactions, utxos, self.nodes)
        fill_mempool(self.relayfee, node, newutxos)

    def _create_and_submit_block(self, node, candidate, get_coinbase):
        # Do POW for mining candidate and submit solution
        block, coinbase_tx = create_block_from_candidate(candidate, get_coinbase)
        self.log.info("block hash before submit: " + str(block.hash))

        if (get_coinbase):
            self.log.info("Checking submission with provided coinbase")
            return node.submitminingsolution({'id': candidate['id'], 'nonce': block.nNonce})
        else:
            self.log.info("Checking submission with generated coinbase")
            return node.submitminingsolution({'id': candidate['id'],
                                              'nonce': block.nNonce,
                                              'coinbase': '{}'.format(ToHex(coinbase_tx))})

    def test_mine_block(self, txnNode, blockNode, get_coinbase):
        self.log.info("Setting up for submission...")

        self._send_transactions_to_node(txnNode, 1000)

        # Check candidate has expected fields
        candidate = blockNode.getminingcandidate(get_coinbase)
        assert 'id' in candidate
        assert 'prevhash' in candidate
        if(get_coinbase):
            assert 'coinbase' in candidate
        else:
            assert 'coinbase' not in candidate
        assert 'coinbaseValue' in candidate
        assert 'version' in candidate
        assert 'nBits' in candidate
        assert 'time' in candidate
        assert 'height' in candidate
        assert 'sizeWithoutCoinbase' in candidate
        assert 'num_tx' in candidate
        assert 'merkleProof' in candidate

        submitResult = self._create_and_submit_block(blockNode, candidate, get_coinbase)

        # submitResult is bool True for success, false if failure
        assert submitResult

    def test_mine_from_old_mining_candidate(self, blockNode, get_coinbase):

        sync_blocks(self.nodes)
        candidate = blockNode.getminingcandidate(get_coinbase)

        # Here we will check multiple block submitions because probability of accepting
        # a block with the random nonce is very high
        for i in range(1, 100):
            # half minute to future. Setting this too high will result in 'time-too-new' errors
            blockNode.setmocktime(candidate["time"] + 30)

            #we are getting new mining candidate to change nTime in the headder of the pblocktemplate
            candidate_new = blockNode.getminingcandidate(get_coinbase)

            assert (candidate["time"] < candidate_new["time"])

            submitResult = self._create_and_submit_block(blockNode, candidate, get_coinbase)
            if submitResult == "high-hash":
                assert False, "Submited blocks is rejected because invalid pow, the time has changed."

            candidate = candidate_new
            sync_blocks(self.nodes)

    def test_optional_validation(self):
        # Start 2 nodes, 1 with validation enabled the other disabled
        self.log.info("Restarting nodes for optional validation")
        self.stop_nodes()
        self.start_nodes([['-blockcandidatevaliditytest=1','-checkmempool=0', "-disablesafemode=1"], ['-blockcandidatevaliditytest=0','-checkmempool=0', "-disablesafemode=1"]])
        self.sync_all()
        connect_nodes_bi(self.nodes, 0, 1)

        self._send_transactions_to_node(self.nodes[0], 5000)
        self.sync_all()
        # Check both nodes have the same number of txns in their mempool
        assert self.nodes[0].getmempoolinfo()['size'] == self.nodes[1].getmempoolinfo()['size']

        # Time call to getminingcandidate with validation
        start_time = time.time()
        candidate = self.nodes[0].getminingcandidate(True)
        end_time = time.time()
        validation_time = end_time - start_time
        self.log.info("Time to get candidate with validation: {}".format(validation_time))

        # Time call to getminingcandidate without validation
        start_time = time.time()
        candidate = self.nodes[1].getminingcandidate(True)
        end_time = time.time()
        novalidation_time = end_time - start_time
        self.log.info("Time to get candidate without validation: {}".format(novalidation_time))

        # Without validation will be significantly quicker
        assert novalidation_time < validation_time

    def test_additional_fields(self, node):
        """
            test rpc call getminingcandidate that correctly returns the number of transactions
            and size of the current candidate
        """
        node.generate(40)
        num_transactions = 40
        relay_fee = Decimal("250") / 100000000
        size_txs = 0
        for i in range(0, num_transactions):
            utx = get_any_unspent(node.listunspent(), threshold_amount=1.0)
            inputs = [{'txid': utx['txid'], 'vout': utx['vout']}]
            outputs = {node.getnewaddress(): utx['amount']-relay_fee}
            rawtx = node.createrawtransaction(inputs, outputs)
            signed = node.signrawtransaction(rawtx)
            node.sendrawtransaction(signed["hex"])
            size_txs += len(signed["hex"]) / 2

        def wait_for_transactions():
            candidate = node.getminingcandidate()
            # check that candidate contains num_transactions + coinbase transaction
            return int(candidate['num_tx']) == num_transactions + 1

        wait_until(wait_for_transactions, check_interval=0.01)

        candidate = node.getminingcandidate()
        self.log.info(f"number of transactions that candidate has including coinbase: {candidate['num_tx']}")
        self.log.info(f"size without coinbase: {candidate['sizeWithoutCoinbase']} bytes")

        # num_transactions + coinbase transaction
        assert_equal(num_transactions + 1, int(candidate['num_tx']))
        # size of transactions + 80 bytes for header
        assert_equal(size_txs + 80, candidate['sizeWithoutCoinbase'])

    def run_test(self):
        txnNode = self.nodes[0]
        blockNode = self.nodes[1]

        self.test_mine_block(txnNode, blockNode, True)
        self.test_mine_block(txnNode, blockNode, False)

        self.test_api_errors(blockNode, txnNode)
        self.sync_all()

        self.test_optional_validation()

        self.test_mine_from_old_mining_candidate(blockNode, True)

        self.sync_all()

        self.test_mine_from_old_mining_candidate(blockNode, False)

        self.test_additional_fields(txnNode)


if __name__ == '__main__':
    MiningTest().main()
