#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

#
# Test merkle proof requests and validation
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import connect_nodes, assert_equal, Decimal, assert_raises_rpc_error, sync_blocks, random, assert_greater_than
import os
import shutil


class MerkleProofTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # On first node set preferred data file size to 30 kB
        # On second node -txindex is set
        self.extra_args = [["-preferredmerkletreefilesize=30720"], ["-txindex"]]

    def setup_network(self):
        self.setup_nodes()
        connect_nodes(self.nodes, 0, 1)
        self.sync_all()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args)
        # change rpc_timeout for node0 to avoid getting timeout on rpc generate when generating
        # large number of blocks.
        self.nodes[0].rpc_timeout = 300
        self.start_nodes()

    def check_equivalence(self, a, b):
        if a['target']['hash'] != b['target']:
            return False
        for ax, bx in zip(a['nodes'], b['nodes']):
            if ax != bx:
                return False
        return True

    def verify_merkle_proof(self, txid, blockhash, node):
        a1 = self.nodes[node].getmerkleproof(txid)
        a2 = self.nodes[node].getmerkleproof(txid, blockhash)
        b1 = self.nodes[node].getmerkleproof2("", txid)
        b2 = self.nodes[node].getmerkleproof2(blockhash,txid)
        assert self.nodes[node].verifymerkleproof(a1)
        assert self.nodes[node].verifymerkleproof(a2)
        assert(self.check_equivalence(a1, b1))
        assert(self.check_equivalence(a2, b2))

        c1 = self.nodes[node].getmerkleproof2("",txid, False, "merkleroot")
        c2 = self.nodes[node].getmerkleproof2(blockhash,txid, False, "merkleroot")

        assert(c1["target"] == a1["target"]["merkleroot"])
        assert(c2["target"] == a2["target"]["merkleroot"])

        d1 = self.nodes[node].getmerkleproof2("",txid, False, "hash")
        d2 = self.nodes[node].getmerkleproof2(blockhash,txid, False, "hash")

        assert(d1["target"] == a1["target"]["hash"])
        assert(d2["target"] == a2["target"]["hash"])
        assert(d2["target"] == blockhash)

        e1 = self.nodes[node].getmerkleproof2("", txid, False, "header")
        e2 = self.nodes[node].getmerkleproof2(blockhash, txid, False, "header")

        current_blockhash = d1["target"]
        blockheader_func = self.nodes[node].getblockheader(current_blockhash, False)
        blockheader_field = e1["target"]
        assert(blockheader_func == blockheader_field)

        blockheader_func = self.nodes[node].getblockheader(blockhash, False)
        blockheader_field = e2["target"]
        assert(blockheader_func == blockheader_field)

    # Calculate Merkle tree size in bytes
    def merkle_tree_size(self, number_of_transactions):
        merkle_tree_size = 0
        while number_of_transactions > 0:
            merkle_tree_size += number_of_transactions
            number_of_transactions //= 2
        # 32 bytes for each hash
        merkle_tree_size *= 32
        return merkle_tree_size

    def verify_stored_data(self, verifyData, node):
        for verifyBlockHash in verifyData:
            verifyTransactions = verifyData[verifyBlockHash]
            for verifyTxid in verifyTransactions:
                self.verify_merkle_proof(verifyTxid, verifyBlockHash, node)

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
        assert_raises_rpc_error(-5, "Transaction not yet in block", self.nodes[0].getmerkleproof2, "", txid1)

        # Mine a new block
        self.log.info("Mining 501st block...")
        self.nodes[0].generate(1)
        self.sync_all()
        height_of_block_501 = self.nodes[1].getblockcount()

        # Check some negative tests on verifymerkleproof
        assert_raises_rpc_error(-8, "\"flags\" must be a numeric value", self.nodes[0].verifymerkleproof, {'flags': '2'})
        assert_raises_rpc_error(-8, "verifymerkleproof only supports \"flags\" with value 2", self.nodes[0].verifymerkleproof, {'flags': 1})
        assert_raises_rpc_error(-8,
                                "\"nodes\" must be a Json array",
                                self.nodes[0].verifymerkleproof,
                                {'flags':2,
                                 'index':4,
                                 'txOrId':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890',
                                 'target':{'merkleroot':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890'},
                                 'nodes':'*'})
        assert_raises_rpc_error(-8,
                                "\"node\" must be a \"hash\" or \"*\"",
                                self.nodes[0].verifymerkleproof,
                                {'flags':2,
                                 'index':4,
                                 'txOrId':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890',
                                 'target':{'merkleroot':'abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890'},
                                 'nodes':[2]})
        assert_raises_rpc_error(-8,
                                "node must be of length 64 (not 10)",
                                self.nodes[0].verifymerkleproof,
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
        assert_raises_rpc_error(-5, "Transaction not yet in block", self.nodes[0].getmerkleproof2, "", txid_spent)

        # We can get the proof if we specify proper block hash
        a = self.nodes[0].getmerkleproof(txid_spent, hash_of_block_501)
        b = self.nodes[0].getmerkleproof2(hash_of_block_501, txid_spent)
        assert self.nodes[0].verifymerkleproof(a)
        assert(self.check_equivalence(a,b))

        # We can't get the proof if we specify a non-existent block
        assert_raises_rpc_error(-5, "Block not found", self.nodes[0].getmerkleproof,  txid_spent, "1234567890abcdef1234567890abcdef")
        assert_raises_rpc_error(-5, "Block not found", self.nodes[0].getmerkleproof2,  "1234567890abcdef1234567890abcdef", txid_spent)

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

        # Create more blocks to get utxos
        self.log.info("Mining additional 1500 blocks...")
        self.nodes[0].generate(1500)
        sync_blocks(self.nodes[0:1])

        # Use all utxos and create more Merkle Trees
        # We create blocks with max 400 transactions (~25 kB for biggest Merkle Tree)
        self.log.info("Mining blocks with random transactions using all utxos...")
        utxos = self.nodes[0].listunspent()
        calculated_merkle_tree_disk_size = 0
        verifyData = {}
        while len(utxos) > 0:
            # Choose random number of transactions
            send_transactions = random.randint(1, 400)
            if len(utxos) < send_transactions:
                send_transactions = len(utxos)
            # Send transactions
            for i in range(send_transactions):
                tx_in = utxos.pop()
                tx_out = tx_in["amount"] - Decimal("0.01")
                tx = self.nodes[0].createrawtransaction([tx_in], {self.nodes[1].getnewaddress(): tx_out})
                txid = self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(tx)["hex"])
            # Mine a block
            self.nodes[0].generate(1)
            sync_blocks(self.nodes[0:1])
            # Verify proofs of some random transactions in each block
            hash_of_this_block = self.nodes[0].getblockhash(self.nodes[0].getblockcount())
            transactions_of_this_block = self.nodes[0].getblock(hash_of_this_block, True)["tx"]
            calculated_merkle_tree_disk_size += self.merkle_tree_size(len(transactions_of_this_block))
            verifyData[hash_of_this_block] = transactions_of_this_block
        # Verify merkle proofs of all transactions in all blocks
        self.verify_stored_data(verifyData, 0)

        # Data files checks
        number_of_data_files = 0
        disk_size = 0
        node0_data_dir = os.path.join(self.options.tmpdir, "node0", "regtest", "merkle", "")
        for data_file in os.listdir(node0_data_dir):
            data_file_name = node0_data_dir + data_file
            if os.path.isfile(data_file_name):
                data_file_size = os.path.getsize(data_file_name)
                # No file should be bigger than 30 kB since no Merkle Tree takes more than 25 kB
                assert_greater_than(30 * 1024, data_file_size)
                disk_size += data_file_size
                number_of_data_files += 1
        # Verify that Merkle Tree disk size is at least the size of Merkle Trees we just stored
        assert_greater_than(disk_size, calculated_merkle_tree_disk_size)
        # Number of data files should be at least calculated_merkle_tree_disk_size/preferred_file_size
        assert_greater_than(number_of_data_files, calculated_merkle_tree_disk_size/(30 * 1024))

        # Delete index to test recreation of index when node is started again
        self.log.info("Restarting nodes to remove Merkle Trees index...")
        self.stop_nodes()
        node0_index_dir = os.path.join(node0_data_dir, "index", "")
        shutil.rmtree(node0_index_dir)
        self.start_nodes(self.extra_args)
        # Repeat merkle proof checks
        self.verify_stored_data(verifyData, 0)
        # Since index was recreated from data files, requesting existing merkle trees shouldn't create any new data
        new_disk_size = 0
        for data_file in os.listdir(node0_data_dir):
            data_file_name = node0_data_dir + data_file
            if os.path.isfile(data_file_name):
                new_disk_size += os.path.getsize(data_file_name)
        assert_equal(disk_size, new_disk_size)


if __name__ == '__main__':
    MerkleProofTest().main()
