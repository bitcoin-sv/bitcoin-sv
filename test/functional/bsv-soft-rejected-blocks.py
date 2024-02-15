#!/usr/bin/env python3
# Copyright (c) 2021  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

import shutil

from test_framework.blocktools import (create_block, create_coinbase, ChainManager, prepare_init_chain,
                                       create_block_from_candidate, create_transaction)
from test_framework.mininode import ToHex, msg_block, COIN, FromHex, CTransaction, CTxOut
from test_framework.script import CScript, OP_TRUE, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (wait_until, assert_raises_rpc_error, assert_equal, sync_blocks, get_datadir_path,
                                 disconnect_nodes_bi, connect_nodes_bi)
"""
Test rpc calls softrejectblock, acceptblock and getsoftrejectedblocks.

Rpc call getsoftrejectedblocks is tested by calling auxiliary test functions
(soft_rej_blocks_hashes and  marked_as_soft_rej_blocks_hashes).
"""


class SoftRejectedBlocks(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[],[]]

    # Return a set containing chain tip hashes
    def chain_tips_hashes(self, conn):
        chain_tips = conn.getchaintips()
        hashes = set()
        for tip in chain_tips:
            hashes.add(tip["hash"])
        return hashes

    def wait_for_chain_tips(self, conn, hashes):
        assert(type(hashes)==type(set())) # hashes parameter must be a set
        wait_until(lambda: self.chain_tips_hashes(conn) == hashes)

    # Return a set containing hashes of all soft rejected blocks
    def soft_rej_blocks_hashes(self, conn):
        soft_rej_blocks = conn.getsoftrejectedblocks(False)
        hashes = set()
        for tip in soft_rej_blocks:
            hashes.add(tip["blockhash"])
        return hashes

    # Return a set containing hashes of blocks that were explicitly marked as soft rejected
    def marked_as_soft_rej_blocks_hashes(self, conn):
        soft_rej_blocks = conn.getsoftrejectedblocks()
        hashes = set()
        for tip in soft_rej_blocks:
            hashes.add(tip["blockhash"])
        return hashes

    # Reinitialize all nodes as if the test script has just started.
    def begin_new_test(self):
        self.log.debug("Stopping nodes to prepare for next test ...")
        self.stop_nodes()

        self.log.debug("Clear all node data")
        for i in range(self.num_nodes):
            node_dir = get_datadir_path(self.options.tmpdir, i)
            shutil.rmtree(node_dir)
        self.nodes = []

        if self.setup_clean_chain:
            self._initialize_chain_clean()
        else:
            self._initialize_chain()

        self.log.debug("Starting nodes for next test ...")
        self.setup_network()

    def test_general_1(self):
        """
            General test with the new rpc methods.
        """

        # Valid block
        b1_hash = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes)

        # Block that will be considered soft rejected only on node0
        b2_hash = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes)

        # Consider block b2 soft rejected until it has more than 1 descendant on the same chain.
        self.nodes[0].softrejectblock(b2_hash, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b1_hash, b2_hash}) # now there should be two chains
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.nodes[1].getblockcount(), 2) # must have no effect on node1
        assert_equal(self.nodes[1].getbestblockhash(), b2_hash)
        # This block must be in list of blocks considered soft rejected and explicitly marked as such.
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Check other data (besides block hash) returned by getsoftrejectedblocks
        assert_equal(self.nodes[0].getsoftrejectedblocks(True), [{
            'blockhash': b2_hash,
            'height': 2,
            'previousblockhash': b1_hash,
            'numblocks': 1
        }])

        # Generate another block on node1
        b3_hash = self.nodes[1].generate(1)[0]
        # Wait for node0 to receive that block and it is on a separate chain
        self.wait_for_chain_tips(self.nodes[0], {b1_hash, b3_hash})
        # This block is still considered soft rejected on node0 and must not be present on active chain
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.nodes[1].getblockcount(), 3)
        assert_equal(self.nodes[1].getbestblockhash(), b3_hash) # must have no effect on node1
        # Now list of blocks considered soft rejected contains two blocks ...
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash})
        # ... but only one is explictly marked as soft rejected.
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Generate another block on node1, this one as well as b2 and b3 should be again valid on node0
        b4_hash = self.nodes[1].generate(1)[0]
        # Wait for node0 to receive that block and it is again on a main chain
        self.wait_for_chain_tips(self.nodes[0], {b4_hash})
        assert_equal(self.nodes[0].getblockcount(), 4)
        assert_equal(self.nodes[0].getbestblockhash(), b4_hash)
        assert_equal(self.nodes[1].getblockcount(), 4)
        assert_equal(self.nodes[1].getbestblockhash(), b4_hash)
        # List of blocks considered soft rejected should not change
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Unmark block b2 as soft rejected
        self.nodes[0].acceptblock(b2_hash)
        # Should have no effect because this block no longer affects the tip of the chain
        assert_equal(self.nodes[0].getblockcount(), 4)
        assert_equal(self.nodes[0].getbestblockhash(), b4_hash)
        # List of blocks considered soft rejected should now be empty
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), set())

        # Consider block b2 soft rejected until it has more than 2 descendants on the same chain.
        self.nodes[0].softrejectblock(b2_hash, 2)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b1_hash, b4_hash}) # now there should be again two chains
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.nodes[1].getblockcount(), 4) # must have no effect on node1
        assert_equal(self.nodes[1].getbestblockhash(), b4_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash, b4_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Check other data (besides block hash) returned by getsoftrejectedblocks
        for b in self.nodes[0].getsoftrejectedblocks(False):
            # NOTE: Must check each one separately, because the order in array is unspecified
            h = b["blockhash"]
            if h == b2_hash:
                assert_equal(b, {
                    'blockhash': b2_hash,
                    'height': 2,
                    'previousblockhash': b1_hash,
                    'numblocks': 2
                })
                continue
            if h == b3_hash:
                assert_equal(b, {
                    'blockhash': b3_hash,
                    'height': 3,
                    'previousblockhash': b2_hash,
                    'numblocks': 1
                })
                continue
            if h == b4_hash:
                assert_equal(b, {
                    'blockhash': b4_hash,
                    'height': 4,
                    'previousblockhash': b3_hash,
                    'numblocks': 0
                })
                continue
            raise AssertionError("Unexpected soft rejected block: "+h)

        # Reconsider block b2 soft rejected until it has more than 1 descendant on the same chain (decrease number of descendants).
        self.nodes[0].acceptblock(b2_hash, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b4_hash}) # now there should be only one chain
        assert_equal(self.nodes[0].getblockcount(), 4)
        assert_equal(self.nodes[0].getbestblockhash(), b4_hash)
        assert_equal(self.nodes[1].getblockcount(), 4) # must have no effect on node1
        assert_equal(self.nodes[1].getbestblockhash(), b4_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Consider block b2 soft rejected until it has more than 2 descendants on the same chain (increase number of descendants).
        self.nodes[0].softrejectblock(b2_hash, 2)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b1_hash, b4_hash}) # now there should be two chains
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.nodes[1].getblockcount(), 4) # must have no effect on node1
        assert_equal(self.nodes[1].getbestblockhash(), b4_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash, b4_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Unmark block b2 as soft rejected
        self.nodes[0].acceptblock(b2_hash)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b4_hash}) # now there should be one chain again
        assert_equal(self.nodes[0].getblockcount(), 4)
        assert_equal(self.nodes[0].getbestblockhash(), b4_hash)
        assert_equal(self.nodes[1].getblockcount(), 4) # must have no effect on node1
        assert_equal(self.nodes[1].getbestblockhash(), b4_hash)
        # List of blocks considered soft rejected should now be empty
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), set())

    def test_general_2(self):
        """
            General test with the new rpc methods.
        """

        # we cant mark genesis block as soft rejected
        genesis_hash = self.nodes[0].getblockhash(0)
        assert_raises_rpc_error(-1, "Error marking block as soft rejected", self.nodes[
            0].softrejectblock, genesis_hash, 0)

        b1_hash = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes)
        self.nodes[0].softrejectblock(b1_hash, 0)
        assert_equal(self.nodes[0].getbestblockhash(), genesis_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b1_hash})

        self.nodes[0].acceptblock(b1_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())

        self.nodes[0].softrejectblock(b1_hash, 0)
        assert_equal(self.nodes[0].getbestblockhash(), genesis_hash)

        b2_hash = self.nodes[1].generate(1)[0]
        # assert b2 is active on all nodes
        sync_blocks(self.nodes)

        self.nodes[0].softrejectblock(b2_hash, 1)
        assert_equal(self.nodes[0].getbestblockhash(), genesis_hash)
        b3_hash = self.nodes[1].generate(1)[0]

        def wait_for_valid_header(blockhash):
            def wait_predicate():
                for tips in self.nodes[0].getchaintips():
                    if tips["status"] == "valid-headers" and tips["hash"] == blockhash:
                        return True
                return False
            wait_until(wait_predicate, check_interval=0.15)

        wait_for_valid_header(b3_hash)

        b4_hash = self.nodes[1].generate(1)[0]
        # assert b4 is active on all nodes
        sync_blocks(self.nodes)

        self.nodes[0].softrejectblock(b4_hash, 4)
        assert_equal(self.nodes[0].getbestblockhash(), genesis_hash)
        blockhashes = self.nodes[1].generate(4)
        b7_hash = blockhashes[2]
        b8_hash = blockhashes[3]

        wait_for_valid_header(b8_hash)

        b9_hash = self.nodes[1].generate(1)[0]
        # assert b9 is active on all nodes
        sync_blocks(self.nodes)

        # marking b8_hash or b7_hash will raise error, because these blocks are already considered soft
        # rejected by call softrejectblock(b4_hash, 4)
        assert_raises_rpc_error(-1, "Error marking block as soft rejected", self.nodes[
            0].softrejectblock, b8_hash, 1)
        assert_raises_rpc_error(-1, "Error marking block as soft rejected", self.nodes[
            0].softrejectblock, b7_hash, 5)

        # check we get error on block we didnt mark directly with softrejectblock
        assert_raises_rpc_error(-1, "Error unmarking block as soft rejected", self.nodes[
            0].acceptblock, b7_hash)
        assert_raises_rpc_error(-1, "Error unmarking block as soft rejected", self.nodes[
            0].acceptblock, b9_hash)

        self.nodes[0].acceptblock(b1_hash)
        self.nodes[0].acceptblock(b2_hash)

        # b3 cannot be marked as soft rejected for 1 or more blocks because that would
        # affect b4, which is already soft rejected
        assert_raises_rpc_error(-1, "Error marking block as soft rejected", self.nodes[
            0].softrejectblock, b3_hash, 1)

        # ... and the same for b2 but 2 or more blocks
        assert_raises_rpc_error(-1, "Error marking block as soft rejected", self.nodes[
            0].softrejectblock, b2_hash, 2)

        # b3 can be marked as soft rejected for 0 subsequent blocks
        self.nodes[0].softrejectblock(b3_hash, 0)
        self.nodes[0].acceptblock(b3_hash)

        # ... so can b2, but for 0 or 1 subsequent blocks
        self.nodes[0].softrejectblock(b2_hash, 0)
        self.nodes[0].softrejectblock(b2_hash, 1)
        self.nodes[0].acceptblock(b2_hash)

        # finish cleaning up so that there are no soft rejected blocks
        self.nodes[0].acceptblock(b4_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())

        # for convenience check the tip will not move back if b2.height+n < b9.height (tip)
        self.nodes[0].softrejectblock(b2_hash, 6)
        assert_equal(self.nodes[0].getbestblockhash(), b9_hash)
        self.nodes[0].acceptblock(b2_hash)

        # check the tip moves back if we call softrejectblock on block with height
        # lower than the tip
        self.nodes[0].softrejectblock(b8_hash, 1)
        assert_equal(self.nodes[0].getbestblockhash(), b7_hash)
        self.nodes[0].acceptblock(b8_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b9_hash)

        self.nodes[0].softrejectblock(b4_hash, 5)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        # b10
        self.nodes[1].generate(1)
        sync_blocks(self.nodes)

        # check for case where we soft reject blocks for more than current height
        # calling again softrejectblock on marked block we can extend the interval
        self.nodes[0].softrejectblock(b4_hash, 6)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        # b11
        self.nodes[1].generate(1)
        sync_blocks(self.nodes)

        self.nodes[0].softrejectblock(b4_hash, 8)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        blockhashes = self.nodes[1].generate(2)
        b12_hash = blockhashes[0]
        b13_hash = blockhashes[1]
        sync_blocks(self.nodes)

        # although invalidateblock and reconsiderblock work independently of these two new rpc calls we still check few
        # interactions.
        # check interaction with invalidateblock
        self.nodes[0].invalidateblock(b13_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        # check we can use acceptblock on marked block to reduce number of blocks needed
        self.nodes[0].acceptblock(b4_hash, 6)
        assert_equal(self.nodes[0].getbestblockhash(), b12_hash)

        # check interaction with reconsider block
        self.nodes[0].softrejectblock(b4_hash, 8)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        self.nodes[0].reconsiderblock(b13_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b13_hash)

        # check invalidateblock can be still used on soft rejected blocks
        self.nodes[0].invalidateblock(b4_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        self.nodes[0].reconsiderblock(b4_hash)
        # check reconsiderblock does not reset status of softrejectblock
        self.nodes[0].softrejectblock(b4_hash, 15)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        self.nodes[0].reconsiderblock(b4_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)

    def test_with_submitblock(self):
        """
            1. mine blocks b1->b2 and call softrejectblock(b1_hash, 1)
            2. make block b3 with b2 as parent and send it via submitblock
            assert: b3 should be active
        """
        blockhashes = self.nodes[0].generate(2)
        b1_hash = blockhashes[0]
        b2_hash = blockhashes[1]

        tip = int(b2_hash, 16)
        tip_blocktime = self.nodes[0].getblockheader(b2_hash)["time"]
        height = self.nodes[0].getblockcount()
        b3 = create_block(tip, create_coinbase(height), tip_blocktime + 1)
        b3.nVersion = 3
        b3.hashMerkleRoot = b3.calc_merkle_root()
        b3.solve()
        b3.rehash()

        self.nodes[0].softrejectblock(b1_hash, 1)
        assert_equal(self.nodes[0].getblockcount(), 0)

        self.nodes[0].submitblock(ToHex(b3))
        assert_equal(self.nodes[0].getbestblockhash(), b3.hash)

    def test_with_submitminingsolution(self):
        """
            Verify getminincandidate and submitminingsolution
        """
        blockhashes = self.nodes[0].generate(5)
        b2_hash = blockhashes[1]
        b3_hash = blockhashes[2]
        self.nodes[0].softrejectblock(b3_hash, 2)
        assert_equal(self.nodes[0].getbestblockhash(), b2_hash)

        candidate = self.nodes[0].getminingcandidate(True)
        block, coinbase_tx = create_block_from_candidate(candidate, True)
        self.nodes[0].submitminingsolution({'id': candidate['id'], 'nonce': block.nNonce, 'coinbase': '{}'.format(ToHex(coinbase_tx))})
        assert_equal(self.nodes[0].getbestblockhash(), block.hash)
        assert_equal(self.nodes[0].getblockcount(), 3)

    def test_with_preciousblock(self):
        """
            Test softrejectblock in combination with precious block
        """
        self.nodes[0].generate(1)
        sync_blocks(self.nodes)

        disconnect_nodes_bi(self.nodes, 0, 1)

        blockhashes_node0 = self.nodes[0].generate(4)
        b2_hash = blockhashes_node0[0]
        b5_hash = blockhashes_node0[3]
        blockhashes_node1 = self.nodes[1].generate(3)
        b4a_hash = blockhashes_node1[2]

        connect_nodes_bi(self.nodes, 0, 1)
        self.wait_for_chain_tips(self.nodes[0], {b5_hash, b4a_hash})

        self.nodes[0].softrejectblock(b2_hash, 3)
        self.nodes[0].waitforblockheight(4)
        assert_equal(self.nodes[0].getbestblockhash(), b4a_hash)

        # call preciousblock to verify it will not delete block candidates with soft rejected status.
        self.nodes[0].preciousblock(b4a_hash)
        # reconsider b2_hash to verify we are still able to reach previous longest chain.
        self.nodes[0].acceptblock(b2_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b5_hash)

    def test_parallelchains(self):
        """
            1. Verify preciousblock does not activate soft rejected block.
            2. Test marking of blocks with softrejectblock with two parallel chains.
        """
        b1_hash = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes)

        disconnect_nodes_bi(self.nodes, 0, 1)

        blockhashes_node0 = self.nodes[0].generate(2)
        b2_hash = blockhashes_node0[0]
        b3_hash = blockhashes_node0[1]
        blockhashes_node1 = self.nodes[1].generate(2)
        b2a_hash = blockhashes_node1[0]
        b3a_hash = blockhashes_node1[1]

        connect_nodes_bi(self.nodes, 0, 1)
        self.wait_for_chain_tips(self.nodes[0], {b3_hash, b3a_hash})

        # 1. Verify preciousblock does not activate soft rejected block.
        self.nodes[0].softrejectblock(b2_hash, 1)
        self.nodes[0].waitforblockheight(3)
        assert_equal(self.nodes[0].getbestblockhash(), b3a_hash)
        self.nodes[0].preciousblock(b3_hash)
        assert_equal(self.nodes[0].getbestblockhash(), b3a_hash)

        self.nodes[0].acceptblock(b2_hash)

        # 2. Test marking of blocks with softrejectblock with two parallel chains.
        self.nodes[0].softrejectblock(b1_hash, 3)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b1_hash, b2_hash, b2a_hash, b3_hash, b3a_hash})
        self.nodes[1].generate(2)
        sync_blocks(self.nodes)

    def test_generate_on_currenttip(self):
        """
            Verify generate will build on current tip
        """
        b1_hash = self.nodes[0].generate(1)[0]
        self.nodes[0].softrejectblock(b1_hash, 0)
        assert_equal(self.nodes[0].getblockcount(), 0)
        b1a_hash = self.nodes[0].generate(1)[0]
        assert_equal(self.nodes[0].getbestblockhash(), b1a_hash)
        b2a_hash = self.nodes[0].generate(1)[0]
        assert_equal(self.nodes[0].getblockheader(b2a_hash)["previousblockhash"], b1a_hash)

    def test_persistence(self):
        """
            Verify that status of soft rejected blocks is properly stored on disk
            and it remains unchanged if node is restarted.
        """

        # Valid block
        b1_hash = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes)

        # Blocks that will be marked as soft rejected on node0 at some point
        b2_hash = self.nodes[1].generate(1)[0]
        b3_hash = self.nodes[1].generate(1)[0]
        sync_blocks(self.nodes)

        # Mark block 2 as soft rejected so that it does not affect the best chain tip
        self.nodes[0].softrejectblock(b2_hash, 0)
        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b3_hash})
        assert_equal(self.nodes[0].getblockcount(), 3)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Extend soft rejection of block 2 so that it does affect the chain tip
        self.nodes[0].softrejectblock(b2_hash, 1)
        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b1_hash, b3_hash})
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Reduce soft rejection of block 2 so that it longer affects the chain tip
        self.nodes[0].acceptblock(b2_hash, 0)
        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b3_hash})
        assert_equal(self.nodes[0].getblockcount(), 3)
        assert_equal(self.nodes[0].getbestblockhash(), b3_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash})

        # Mark block 3 as soft rejected
        self.nodes[0].softrejectblock(b3_hash, 2)
        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b1_hash, b3_hash})
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash})

        # Generate another block on node1, which should implicitly be considered soft rejected on node0
        b4_hash = self.nodes[1].generate(1)[0]
        self.wait_for_chain_tips(self.nodes[0], {b1_hash, b4_hash})
        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b1_hash, b4_hash})
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash, b4_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b2_hash, b3_hash})

        # Unmark block 2 as soft rejected
        self.nodes[0].acceptblock(b2_hash)
        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b2_hash, b4_hash})
        assert_equal(self.nodes[0].getblockcount(), 2)
        assert_equal(self.nodes[0].getbestblockhash(), b2_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), {b3_hash, b4_hash})
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), {b3_hash})

        # Unmark block 3 as soft rejected
        self.nodes[0].acceptblock(b3_hash)
        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.chain_tips_hashes(self.nodes[0]), {b4_hash})
        assert_equal(self.nodes[0].getblockcount(), 4)
        assert_equal(self.nodes[0].getbestblockhash(), b4_hash)
        assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())
        assert_equal(self.marked_as_soft_rej_blocks_hashes(self.nodes[0]), set())

    def test_blocktree(self):
        """
            Test soft rejection block status in non-trivial tree of blocks
        """

        # Create a P2P connection to node0 that will be used to send blocks
        self.stop_node(0)
        with self.run_node_with_connections(title="test_blocktree",
                                            node_index=0,
                                            args=["-whitelist=127.0.0.1"], # Need to whilelist localhost, so that node accepts any block
                                            number_of_connections=1) as connections:
            conn0 = connections[0]

            # Create the following tree of blocks:
            #     genesis
            #        |
            #   1001...1200
            #        |
            #        1    height=201
            #       / \
            #      2   8
            #     /|\   \
            #    3 4 6   9
            #      | |
            #      5 7
            chain = ChainManager()
            genesis_hash = self.nodes[0].getbestblockhash()
            chain.set_genesis_hash(int(genesis_hash, 16))
            _, out, _ = prepare_init_chain(chain, 200, 12, block_0=False, start_block=1001, node=conn0.cb)
            conn0.cb.sync_with_ping()

            # Check that we have created a chain that we wanted
            assert_equal(self.nodes[0].getblockcount(), 200)
            assert_equal(self.nodes[0].getblock(chain.blocks[1001].hash)["height"], 1)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[1200].hash)
            assert_equal(self.nodes[0].getblock(chain.blocks[1200].hash)["height"], 200)

            def new_blk(idx, prev_idx):
                chain.set_tip(prev_idx)
                # spend output with the same index as block
                # NOTE: We don't really care about spending outputs in this test.
                #       Spending different outputs is only used as a convenient way
                #       to make two blocks different if they have the same parent.
                b = chain.next_block(idx, spend=out[idx])
                conn0.cb.send_message(msg_block(b)) # send block to node
                conn0.cb.sync_with_ping() # wait until node has processed the block
                self.log.debug("Created block: idx=%i prev=%i hash=%s" % (idx, prev_idx, b.hash))

            new_blk(1, 1200)
            new_blk(2, 1)
            new_blk(3, 2)
            new_blk(4, 2)
            new_blk(5, 4)
            new_blk(6, 2)
            new_blk(7, 6)
            new_blk(8, 1)
            new_blk(9, 8)

            # Block 5 should be tip of the active chain (it its highest and was received before 7, which is at the same height)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[5].hash)

            # If 5 is soft rejected, 7 should become best
            self.nodes[0].softrejectblock(chain.blocks[5].hash, 0)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[7].hash)

            # If 7 is also soft rejected, 3 should become best
            self.nodes[0].softrejectblock(chain.blocks[7].hash, 0)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[3].hash)

            # Reset state
            self.nodes[0].acceptblock(chain.blocks[5].hash)
            self.nodes[0].acceptblock(chain.blocks[7].hash)
            assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[5].hash)

            # If 2 is soft rejected for next two blocks, 9 should become best
            self.nodes[0].softrejectblock(chain.blocks[2].hash, 2)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[9].hash)

            # If we reconsider 2 to be soft rejected only for next one block, 5 should again become best
            self.nodes[0].acceptblock(chain.blocks[2].hash, 1)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[5].hash)

            # Reset state
            self.nodes[0].acceptblock(chain.blocks[2].hash)
            assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[5].hash)

            # If 1 is soft rejected for next 3 blocks, 1200 should become best
            self.nodes[0].softrejectblock(chain.blocks[1].hash, 3)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[1200].hash)

            # If 1 is soft rejected only until next block, 2 is soft rejected for 2 blocks and 9 is soft rejected until next block, 8 should become best
            self.nodes[0].acceptblock(chain.blocks[1].hash, 0)
            self.nodes[0].softrejectblock(chain.blocks[9].hash, 0)
            self.nodes[0].softrejectblock(chain.blocks[2].hash, 2)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[8].hash)

            # If we now receive a new block after 9, it should become best
            new_blk(10, 9)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[10].hash)

            # Reset state, block 5 should still be best
            self.nodes[0].acceptblock(chain.blocks[1].hash)
            self.nodes[0].acceptblock(chain.blocks[9].hash)
            self.nodes[0].acceptblock(chain.blocks[2].hash)
            assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[5].hash)

            # Soft rejecting block 1001 for next 202 blocks should have no effect
            self.nodes[0].softrejectblock(chain.blocks[1001].hash, 202)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[5].hash)

            # If block 1001 is soft rejected for next 203 blocks, genesis should become best
            self.nodes[0].softrejectblock(chain.blocks[1001].hash, 203)
            assert_equal(self.nodes[0].getbestblockhash(), genesis_hash)

            # If we now receive a new block after 5, it should become best
            new_blk(11, 5)
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[11].hash)

            # Reset state, block 11 should still be best
            self.nodes[0].acceptblock(chain.blocks[1001].hash)
            assert_equal(self.soft_rej_blocks_hashes(self.nodes[0]), set())
            assert_equal(self.nodes[0].getbestblockhash(), chain.blocks[11].hash)

    def test_invalidblock(self):
        """
            Verify situation when receiving invalid block, which is no longer considered soft rejected, via p2p
        """
        self.nodes[0].generate(101)

        # create spendable tx
        tx_spendable = CTransaction()
        tx_spendable.vout = [CTxOut(4500000000, CScript([OP_TRUE]))]
        tx_hex_funded = self.nodes[0].fundrawtransaction(ToHex(tx_spendable), {'changePosition' : len(tx_spendable.vout)})['hex']
        tx_hex = self.nodes[0].signrawtransaction(tx_hex_funded)['hex']
        self.nodes[0].sendrawtransaction(tx_hex, True)
        tx_spendable = FromHex(CTransaction(), tx_hex)
        tx_spendable.rehash()

        b1_hash = self.nodes[0].generate(1)[0]
        b2_hash = self.nodes[0].generate(1)[0]
        b3_hash = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes)

        self.nodes[0].softrejectblock(b2_hash, 1)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)
        assert_equal(self.nodes[0].getblockcount(), 102)

        # Create a P2P connection to node0 that will be used to send blocks
        self.stop_node(0)
        with self.run_node_with_connections(title="test_invalidblock",
                                            node_index=0,
                                            args=["-whitelist=127.0.0.1"],
                                            # Need to whilelist localhost, so that node accepts any block
                                            number_of_connections=1) as connections:
            conn0 = connections[0]
            self.nodes[0].waitforblockheight(102)

            # create and send block (child of b3) with coinbase tx that pays to much
            coinbase_tx = create_coinbase(103)
            coinbase_tx.vout[0].nValue = 60 * COIN
            coinbase_tx.rehash()
            b4_invalid = create_block(int(b3_hash, 16), coinbase_tx)
            b4_invalid.hashMerkleRoot = b4_invalid.calc_merkle_root()
            b4_invalid.solve()
            b4_invalid.rehash()
            conn0.cb.send_message(msg_block(b4_invalid))
            # b1 must still be at the tip
            self.wait_for_chain_tips(self.nodes[0], {b1_hash, b4_invalid.hash})
            wait_until(lambda: self.nodes[0].getbestblockhash() == b1_hash) # NOTE: need to wait, since reorg back to b1 can take a while even after chaintips are already as expected

            # create and send block b2 (child of b1) that creates a new chain
            b2a = create_block(int(b1_hash, 16), create_coinbase(102))
            b2a.solve()
            b2a.rehash()
            conn0.cb.send_message(msg_block(b2a))
            # b2a must become new tip
            self.wait_for_chain_tips(self.nodes[0], {b2a.hash, b4_invalid.hash})
            assert_equal(self.nodes[0].getbestblockhash(), b2a.hash)

            # create and send block (child of b3) containing an invalid txn
            b4a_invalid = create_block(int(b3_hash, 16), create_coinbase(103))
            b4a_invalid.vtx.append(create_transaction(tx_spendable, 0, CScript([OP_RETURN]), 100000)) # invalid unlock script
            b4a_invalid.hashMerkleRoot = b4a_invalid.calc_merkle_root()
            b4a_invalid.solve()
            b4a_invalid.rehash()
            conn0.cb.send_message(msg_block(b4a_invalid))
            # b2a must still be at the tip
            self.wait_for_chain_tips(self.nodes[0], {b2a.hash, b4_invalid.hash, b4a_invalid.hash})
            wait_until(lambda: self.nodes[0].getbestblockhash() == b2a.hash)

    def test_invalidblock2(self):
        """
            Verify we have the expected tip after calling acceptblock on chain
            where we received invalid block.

            b1->b2->b3; softrejectblock(b2,2); send b4_invalid made on top of b3
            acceptblock(b2); assert b3 is active
        """
        self.nodes[0].generate(101)

        # create spendable tx
        tx_spendable = CTransaction()
        tx_spendable.vout = [CTxOut(4500000000, CScript([OP_TRUE]))]
        tx_hex_funded = self.nodes[0].fundrawtransaction(ToHex(tx_spendable), {'changePosition' : len(tx_spendable.vout)})['hex']
        tx_hex = self.nodes[0].signrawtransaction(tx_hex_funded)['hex']
        self.nodes[0].sendrawtransaction(tx_hex, True)
        tx_spendable = FromHex(CTransaction(), tx_hex)
        tx_spendable.rehash()

        b1_hash = self.nodes[0].generate(1)[0]
        b2_hash = self.nodes[0].generate(1)[0]
        b3_hash = self.nodes[0].generate(1)[0]
        sync_blocks(self.nodes)

        self.nodes[0].softrejectblock(b2_hash, 2)
        assert_equal(self.nodes[0].getbestblockhash(), b1_hash)

        # Create a P2P connection to node0 that will be used to send blocks
        self.stop_node(0)
        with self.run_node_with_connections(title="test_acceptblock",
                                            node_index=0,
                                            args=["-whitelist=127.0.0.1"],
                                            # Need to whilelist localhost, so that node accepts any block
                                            number_of_connections=1) as connections:
            conn0 = connections[0]
            self.nodes[0].waitforblockheight(102)

            # create and send block (child of b3) containing an invalid txn
            b4a_invalid = create_block(int(b3_hash, 16), create_coinbase(105))
            b4a_invalid.vtx.append(create_transaction(tx_spendable, 0, CScript([OP_RETURN]), 100000)) # invalid unlock script
            b4a_invalid.hashMerkleRoot = b4a_invalid.calc_merkle_root()
            b4a_invalid.solve()
            b4a_invalid.rehash()
            conn0.cb.send_message(msg_block(b4a_invalid))

            # b1 must still be at the tip
            self.wait_for_chain_tips(self.nodes[0], {b1_hash, b4a_invalid.hash})
            wait_until(lambda: self.nodes[0].getbestblockhash() == b1_hash)

            # If b2 is unmarked as soft rejected, b3 must become tip
            self.nodes[0].acceptblock(b2_hash)
            assert_equal(self.nodes[0].getbestblockhash(), b3_hash)

    def run_test(self):
        self.log.info("Test: general 1")
        self.test_general_1()

        self.begin_new_test()
        self.log.info("Test: general 2")
        self.test_general_2()

        self.begin_new_test()
        self.log.info("Test: with submitblock")
        self.test_with_submitblock()

        self.begin_new_test()
        self.log.info("Test: with submitminingsolution")
        self.test_with_submitminingsolution()

        self.begin_new_test()
        self.log.info("Test: with preciousblock")
        self.test_with_preciousblock()

        self.begin_new_test()
        self.log.info("Test: parallel chains")
        self.test_parallelchains()

        self.begin_new_test()
        self.log.info("Test: generate on current tip")
        self.test_generate_on_currenttip()

        self.begin_new_test()
        self.log.info("Test: invalidblock")
        self.test_invalidblock()

        self.begin_new_test()
        self.log.info("Test: invalidblock 2")
        self.test_invalidblock2()

        self.begin_new_test()
        self.log.info("Test: perisistence")
        self.test_persistence()

        self.begin_new_test()
        self.log.info("Test: blocktree")
        self.test_blocktree()


if __name__ == '__main__':
    SoftRejectedBlocks().main()
