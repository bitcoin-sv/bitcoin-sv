#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Scenario:
# 1. Prepare 120 starting blocks.
# 2. Make 2 more blocks and send them via P2P.
#    First block has 4 transactions (1 coinbase + 3 normal), second block has 18 transactions (1 coinbase + 17 normal)
# 3. Send gethdrsen message for last 2 blocks.
# 4. Receive hdrsen message with 2 HeadersEnriched objects: calculate and check their merkle proof.

###############

# 5. Send another block with coinbase transaction larger than 1MB.
# 6. Send gethdrsen for the last block and successfully receive hdrsen message.
# 7. Send gethdrsen for the last two blocks. Only hdrsen for the first block should be received. noMoreHeaders flag should be false.
#    The last block header is not received because it is too large (larger than MAX_PROTOCOL_RECV_PAYLOAD_LENGTH).
# 8. Test stophash parameter

from test_framework.mininode import CBlock, CTransaction, msg_block, msg_gethdrsen, CTxOut, MAX_PROTOCOL_RECV_PAYLOAD_LENGTH, CTxIn, COutPoint, uint256_from_str, ToHex, FromHex
from test_framework.util import assert_equal, hex_str_to_bytes
from test_framework.test_framework import BitcoinTestFramework
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.blocktools import create_coinbase, merkle_root_from_merkle_proof
from test_framework.script import CScript, OP_RETURN, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG
import math


class BsvHeadersEnrichedTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    # This function takes unspent transaction and returns transaction (pay to random address), second (optional)
    # parameter is fee that we want to pay for this transaction.
    def make_signed_tx(self, node, unspent_transaction, fee=10000):
        unspent_amount = int(unspent_transaction['amount']) * 100000000  # BTC to Satoshis
        ftx = CTransaction()
        ftx.vout.append(CTxOut(unspent_amount - fee, CScript([OP_DUP, OP_HASH160,
                                                              hex_str_to_bytes(
                                                                  "ab812dc588ca9d5787dde7eb29569da63c3a238c"),
                                                              OP_EQUALVERIFY,
                                                              OP_CHECKSIG])))  # Pay to random address
        ftx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(unspent_transaction["txid"])[::-1]),
                                       unspent_transaction["vout"])))
        ftx.rehash()
        ftx_hex = node.signrawtransaction(ToHex(ftx))['hex']
        ftx = FromHex(CTransaction(), ftx_hex)
        ftx.rehash()
        return ftx

    def make_block(self, unspent_txns, bigCoinbaseTx=False):
        tmpl = self.nodes[0].getblocktemplate()
        coinbase_tx = create_coinbase(height=int(tmpl["height"]) + 1)
        coinbase_tx.vin[0].nSequence = 2 ** 32 - 2
        if bigCoinbaseTx:
            coinbase_tx.vout.append(CTxOut(1000, CScript([OP_RETURN] + [b"a" * MAX_PROTOCOL_RECV_PAYLOAD_LENGTH])))
        coinbase_tx.rehash()

        transactions_to_send = []
        for i in range(len(unspent_txns)):
            tx = self.make_signed_tx(self.nodes[0], unspent_txns[i], 100000)
            transactions_to_send.append(tx)

        block = CBlock()
        block.nVersion = tmpl["version"]
        block.hashPrevBlock = int(tmpl["previousblockhash"], 16)
        block.nTime = tmpl["curtime"]
        block.nBits = int(tmpl["bits"], 16)
        block.nNonce = 0
        block.vtx = [coinbase_tx]
        block.vtx.extend(transactions_to_send)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        block.rehash()

        return block, coinbase_tx

    def send_block(self, unspent_txns, bigCoinbaseTx=False):
        block, coinbase_tx = self.make_block(unspent_txns, bigCoinbaseTx)
        self.test_node.cb.send_message(msg_block(block))
        self.test_node.cb.sync_with_ping()

        return block, coinbase_tx

    def send_gethdrsen(self, locator_hash, hashstop=0):
        msg = msg_gethdrsen()
        msg.locator.vHave = [locator_hash]
        msg.hashstop = hashstop
        self.test_node.cb.send_message(msg)
        self.test_node.cb.sync_with_ping()

    def check_hdrsen_message(self, hdrsen, num_of_txs, coinbase_tx, block):
        assert_equal(hdrsen.nTx, num_of_txs)
        hdrsen.coinbaseTxProof.tx.rehash()
        assert_equal(hdrsen.coinbaseTxProof.tx.hash, coinbase_tx.hash)
        assert_equal(hdrsen.coinbaseTxProof.proof.flags, 0)
        assert_equal(hdrsen.coinbaseTxProof.proof.index, 0)
        assert_equal(hdrsen.coinbaseTxProof.proof.txOrId, coinbase_tx.sha256)
        assert_equal(hdrsen.coinbaseTxProof.proof.target, block.sha256)
        assert_equal(len(hdrsen.coinbaseTxProof.proof.nodes), math.ceil(math.log2(num_of_txs)))

        merkleProof = [format(x.value, '064x') for x in hdrsen.coinbaseTxProof.proof.nodes]
        calculatedRootHash = merkle_root_from_merkle_proof(hdrsen.coinbaseTxProof.tx.sha256, merkleProof)
        assert_equal(calculatedRootHash, hdrsen.hashMerkleRoot)

    def run_test(self):

        with self.run_node_with_connections("Test P2P hdrsen message.", 0, ['-genesisactivationheight=1'], 1) as connections:

            self.test_node = connections[0]

            headersEnriched = []

            def on_hdrsen(conn, message):
                for h in message.headers:
                    headersEnriched.append(h)

            self.test_node.cb.on_hdrsen = on_hdrsen

            startingHeight = 120
            self.nodes[0].generate(startingHeight)
            assert_equal(startingHeight, self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height'])
            hash_at_120 = int(self.nodes[0].getbestblockhash(), 16)

            unspent_txns = self.nodes[0].listunspent()

            # First block includes 3 transactions + coinbase transaction
            txsInFirstBlock = 4
            first_block, first_coinbase_tx = self.send_block(unspent_txns[:3])

            # Second block includes 17 transactions + coinbase transaction
            txsInSecondBlock = 18
            second_block, second_coinbase_tx = self.send_block(unspent_txns[3:])

            assert_equal(startingHeight+2, self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height'])

            self.send_gethdrsen(hash_at_120)
            self.test_node.cb.wait_for_hdrsen()

            assert_equal(len(headersEnriched), 2)

            # Test first headersEnriched message.
            self.check_hdrsen_message(headersEnriched[0], txsInFirstBlock, first_coinbase_tx, first_block)

            # Test second headersEnriched message.
            self.check_hdrsen_message(headersEnriched[1], txsInSecondBlock, second_coinbase_tx, second_block)

            #######################
            hash_at_122 = int(self.nodes[0].getbestblockhash(), 16)
            unspent_txns = self.nodes[0].listunspent()
            headersEnriched = []

            # Send block with coinbase transaction larger than 1MB.
            big_coinbase_tx = self.send_block(unspent_txns, bigCoinbaseTx=True)
            assert_equal(startingHeight+3, self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height'])

            # Obtain hdrsen for block with big coinbase transaction.
            self.send_gethdrsen(hash_at_122)
            self.test_node.cb.wait_for_hdrsen()
            assert_equal(len(headersEnriched), 1)

            headersEnriched = []
            # Obtain hdrsen for last 2 blocks (1 normal and one with big coinbase transaction)
            hash_at_121 = int(self.nodes[0].getblockhash(self.nodes[0].getblockcount() - 2), 16)
            self.send_gethdrsen(hash_at_121)
            self.test_node.cb.wait_for_hdrsen()
            assert_equal(len(headersEnriched), 1)
            # There are more headers to fetch (but we cannot do it in this message)
            assert_equal(headersEnriched[0].noMoreHeaders, False)
            # Make sure we fetched the smaller hdrsen.
            assert_equal(headersEnriched[0].nTx, txsInSecondBlock)

            # Check that hashstop parameter works as expected
            # gethdrsen between (120 and 122] should get 2 hdrsen
            headersEnriched = []
            self.send_gethdrsen(hash_at_120, hash_at_122)
            self.test_node.cb.wait_for_hdrsen()
            assert_equal(len(headersEnriched), 2)
            # gethdrsen between (120 and 121] should get 1 hdrsen
            headersEnriched = []
            self.send_gethdrsen(hash_at_120, hash_at_121)
            self.test_node.cb.wait_for_hdrsen()
            assert_equal(len(headersEnriched), 1)


if __name__ == '__main__':
    BsvHeadersEnrichedTest().main()
