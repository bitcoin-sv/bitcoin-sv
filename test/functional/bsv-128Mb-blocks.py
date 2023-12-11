#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
This test checks that by using the newly promoted -excessiveblocksize
flag we can actually process very big blocks.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
import time
from test_framework.script import *
from test_framework.cdefs import (ONE_MEGABYTE)


class BSV128MBlocks(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option("--excessiveblocksize", dest="excessive_block_size", default=128*ONE_MEGABYTE, type='int')

    def run_test(self):
        self.extra_args = [['-whitelist=127.0.0.1',
                            "-excessiveblocksize=%d" % self.options.excessive_block_size]]
        self.nodes[0].setexcessiveblock(self.options.excessive_block_size)
        self.test.run()

    def get_tests(self):
        self.log.info("Testing with -excessiveblocksize set to {} MB ({} bytes)"
                      .format((self.options.excessive_block_size/ONE_MEGABYTE),
                              self.options.excessive_block_size))

        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        # shorthand for functions
        block = self.chain.next_block
        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 99, 100)

        yield test

        # block of maximal size
        block(1, spend=out[0], block_size=self.options.excessive_block_size)
        yield self.accepted()

        # Oversized blocks will cause us to be disconnected
        assert(not self.test.test_nodes[0].closed)
        block(2, spend=out[1], block_size=self.options.excessive_block_size + 1)
        self.test.connections[0].send_message(msg_block((self.chain.tip)))
        self.test.wait_for_disconnections()
        assert(self.test.test_nodes[0].closed)

        # Rewind bad block and remake connection to node
        self.chain.set_tip(1)
        self.restart_network()
        self.test.wait_for_verack()

        # Check we can still mine a good size block
        block(3, spend=out[1], block_size=self.options.excessive_block_size)
        yield self.accepted()


if __name__ == '__main__':
    BSV128MBlocks().main()
