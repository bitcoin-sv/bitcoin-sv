#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Start up nodes, feed in some transactions, use the mining API to
mine some blocks, verify all nodes accept the mined blocks and
check if merkleproof returned by getblockheader RPC function and
/rest/headers/extended REST call is valid.
"""

from test_framework.blocktools import create_block_from_candidate, merkle_root_from_merkle_proof
from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import ToHex
from test_framework.util import satoshi_round, connect_nodes_bi, create_confirmed_utxos, assert_equal, sync_mempools, sync_blocks
from decimal import Decimal
import math

import json
import http.client
import urllib.parse


# Split some UTXOs into some number of spendable outputs
def split_utxos(fee, node, count, utxos, sync_nodes):
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
        sync_blocks(sync_nodes)

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


# The main test class
class BSVBlockWithCBProof(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = False

    def setup_network(self):
        super().setup_network()

        # Connect the nodes up
        connect_nodes_bi(self.nodes, 0,1)

    def _send_transactions_to_node(self, node, num_trasactions):
        # Create UTXOs to build a bunch of transactions from
        self.relayfee = Decimal("0.00000250")
        utxos = create_confirmed_utxos(self.relayfee, node, 100, nodes=self.nodes)
        self.sync_all()

        # Create a lot of transactions from the UTXOs
        newutxos = split_utxos(self.relayfee, node, num_trasactions, utxos, sync_nodes=self.nodes)
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

        self._send_transactions_to_node(txnNode, 100)
        sync_mempools(self.nodes)

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
        assert 'merkleProof' in candidate

        submitResult = self._create_and_submit_block(blockNode, candidate, get_coinbase)
        sync_blocks(self.nodes)

        # submitResult is bool True for success, if failure error is returned
        assert_equal(submitResult, True)

    # Return JSON header obtained via REST call /rest/headers/extended
    def call_rest_headers_extended_json(self, node, hash):
        FORMAT_SEPARATOR = "."
        url = urllib.parse.urlparse(node.url)
        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.request('GET', '/rest/headers/extended/1/' + hash + FORMAT_SEPARATOR + "json")

        response = conn.getresponse()
        assert_equal(response.status, 200)

        return json.loads(response.read().decode('utf-8'), parse_float=Decimal)

    # Helper to check if JSON extended header returned via rest call matches hdr
    def check_rest_header_extendeded_json(self, node, hash, hdr_expected):
        hdrs_rest = self.call_rest_headers_extended_json(node, hash)
        assert_equal(len(hdrs_rest), 1)
        assert_equal(hdrs_rest[0], hdr_expected)

    def check_node(self, node, block_hash):
        obj = node.getblock(block_hash, 2)
        hdr = node.getblockheader(block_hash, 2)
        self.check_rest_header_extendeded_json(node, block_hash, hdr) # header returned via rest call must be the same
        assert_equal(hdr["tx"][0], obj["tx"][0]) # coinbase transaction must also be returned in a header
        assert(len(hdr["merkleproof"]) > 0)
        # check if merkle root is correct by calculating root from merkleproof tree and coinbase tx hash
        root_hash = merkle_root_from_merkle_proof(int(obj["tx"][0]["hash"],16), hdr["merkleproof"])
        assert_equal(root_hash, int(obj["merkleroot"],16))

    def run_test(self):
        txnNode = self.nodes[0]
        blockNode = self.nodes[1]

        self.test_mine_block(txnNode, blockNode, True)
        bestHash = blockNode.getbestblockhash()
        self.check_node(blockNode, bestHash)
        # also check in 2nd node that received the newly mined block if merkleeproof is present and if it's correct
        self.check_node(txnNode, bestHash)

        self.test_mine_block(txnNode, blockNode, False)
        bestHash = blockNode.getbestblockhash()
        self.check_node(blockNode, bestHash)
        # also check in 2nd node that received the newly mined block if merkleeproof is present and if it's correct
        self.check_node(txnNode, bestHash)


if __name__ == '__main__':
    BSVBlockWithCBProof().main()
