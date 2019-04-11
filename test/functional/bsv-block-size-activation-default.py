#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
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
from test_framework.cdefs import (ONE_MEGABYTE, REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME, 
REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE, REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER, REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)


DEFAULT_ACTIVATION_TIME = REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME # can be overriden on command line
DEFAULT_MAX_BLOCK_SIZE_BEFORE = REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE
DEFAULT_MAX_BLOCK_SIZE_AFTER = REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER
DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER

# Test the HF activation of new size of blocks
class BSVBlockSizeActivation(ComparisonTestFramework):

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option("--blocksizeactivationtime", dest="blocksizeactivationtime", type='int')


    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True        
        self.extra_args = [['-whitelist=127.0.0.1' ]]

    def setup_network(self):
        # Append -blocksizeactivationtime only if explicitly specified
        # We can not do this in set_test_params, because self.options has yet initialized there
        self.activation_time = DEFAULT_ACTIVATION_TIME
        if (self.options.blocksizeactivationtime is not None):
            self.activation_time = self.options.blocksizeactivationtime
            self.extra_args[0].append("-blocksizeactivationtime=%d" % self.options.blocksizeactivationtime)

        super().setup_network()

    def run_test(self):
        activation_time = self.activation_time
        self.log.info("Using activation time %d " % activation_time)

        self.nodes[0].setmocktime(activation_time)
        self.test.run()

    def get_tests(self):
        activation_time = self.activation_time

        # Test no excessiveblocksize parameter specified
        node = self.nodes[0]
        self.chain.set_genesis_hash( int(node.getbestblockhash(), 16) )

        # shorthand for functions
        block = self.chain.next_block

        # Create a new block
        block(0)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(99):
            block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(100):
            out.append(self.chain.get_spendable_output())

        # Let's build some blocks and test them.
        for i in range(15):
            n = i + 1
            block(n, spend=out[i], block_size=n * ONE_MEGABYTE)
            yield self.accepted()
        
        # Start moving MTP forward
        bfork = block(5555, out[15], block_size=DEFAULT_MAX_BLOCK_SIZE_BEFORE)
        bfork.nTime = activation_time - 1
        self.chain.update_block(5555, [])
        yield self.accepted()

        # Get to one block of the activation
        for i in range(5):
            block(5100 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
        yield test

        # Check that the MTP is just before the configured fork point.
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     activation_time - 1)

        # Before we acivate the DEFAULT_MAX_BLOCK_SIZE_BEFORE  is the limit.
        block(4444, spend=out[16], block_size=DEFAULT_MAX_BLOCK_SIZE_BEFORE + 1)
        yield self.rejected(RejectResult(16, b'bad-blk-length'))

        # Rewind bad block.
        self.chain.set_tip(5104)

        # Activate the new rules
        block(5556)
        yield self.accepted()

        # Now MTP is exactly the fork time. Bigger blocks are now accepted.
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     activation_time)

        # block of maximal size
        block(17, spend=out[16], block_size=DEFAULT_MAX_BLOCK_SIZE_AFTER)
        yield self.accepted()

        # Oversized blocks will cause us to be disconnected
        assert(not self.test.test_nodes[0].closed)
        block(18, spend=out[17], block_size=DEFAULT_MAX_BLOCK_SIZE_AFTER + 1)
        self.test.connections[0].send_message(msg_block((self.chain.tip)))
        self.test.wait_for_disconnections()
        assert(self.test.test_nodes[0].closed)

        # Rewind bad block and remake connection to node
        self.chain.set_tip(17)
        self.restart_network()
        self.test.wait_for_verack()

        # Check we can still mine a good size block (the default generated block size after the 
        # activation - not the largest accepted()
        block(5557, spend=out[18], block_size=DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)
        yield self.accepted()


if __name__ == '__main__':
    BSVBlockSizeActivation().main()
