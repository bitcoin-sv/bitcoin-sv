#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test that when prune is enabled on the node, RPC extended getheader and P2P gethdrsen do not return the merkle proof of a coinbase if the block was pruned.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestInstance
from test_framework.util import satoshi_round, Decimal, sync_blocks
from test_framework.blocktools import CTxIn, COutPoint, msg_tx, assert_equal, assert_raises_rpc_error, merkle_root_from_merkle_proof
from test_framework.cdefs import ONE_MEGABYTE, ONE_GIGABYTE, DEFAULT_MIN_BLOCKS_TO_KEEP
from test_framework.script import CScript, CTransaction, CTxOut, OP_TRUE
from test_framework.mininode import FromHex, CBlock, msg_gethdrsen


class BSVMerkleProofInPrunedBlock(ComparisonTestFramework):

    def create_transaction(self, prevtx, number_of_outputs, sig, value, scriptPubKey=CScript()):
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(prevtx.sha256, 0), sig, 0xffffffff))
        for _ in range(number_of_outputs):
            tx.vout.append(CTxOut(value, scriptPubKey))
        tx.calc_sha256()
        return tx

    # Sync chain tip inside python with tip of the node
    def sync_chain_tip_with_node(self, node):
        bestBlockHash = node.getbestblockhash()
        blockHex = node.getblock(bestBlockHash, 0)
        blockHeight = node.getblockheader(bestBlockHash, 1)['height']
        blockTip = FromHex(CBlock(), blockHex)
        blockTip.hash = bestBlockHash
        blockTip.calc_sha256()
        self.chain.tip = blockTip
        self.chain.block_heights[blockTip.sha256] = blockHeight

    # Take care...if too many blocks will be required then the function will run out of coins
    # the reason is that it currently takes coins only from 1 CB transaction
    def create_big_blocks(self, spendableCB_Tx, num_of_blocks, tx_count_per_block, node, conn):
        fee = satoshi_round(Decimal(((ONE_MEGABYTE + 100) * tx_count_per_block * 0.00000001)))
        fee = int(fee * 100000000)
        availableAmount = spendableCB_Tx[0].tx.vout[0].nValue
        sendValue = int((availableAmount - 9000000) / (num_of_blocks * tx_count_per_block))

        # Create a transaction with many outputs that will be spent later by big transaction
        spendableTx = self.create_transaction(spendableCB_Tx[0].tx, num_of_blocks * tx_count_per_block, CScript(), sendValue)
        conn.send_message(msg_tx(spendableTx))
        self.check_mempool(self.test.connections[0].rpc, [spendableTx])

        node.generate(1)
        self.sync_chain_tip_with_node(node)

        # Create blocks with size of 1 MB and desired number of transactions
        n = 0
        for _ in range(num_of_blocks):
            for _ in range(tx_count_per_block):
                availableAmount = spendableTx.vout[n].nValue
                sendValue = int((availableAmount - fee) / tx_count_per_block)
                smallTx = CTransaction()
                smallTx.vin.append(CTxIn(COutPoint(spendableTx.sha256, n), CScript([OP_TRUE]), 0xffffffff))
                smallTx.vout.append(CTxOut(sendValue, CScript([b'\xFE' * int((ONE_MEGABYTE - 1000) / tx_count_per_block)])))
                smallTx.calc_sha256()
                conn.send_message(msg_tx(smallTx))
                self.check_mempool(self.test.connections[0].rpc, [smallTx])
                n += 1
            node.generate(1)
            self.sync_chain_tip_with_node(node)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.FORMAT_SEPARATOR = "."
        self.extra_args = [['-whitelist=127.0.0.1',
                            '-rpcservertimeout=5000',
                            '-acceptnonstdtxn',
                            '-genesisactivationheight=1',
                            '-maxtxnvalidatorasynctasksrunduration=3600001',
                            '-maxnonstdtxvalidationduration=3600000',
                            '-maxtxsizepolicy=%d' % ONE_GIGABYTE,
                            '-preferredblockfilesize=%d' % ONE_MEGABYTE,
                            '-prune=1']]

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        # Create a new block
        block(0)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(1100):
            block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()

        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(50):
            out.append(self.chain.get_spendable_output())

        self.create_big_blocks(out, 10, 10, node, self.test.connections[0])
        sync_blocks(self.nodes)
        self.log.info('Test (big blocks created): PASS')

        # Get one of the big blocks and check if Merkle proof is ok
        bigBlock = node.getblock(node.getbestblockhash(), 1)
        bigBlockHash = bigBlock["hash"]
        bigBlockHdr = node.getblockheader(node.getbestblockhash(), 2)
        assert(len(bigBlockHdr["merkleproof"]) == 4)
        rootHash = merkle_root_from_merkle_proof(int(bigBlock["tx"][0],16), bigBlockHdr["merkleproof"])
        assert_equal(rootHash, int(bigBlock["merkleroot"],16))

        # Need to mine at least DEFAULT_MIN_BLOCKS_TO_KEEP additional blocks (min number of unpruned blocks) so we can prune our big blocks
        for i in range(DEFAULT_MIN_BLOCKS_TO_KEEP + 100):
            block(7000 + i, version=10) # NOTE: Must use higher block version since chain will be past BIP-65 activation height
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # We now have total 1 + 1100 + 10 big blocks + DEFAULT_MIN_BLOCKS_TO_KEEP + 100 blocks
        # Let's prune blocks up to height 1130 to also prune 10 big blocks
        node.pruneblockchain(1130)
        # Check that block is really pruned
        assert_raises_rpc_error(-1, "Block file %s not available." % bigBlockHash, node.getblock, bigBlockHash, 1)

        # After pruning getblockheader method should not have merkle proof and coinbase transaction
        bigBlockHeader = node.getblockheader(bigBlockHash, 2)
        assert("merkleproof" not in bigBlockHeader)

        # P2P message gethdrsen should also return hdrsen message without Merkle proof and coinbase transaction for pruned block
        self.test.connections[0].send_message(msg_gethdrsen(locator_have=[], hashstop=int(bigBlockHash,16)))
        self.test.test_nodes[0].wait_for_hdrsen(5)
        assert_equal(len(self.test.test_nodes[0].last_message.get("hdrsen").headers), 1)
        headerEnriched = self.test.test_nodes[0].last_message.get("hdrsen").headers[0]
        headerEnriched.rehash()
        assert_equal(headerEnriched.hash, bigBlockHash)
        assert(headerEnriched.coinbaseTxProof is None)


if __name__ == '__main__':
    BSVMerkleProofInPrunedBlock().main()
