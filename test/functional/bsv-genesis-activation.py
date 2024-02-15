#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test genesis activation height.
Test accepts block that tries to spend transaction with OP_RETURN which was mined after genesis
and rejects block that tries to spend transaction with OP_RETURN which was mined before genesis.
Scenario:
Genesis height is 109.

1. Create block on height 107.
2. Create block on height 108 with tx0 that has OP_RETURN in the locking script and tx1 that
   has OP_TRUE in the unlocking script. Block is rejected because it contains tx1.
   Tx1 spends an unspendable transaction because tx0 contains OP_RETURN (such transactions can be spent after genesis activation)
3. Rewind to block 107.
4. Create block on height 108 (genesis NOT activated).
5. Create block on height 109 and try to spend from block on height 108. It is rejected.
6. Rewind to block 108.
7. Create block on height 109 with tx0 that has OP_RETURN in the locking script and tx1 that
   has OP_TRUE in the unlocking script. Block is accepted.

8. Invalidate block 109. tx0 and tx1 are added to mempool.
9. Invalidate block 108. tx0 and tx1 are deleted from mempool (crossing Genesis).
10. Try to generate new block (height 108). Generating is successful, tx0 and tx1 are not in this block.

"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_RETURN, OP_TRUE
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal, assert_raises_rpc_error, hashToHex
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.mininode import msg_tx
from time import sleep


class BSVGenesisActivation(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 109
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight]]

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block

        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 105, 100)

        yield test

        # Block with height 107.
        block(1, spend=out[0])
        yield self.accepted()

        # Create block with height 108 (genesis NOT activated).
        block(2, spend=out[1])
        tx0 = create_transaction(out[2].tx, out[2].n, b"", 100000, CScript([OP_RETURN]))
        self.chain.update_block(2, [tx0])
        # \x51 is OP_TRUE
        tx1 = create_transaction(tx0, 0, b'\x51', 1, CScript([OP_TRUE]))
        b108_rejected = self.chain.update_block(2, [tx1])
        self.log.info("Created block %s on height %d that tries to spend from block on height %d.",
                      b108_rejected.hash,
                      self.genesisactivationheight-1,
                      self.genesisactivationheight-1)
        yield self.rejected(RejectResult(16, b'bad-txns-inputs-missingorspent'))

        # Rewind bad block (height is 107).
        self.chain.set_tip(1)

        # Create block on height 108 (genesis NOT activated).
        block(3, spend=out[3])
        tx0 = create_transaction(out[4].tx, out[4].n, b"", 100000, CScript([OP_RETURN]))
        b108 = self.chain.update_block(3, [tx0])
        self.log.info("Created block %s on height %d.", b108.hash, self.genesisactivationheight - 1)
        yield self.accepted()

        # Create block on height 109 and try to spend from block on height 108
        block(4, spend=out[5])
        # \x51 is OP_TRUE
        tx1 = create_transaction(tx0, 0, b'\x51', 1, CScript([OP_TRUE]))
        b109_rejected = self.chain.update_block(4, [tx1])
        self.log.info("Created block %s on height %d that tries to spend from block on height %d.",
                      b109_rejected.hash,
                      self.genesisactivationheight,
                      self.genesisactivationheight - 1)
        yield self.rejected(RejectResult(16, b'bad-txns-inputs-missingorspent'))

        # Rewind bad block (height is 108).
        self.chain.set_tip(3)

        # Create block on height 109 and try to spend from block on height 109.
        block(5, spend=out[6])
        tx0 = create_transaction(out[7].tx, out[7].n, b"", 100000, CScript([OP_RETURN]))

        self.chain.update_block(5, [tx0])
        # \x51 is OP_TRUE
        tx1 = create_transaction(tx0, 0, b'\x51', 1, CScript([OP_TRUE]))
        b109_accepted = self.chain.update_block(5, [tx1])
        self.log.info("Created block %s on height %d that tries to spend from block on height %d.",
                      b109_accepted.hash,
                      self.genesisactivationheight,
                      self.genesisactivationheight)
        yield self.accepted()

        #############

        # At this point, we have tx0 and tx1 in cache script marked as valid.
        # Now, invalidate blocks 109 and 108 so that we are in state before genesis.

        node.invalidateblock(hashToHex(b109_accepted.sha256))
        sleep(1)

        # tx0 and tx1 are in mempool (currently valid because it was sent after genesis)
        assert_equal(True, tx0.hash in node.getrawmempool())
        assert_equal(True, tx1.hash in node.getrawmempool())

        node.invalidateblock(hashToHex(b108.sha256))

        # tx0 and tx1 are not in mempool (mempool is deleted when 108 is invalidated)
        assert_equal(False, tx0.hash in node.getrawmempool())
        assert_equal(False, tx1.hash in node.getrawmempool())

        # So we are at height 107.
        assert_equal(node.getblock(node.getbestblockhash())['height'], 107)

        self.nodes[0].generate(1)
        tx = self.nodes[0].getblock(self.nodes[0].getbestblockhash())['tx']
        assert_equal(False, tx0.hash in tx)
        assert_equal(False, tx1.hash in tx)

        # Now we are at height 108.
        assert_equal(node.getblock(node.getbestblockhash())['height'], 108)


if __name__ == '__main__':
    BSVGenesisActivation().main()
