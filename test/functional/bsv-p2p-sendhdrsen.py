#!/usr/bin/env python3
# Copyright (c) 2021  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check P2P message sendhdrsen
"""
from test_framework.blocktools import create_block, create_coinbase, create_transaction, merkle_root_from_branch
from test_framework.miner_id import MinerIdKeys, make_miner_id_block, create_dataref_txn
from test_framework.mininode import COIN, CBlock, CInv, CTxOut, FromHex, mininode_lock, MAX_PROTOCOL_RECV_PAYLOAD_LENGTH, msg_gethdrsen, msg_sendhdrsen, msg_sendheaders, NetworkThread, NodeConn, NodeConnCB, ToHex
from test_framework.script import CScript, OP_FALSE, OP_RETURN, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, p2p_port, wait_until, sync_blocks

import time
import os


class SPVNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.hdrsen = [] # array of received hdrsen messages

    def sendhdrsen(self):
        self.send_and_ping(msg_sendhdrsen())

    def gethdrsen(self, locator, hashstop):
        msg = msg_gethdrsen()
        msg.locator.vHave = locator
        msg.hashstop = hashstop
        self.send_and_ping(msg)

    def on_hdrsen(self, conn, msg):
        self.hdrsen.append(msg)

    def wait_for_hdrsen(self, timeout=5):
        def test_function(): return len(self.hdrsen)>0
        wait_until(test_function, timeout=timeout, lock=mininode_lock)
        with mininode_lock:
            return self.hdrsen.pop(0)

    def get_peer_info(self, node):
        peers_info = node.getpeerinfo()

        # Find the element corresponding to info about this peer in peers_info
        our_peer_info = [peer for peer in peers_info if ("addrlocal" in peer) and (peer["addrlocal"]==(self.connection.dstaddr+":"+str(self.connection.dstport)))]
        assert_equal(len(our_peer_info), 1)

        return our_peer_info[0]


class SendHdrsEnTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-genesisactivationheight=1']]*2 # Genesis must be activated so that we can send large transactions

        # Setup miner ID keys and a single revocation key
        self.minerIdKey = MinerIdKeys("01")
        self.minerIdRevocationKey = MinerIdKeys("10")

    def generate_block(self, node):
        block_hashes = node.generate(1)
        assert_equal(len(block_hashes), 1)
        block = FromHex(CBlock(), node.getblock(block_hashes[0], 0))
        block.rehash()
        return block

    def submit_block(self, node, coinbase_tx, txs=[]):
        templ = node.getblocktemplate()
        block = create_block(int(templ["previousblockhash"], 16), coinbase_tx, templ["mintime"])
        block.nVersion = templ["version"]
        for tx in txs:
            block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        block.rehash()
        node.submitblock(ToHex(block))
        assert_equal(node.getbestblockhash(), block.hash)
        return block

    # Check contents of TSCMerkleProof object in enriched header
    def check_TSCMerkleProof(self, hdren):
        def check(txproof, expectedHash, expectedMerkleRoot):
            assert(txproof)
            assert_equal(txproof.proof.flags, 0)
            assert_equal(txproof.proof.target, expectedHash)
            txproof.tx.rehash()
            assert_equal(txproof.proof.txOrId, txproof.tx.sha256)
            calculatedRootHash = merkle_root_from_branch(txproof.proof.txOrId, txproof.proof.index, [x.value for x in txproof.proof.nodes])
            assert_equal(calculatedRootHash, expectedMerkleRoot)

        hdren.rehash()
        check(hdren.coinbaseTxProof, hdren.sha256, hdren.hashMerkleRoot)
        if hdren.minerInfoProof:
            check(hdren.minerInfoProof, hdren.sha256, hdren.hashMerkleRoot)

    def run_test(self):
        self.log.info("Checking block announcements using sendhdrsen message")

        # Node that will send enriched header announcements
        node = self.nodes[0]
        # Node used to trigger a reorg
        node1 = self.nodes[1]

        # Generate a block to get out of IBD
        node.generate(1)

        # Create a block with CB transaction whose outputs we can spend later
        funding_tx = create_coinbase(node.getblockcount())
        funding_tx.vout[0].nValue = 1*COIN
        for i in range(1,50):
            funding_tx.vout.append(CTxOut(1*COIN, CScript([OP_TRUE])))
        funding_tx.rehash()
        self.submit_block(node, funding_tx)
        node.generate(100)
        sync_blocks(self.nodes)

        # This is the initial block height before spv_node is started
        start_height=102
        assert_equal(node.getblockcount(), start_height)
        assert_equal(node1.getblockcount(), start_height)
        block0_hash = node.getbestblockhash()
        assert_equal(node1.getbestblockhash(), block0_hash)

        # Node that will receive enriched header announcements from node
        spv_node = SPVNode()
        spv_node.add_connection(NodeConn('127.0.0.1', p2p_port(0), node, spv_node))
        NetworkThread().start()
        spv_node.wait_for_verack()
        spv_node.sync_with_ping()

        # Send sendhdrsen to request receiving hdrsen message when new blocks are announced
        spv_node.sendhdrsen()
        spv_node.sync_with_ping()

        # Generate a block to trigger announcement
        block1 = self.generate_block(node)

        # We should receive an 'inv' message since the node thinks we do not yet have parent block
        spv_node.wait_for_inv([CInv(CInv.BLOCK, block1.sha256)], 5)

        # Send gethdrsen to let the node know that we have this block.
        spv_node.gethdrsen(locator=[block1.sha256], hashstop=0)
        h = spv_node.wait_for_hdrsen()
        assert_equal(h.headers, []) # we should receive no headers

        block2 = self.generate_block(node)

        # We should now receive 'hdrsen' message when block is announced
        h = spv_node.wait_for_hdrsen()
        assert_equal(len(h.headers), 1) # should contain only one header
        h0=h.headers[0]
        assert(h0.coinbaseTxProof)
        assert(not h0.minerInfoProof)
        self.check_TSCMerkleProof(h0) # NOTE: Proof is checked first, since method also calculates hashes of block header transaction
        assert_equal(h0.hash, block2.hash) # should contain header of the block that was just generated
        assert_equal(h0.noMoreHeaders, 1)
        assert(h0.coinbaseTxProof is not None)
        assert_equal(ToHex(block2.vtx[0]), ToHex(h.headers[0].coinbaseTxProof.tx)) # Contents of coinbase tx in hdrsen must be the same as in block

        # Create and submit block with coinbase transaction whose size is such that resulting hdrsen is just within the limit
        coinbase_tx = create_coinbase(node.getblockcount())
        # NOTE: 83 is size of coinbase transaction if there was no data after the PUSHDATA4 after OP_RETURN
        #       152 is size of hdrsen message without transaction:
        #           1 byte for number of headers
        #           80 bytes for header
        #           1 byte for field txn_count
        #           1 byte for field no_more_headers
        #           1 byte for indication of following coinbase details
        #           1 byte for coinbase_merkle_proof.flags
        #           1 byte for coinbase_merkle_proof.index
        #           32 bytes for field coinbase_merkle_proof.txOrId
        #           32 bytes for field coinbase_merkle_proof.target
        #           1 byte for number of elements in field coinbase_merkle_proof.nodes
        #           0 bytes for element data in field coinbase_merkle_proof.nodes
        #           1 byte for indication of following miner-info txn details
        HdrsEnSize = 152
        coinbase_tx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN] + [b"a" * (MAX_PROTOCOL_RECV_PAYLOAD_LENGTH-83-HdrsEnSize)])))
        coinbase_tx.rehash()
        assert_equal(len(coinbase_tx.serialize()), MAX_PROTOCOL_RECV_PAYLOAD_LENGTH-HdrsEnSize) # check that we have created transaction of correct size
        block3 = self.submit_block(node, coinbase_tx)

        # We should receive a 'hdrsen' message whose size is exactly MAX_PROTOCOL_RECV_PAYLOAD_LENGTH
        h = spv_node.wait_for_hdrsen()
        assert_equal(len(h.serialize()), MAX_PROTOCOL_RECV_PAYLOAD_LENGTH)

        # Create and submit block with coinbase transaction whose size is such that resulting hdrsen is one byte over the limit
        coinbase_tx = create_coinbase(node.getblockcount())
        coinbase_tx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN] + [b"a" * (MAX_PROTOCOL_RECV_PAYLOAD_LENGTH-83-(HdrsEnSize-1))])))
        coinbase_tx.rehash()
        block4 = self.submit_block(node, coinbase_tx)

        # We should receive an 'inv' message since hdrsen message is too large
        spv_node.wait_for_inv([CInv(CInv.BLOCK, block4.sha256)], 5)

        # Send gethdrsen to get header for block4
        spv_node.gethdrsen(locator=[block3.sha256], hashstop=0)

        # We should receive a 'hdrsen' message whose size is one byte over the limit
        h = spv_node.wait_for_hdrsen()
        assert_equal(len(h.headers), 1)
        h0=h.headers[0]
        assert(h0.coinbaseTxProof)
        assert(not h0.minerInfoProof)
        self.check_TSCMerkleProof(h0)
        assert_equal(h0.hash, block4.hash)
        assert_equal(len(h.serialize()), MAX_PROTOCOL_RECV_PAYLOAD_LENGTH+1)

        # Create and mine a block with several transactions so that Merkle proof in hdrsen message is not empty
        txs = []
        for i in range(49):
            txs.append(create_transaction(funding_tx, i, CScript(), 1*COIN))
        # Also make this block a miner ID enabled one containing a miner-info txn
        minerIdParams = {
            'height': self.nodes[0].getblockcount() + 1,
            'minerKeys': self.minerIdKey,
            'revocationKeys': self.minerIdRevocationKey
        }
        block5 = make_miner_id_block(node, minerIdParams, utxo={"txid" : funding_tx.hash, "vout" : 49, "amount" : 1.0}, txns=txs)
        coinbase_tx = block5.vtx[0]
        miner_info_tx = block5.vtx[1 + len(txs)]
        node.submitblock(ToHex(block5))

        h = spv_node.wait_for_hdrsen()
        assert_equal(len(h.headers), 1)
        h0=h.headers[0]
        assert(h0.coinbaseTxProof)
        assert(h0.minerInfoProof)
        self.check_TSCMerkleProof(h0)
        assert_equal(h0.hash, block5.hash)
        assert_equal(h0.noMoreHeaders, 1)
        assert_equal(ToHex(coinbase_tx), ToHex(h0.coinbaseTxProof.tx))
        assert_equal(ToHex(miner_info_tx), ToHex(h0.minerInfoProof.tx))

        # Mine 2 more blocks to have a chain of 7 blocks
        for i in range(2):
            block = self.generate_block(node)
            h = spv_node.wait_for_hdrsen()
            assert_equal(len(h.headers), 1)
            h0=h.headers[0]
            self.check_TSCMerkleProof(h0)
            assert(h0.coinbaseTxProof)
            assert(not h0.minerInfoProof)
            assert_equal(h0.hash, block.hash)
            assert_equal(h0.noMoreHeaders, 1)

        # Create an alternate chain on node1 that is 8 blocks long
        sync_blocks(self.nodes)
        node1.invalidateblock(block1.hash)
        assert_equal(node1.getblockcount(), start_height)
        block_hashes = []
        for i in range(8):
            block_hashes.append(node1.generate(1)[0])

        # Check that we receive 8 headers in 1 hdrsen message
        h = spv_node.wait_for_hdrsen()
        assert_equal(len(h.headers), 8)
        for i in range(8):
            hi=h.headers[i]
            assert(hi.coinbaseTxProof)
            assert(not hi.minerInfoProof)
            self.check_TSCMerkleProof(hi)
            assert_equal(hi.hash, block_hashes[i])
            assert(hi.coinbaseTxProof is not None) # coinbase data must be available in all headers in hdrsen message
            if i==7:
                assert_equal(hi.noMoreHeaders, 1) # the last header must indicate that it is a tip
            else:
                assert_equal(hi.noMoreHeaders, 0)

        # Create an alternate chain on node1 that is 9 blocks long
        sync_blocks(self.nodes)
        node1.invalidateblock(block_hashes[0])
        assert_equal(node1.getblockcount(), start_height)
        block_hashes = []
        for i in range(9):
            block_hashes.append(node1.generate(1)[0])

        # Should receive an inv instead of hdrsen, because hdrsen message would contain more than 8 headers
        spv_node.wait_for_inv([CInv(CInv.BLOCK, int(block_hashes[8],16))], 5)
        assert_equal(spv_node.hdrsen, [])
        assert_equal(node.getblockcount(), start_height+9)

        # Send gethdrsen to get the missing headers
        spv_node.gethdrsen(locator=[int(block0_hash,16), int(h.headers[7].hash,16)], hashstop=0) # NOTE: h still contains the last blocks announcement before reorg
        h = spv_node.wait_for_hdrsen()
        assert_equal(len(h.headers), 9)
        for i in range(9):
            hi=h.headers[i]
            assert(hi.coinbaseTxProof)
            assert(not hi.minerInfoProof)
            self.check_TSCMerkleProof(hi)
            assert_equal(hi.hash, block_hashes[i])
            if i==8:
                assert_equal(hi.noMoreHeaders, 1)
            else:
                assert_equal(hi.noMoreHeaders, 0)
        assert_equal(spv_node.hdrsen, [])

        # Should receive announcement for next block in form of hdrsen message
        last_block_hash = node1.generate(1)[0] # should work the same even if block is mined by a different node
        h = spv_node.wait_for_hdrsen()
        assert_equal(len(h.headers), 1)
        h0=h.headers[0]
        assert(h0.coinbaseTxProof)
        assert(not h0.minerInfoProof)
        self.check_TSCMerkleProof(h0)
        assert_equal(h0.hash, last_block_hash)
        assert_equal(h0.noMoreHeaders, 1)
        assert(h0.coinbaseTxProof is not None)

        # Message sendheaders should override sendhdrsen
        spv_node.send_and_ping(msg_sendheaders())
        block10 = self.generate_block(node)
        spv_node.wait_for_headers(5)
        h = spv_node.last_message.get("headers") # should receive 'headers' message
        assert_equal(len(h.headers), 1)
        h.headers[0].rehash()
        assert_equal(h.headers[0].hash, block10.hash)
        assert_equal(spv_node.hdrsen, []) # there should be no 'hdrsen' message

        # Check that sending sendhdrsen several times increases peer ban score
        peerInfoPreSendHeaderMsg = spv_node.get_peer_info(node)
        for i in range(3):
            spv_node.sendhdrsen()
            time.sleep(1)
        peerInfoPostSendHeaderMsg = spv_node.get_peer_info(node)
        assert_greater_than(peerInfoPostSendHeaderMsg['banscore'], peerInfoPreSendHeaderMsg['banscore'])


if __name__ == '__main__':
    SendHdrsEnTest().main()
