#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that the new default maximum accepted blocks activates correctly without the use
of the excessiveblocksize parameter.

In short; if the user doesn't override things via the excessiveblocksize
parameter then after the NEW_BLOCKSIZE_ACTIVATION_TIME date the default max accepted block 
size should increase to DEFAULT_MAX_BLOCK_SIZE_*_AFTER
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.util import assert_equal
from test_framework.cdefs import (ONE_MEGABYTE, ONE_GIGABYTE, REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME, GENESIS_ACTIVATION_HEIGHT_REGTEST, 
REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS, REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER_GENESIS, REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)


DEFAULT_ACTIVATION_TIME = REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME # can be overriden on command line
DEFAULT_GENESIS_ACTIVATION_HEIGHT = GENESIS_ACTIVATION_HEIGHT_REGTEST # can be overriden on command line
DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS = REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS
DEFAULT_MAX_BLOCK_SIZE_AFTER_GENESIS = REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER_GENESIS
DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER

# Test the HF activation of new size of blocks
class BSVBlockSizeActivationGenesis(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 105
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight]]

    def run_test(self):
        # Increase timeout when testing with big blocks (mininode's handle_write is not very efficient)
        self.test.waitForPingTimeout = 180
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
        for i in range(self.genesisactivationheight - 3):
            block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(self.genesisactivationheight - 3):
            out.append(self.chain.get_spendable_output())        

        # Current block height is 103
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 2)

        # Send oversized block at height 104 that should be rejected
        block(1, spend=out[0], block_size=DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS + 1)
        yield self.rejected(RejectResult(16, b'bad-blk-length'))

        # Rewind bad block.
        self.chain.set_tip(5101)
        
        # Current block height is 103
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 2)

        # Send one more block to activate genesis        
        block(2)
        yield self.accepted()

        # Current block height is 104
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 1)
        
        # Send oversized block that should be accepted
        block(3, spend=out[2], block_size=DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS + 1)
        yield self.accepted()

        # Current block height is 105 - genesis
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight)

if __name__ == '__main__':
    BSVBlockSizeActivationGenesis().main()
