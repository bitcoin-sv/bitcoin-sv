#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test genesis activation height.
Test accepts block that tries to spend transaction with OP_RETURN which was mined after genesis
and rejects block that tries to spend transaction with OP_RETURN which was mined before genesis.
Scenario:
Genesis height is 108.

1. Create block on height 106.
2. Create block on height 107 with tx0 that has OP_RETURN in the locking script and tx1 that
   has OP_TRUE in the unlocking script. Block is rejected because it contains tx1.
   Tx1 spends an unspendable transaction because tx0 contains OP_RETURN (such transactions can be spent after genesis activation)
3. Rewind to block 106.
4. Create block on height 107 (genesis NOT activated).
5. Create block on height 108 and try to spend from block on height 107. It is rejected.
6. Rewind to block 107.
2. Create block on height 108 with tx0 that has OP_RETURN in the locking script and tx1 that
   has OP_TRUE in the unlocking script. Block is accepted.
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_RETURN, OP_TRUE
from test_framework.blocktools import create_transaction
from test_framework.util import assert_equal
from test_framework.comptool import TestManager, TestInstance, RejectResult

class BSVGenesisActivation(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 108
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight]]

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block

        node = self.nodes[0]
        self.chain.set_genesis_hash( int(node.getbestblockhash(), 16) )

        # Create a new block
        block(0)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(104):
            block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(100):
            out.append(self.chain.get_spendable_output())

        # Block on height 106.
        block(1, spend=out[0])
        yield self.accepted()

        # Create block on height 107 (genesis NOT activated).
        block(2, spend=out[1])
        tx0 = create_transaction(out[2].tx, out[2].n, b"", 100000, CScript([OP_RETURN]))
        self.chain.update_block(2, [tx0])
        # \x51 is OP_TRUE
        tx1 = create_transaction(tx0, 0, b'\x51', 1, CScript([OP_TRUE]))
        b107_rejected = self.chain.update_block(2, [tx1])
        self.log.info("Created block %s on height %d that tries to spend from block on height %d.",
            b107_rejected.hash, self.genesisactivationheight-1, self.genesisactivationheight-1)
        yield self.rejected(RejectResult(16, b'bad-txns-inputs-missingorspent'))

        # Rewind bad block (height is 106).
        self.chain.set_tip(1)

        # Create block on height 107 (genesis NOT activated).
        block(3, spend=out[3])
        tx0 = create_transaction(out[4].tx, out[4].n, b"", 100000, CScript([OP_RETURN]))
        b107 = self.chain.update_block(3, [tx0])
        self.log.info("Created block %s on height %d.", b107.hash, self.genesisactivationheight - 1)
        yield self.accepted()

        # Create block on height 108 and try to spend from block on height 107
        block(4, spend=out[5])
        # \x51 is OP_TRUE
        tx1 = create_transaction(tx0, 0, b'\x51', 1, CScript([OP_TRUE]))
        b108_rejected = self.chain.update_block(4, [tx1])
        self.log.info("Created block %s on height %d that tries to spend from block on height %d.",
            b108_rejected.hash, self.genesisactivationheight, self.genesisactivationheight - 1)
        yield self.rejected(RejectResult(16, b'bad-txns-inputs-missingorspent'))

        # Rewind bad block (height is 107).
        self.chain.set_tip(3)

        # Create block on height 108 and try to spend from block on height 108.
        block(5, spend=out[6])
        tx0 = create_transaction(out[7].tx, out[7].n, b"", 100000, CScript([OP_RETURN]))
        self.chain.update_block(5, [tx0])
        # \x51 is OP_TRUE
        tx1 = create_transaction(tx0, 0, b'\x51', 1, CScript([OP_TRUE]))
        b108_accepted = self.chain.update_block(5, [tx1])
        self.log.info("Created block %s on height %d that tries to spend from block on height %d.",
            b108_accepted.hash, self.genesisactivationheight, self.genesisactivationheight)
        yield self.accepted()

if __name__ == '__main__':
    BSVGenesisActivation().main()
