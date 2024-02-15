#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test excessiveblocksize that is a required parameter. Blocks should be accepted at set value.
Note we don't test oversized blocks as such peers are banned
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.util import assert_equal
from test_framework.cdefs import ONE_MEGABYTE


# Test the size of blocks
class BSVBlockSizeTest(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 105
        self.maxBlockSize = 10 * ONE_MEGABYTE
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight, '-rpcservertimeout=500', '-excessiveblocksize=%d' % self.maxBlockSize]]

    def run_test(self):
        # Increase timeout when testing with big blocks (mininode's handle_write is not very efficient)
        self.test.waitForPingTimeout = 360
        self.test.run()

    def get_tests(self):

        # shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, self.genesisactivationheight - 3, self.genesisactivationheight - 3)

        yield test

        # Current block height is 103
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 2)

        # Send proper block at height 104 that should be accepted
        block(1, spend=out[0], block_size=self.maxBlockSize)
        yield self.accepted()

        # Current block height is 104
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 1)

        # Send proper sized block that should be accepted
        block(2, spend=out[1], block_size=self.maxBlockSize)
        yield self.accepted()

        # Current block height is 105 - genesis
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight)


if __name__ == '__main__':
    BSVBlockSizeTest().main()
