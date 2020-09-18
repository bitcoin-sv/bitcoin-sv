#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

#
# Test merkle proof requests and validation
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import connect_nodes, assert_equal, Decimal, assert_raises_rpc_error
import os

class MerkleProofTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # On second node -txindex is set
        self.extra_args = [[], ["-txindex"]]

    def setup_network(self):
        self.setup_nodes()
        connect_nodes(self.nodes[0], 1)
        self.sync_all()

    def verify_merkle_proof(self, txid, blockhash, node):
        assert self.nodes[node].verifymerkleproof(self.nodes[node].getmerkleproof(txid))
        assert self.nodes[node].verifymerkleproof(self.nodes[node].getmerkleproof(txid, blockhash))

    def run_test(self):
        self.log.info("Mining 500 blocks...")
        self.nodes[0].generate(500)
        self.sync_all()

        assert_equal(self.nodes[1].getblockcount(), 500)
        assert_equal(self.nodes[1].getbalance(), 0)

        # Create and send two transactions
        tx1_in = self.nodes[0].listunspent().pop()
        tx1_out = tx1_in["amount"] - Decimal("0.01")
        tx1 = self.nodes[0].createrawtransaction([tx1_in], {self.nodes[1].getnewaddress(): tx1_out})
        txid1 = self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(tx1)["hex"])
        tx2_in = self.nodes[0].listunspent().pop()
        tx2_out = tx2_in["amount"] - Decimal("0.01")
        tx2 = self.nodes[0].createrawtransaction([tx2_in], {self.nodes[1].getnewaddress(): tx2_out})
        txid2 = self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(tx2)["hex"])

        # Try to get proof for one of the trasaction - should fail because transaction is not yet in a block
        assert_raises_rpc_error(-5, "Transaction not yet in block", self.nodes[0].getmerkleproof, txid1)

        # Mine a new block
        self.log.info("Mining 501st block...")
        self.nodes[0].generate(1)
        self.sync_all()
        height_of_block_501 = self.nodes[1].getblockcount()

        # Check some negative tests on verifymerkleproof
        assert_raises_rpc_error(-8, "\"flags\" must be a numeric value", self.nodes[0].verifymerkleproof, {'flags': '2'})
        assert_raises_rpc_error(-8, "verifymerkleproof only supports \"flags\" with value 2", self.nodes[0].verifymerkleproof, {'flags': 1})
        assert_raises_rpc_error(-8, "\"nodes\" must be a Json array", self.nodes[0].verifymerkleproof, 
            {'flags':2,
             'index':4,
             'txOrId':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890',
             'target':{'merkleroot':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890'},
             'nodes':'*'})
        assert_raises_rpc_error(-8, "\"node\" must be a \"hash\" or \"*\"", self.nodes[0].verifymerkleproof, 
            {'flags':2,
             'index':4,
             'txOrId':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890',
             'target':{'merkleroot':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890'},
             'nodes':[2]})
        assert_raises_rpc_error(-8, "node must be of length 64 (not 10)", self.nodes[0].verifymerkleproof, 
            {'flags':2,
             'index':4,
             'txOrId':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890',
             'target':{'merkleroot':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890'},
             'nodes':['*','abcdef1234']})

        # Get proof for 1st and 2nd transaction and verify that calculated roots are the same as block's merkle root
        hash_of_block_501 = self.nodes[0].getblockhash(height_of_block_501)
        self.verify_merkle_proof(txid1, hash_of_block_501, 0)
        self.verify_merkle_proof(txid2, hash_of_block_501, 0)

        # Create and send 3rd transaction
        tx_spent = self.nodes[1].listunspent().pop()
        tx3_out = tx_spent["amount"] - Decimal("0.01")
        tx3 = self.nodes[1].createrawtransaction([tx_spent], {self.nodes[0].getnewaddress(): tx3_out})
        txid3 = self.nodes[0].sendrawtransaction(self.nodes[1].signrawtransaction(tx3)["hex"])

        # Mine a new block
        self.log.info("Mining 502nd block...")
        self.nodes[0].generate(1)
        self.sync_all()

        # Get id of spent and unspent transaction
        txid_spent = tx_spent["txid"]
        txid_unspent = txid1 if txid_spent != txid1 else txid2

        # We can't find the block if transaction was spent because -txindex is not set on node[0]
        assert_raises_rpc_error(-5, "Transaction not yet in block", self.nodes[0].getmerkleproof, txid_spent)

        # We can get the proof if we specify proper block hash
        assert self.nodes[0].verifymerkleproof(self.nodes[0].getmerkleproof(txid_spent, hash_of_block_501))

        # We can't get the proof if we specify a non-existent block
        assert_raises_rpc_error(-5, "Block not found", self.nodes[0].getmerkleproof,  txid_spent, "1234567890abcdef1234567890abcdef")

        # We can get the proof if the transaction is unspent
        self.verify_merkle_proof(txid_unspent, hash_of_block_501, 0)

        # We can get a proof of a spent transaction without block hash if node runs with -txindex (nodes[1] in this case)
        self.verify_merkle_proof(txid_spent, hash_of_block_501, 1)

        # Restart nodes
        self.log.info("Restarting nodes...")
        self.stop_nodes()
        self.start_nodes(self.extra_args)

        # Repeat tests after nodes restart
        self.verify_merkle_proof(txid_unspent, hash_of_block_501, 0)
        self.verify_merkle_proof(txid_spent, hash_of_block_501, 1)
        hash_of_block_502 = self.nodes[0].getblockhash(height_of_block_501 + 1)
        self.verify_merkle_proof(txid3, hash_of_block_502, 0)

if __name__ == '__main__':
    MerkleProofTest().main()
