#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2018 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test that the new default 128Mb blocks activates correctly in conjunction
with the excessiveblocksize parameter.

In short; after the Nov 15th HF date the default max block size should become
128Mb, unless the user has explictly set the max block size via the
excessiveblocksize parameter, in which case we should continue to honor
that value.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.util import assert_equal
from test_framework.cdefs import (ONE_MEGABYTE)

# Far into the future
MAGNETIC_START_TIME = 2000000000

# Test the HF activation of 128Mb blocks
class BSV128MbActivation(ComparisonTestFramework):

    # Can either run this test as 1 node with expected answers, or two and compare them.
    # Change the "outcome" variable from each TestInstance object to only do
    # the comparison.

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.excessive_block_size = 64 * ONE_MEGABYTE
        self.extra_args = [['-whitelist=127.0.0.1',
                            "-magneticactivationtime=%d" % MAGNETIC_START_TIME,
                            "-excessiveblocksize=%d" % self.excessive_block_size]]

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option(
            "--runbarelyexpensive", dest="runbarelyexpensive", default=True)

    def run_test(self):
        self.nodes[0].setmocktime(MAGNETIC_START_TIME)
        self.test.run()

    def get_tests(self):

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
        bfork = block(5555, out[15], block_size=32 * ONE_MEGABYTE)
        bfork.nTime = MAGNETIC_START_TIME - 1
        self.chain.update_block(5555, [])
        yield self.accepted()

        # Get to one block of the Nov 15, 2018 HF activation
        for i in range(5):
            block(5100 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
        yield test

        # Check that the MTP is just before the configured fork point.
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     MAGNETIC_START_TIME - 1)

        # Before we acivate the Nov 15, 2018 HF, 64MB is the limit.
        # Oversized blocks will cause us to be disconnected
        assert(not self.test.test_nodes[0].closed)
        block(4444, spend=out[16], block_size=self.excessive_block_size + 1)
        self.test.connections[0].send_message(msg_block((self.chain.tip)))
        self.test.wait_for_disconnections()
        assert(self.test.test_nodes[0].closed)

        # Rewind bad block and remake connection to node
        self.chain.set_tip(5104)
        self.test.clear_all_connections()
        self.test.add_all_connections(self.nodes)
        NetworkThread().start()
        self.test.wait_for_verack()

        # Activate the Nov 15, 2018 HF
        block(5556)
        yield self.accepted()

        # Now MTP is exactly the fork time. Bigger blocks are now accepted.
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     MAGNETIC_START_TIME)

        # Block of maximal size (excessiveblocksize)
        block(17, spend=out[18], block_size=self.excessive_block_size)
        yield self.accepted()

        # Oversized blocks will cause us to be disconnected
        assert(not self.test.test_nodes[0].closed)
        block(18, spend=out[19], block_size=self.excessive_block_size + 1)
        self.test.connections[0].send_message(msg_block((self.chain.tip)))
        self.test.wait_for_disconnections()
        assert(self.test.test_nodes[0].closed)

        # Rewind bad block and remake connection to node
        self.chain.set_tip(17)
        self.test.clear_all_connections()
        self.test.add_all_connections(self.nodes)
        NetworkThread().start()
        self.test.wait_for_verack()

        # Check we can still mine a good size block
        block(5557, spend=out[20], block_size=self.excessive_block_size)
        yield self.accepted()


if __name__ == '__main__':
    BSV128MbActivation().main()
