#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that we spot whether the excessiveblocksize configuration parameter
is overridden in all cases.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.cdefs import (ONE_MEGABYTE)

import os


# Check excessiveblocksize configuration override
class BSVEbsOverridden(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.maxblocksize = 64 * ONE_MEGABYTE

    def test_fromcmdline(self):
        self.log.info("Testing setting excessiveblocksize from command line")

        # Stop node and restart with args
        self.stop_node(0)
        self.start_node(0, ["-excessiveblocksize=%d" % self.maxblocksize])
        res = self.nodes[0].getexcessiveblock()
        assert_equal(res["excessiveBlockSize"], self.maxblocksize)
        return True

    def test_fromrpc(self):
        self.log.info("Testing setting excessiveblocksize from RPC")

        # Stop node and restart
        self.stop_node(0)
        self.start_node(0, [])

        # Set via RPC
        self.nodes[0].setexcessiveblock(self.maxblocksize)
        res = self.nodes[0].getexcessiveblock()
        assert_equal(res["excessiveBlockSize"], self.maxblocksize)
        return True

    def test_fromfile(self):
        self.log.info("Testing setting excessiveblocksize from file")

        # Stop node, reconfigure and restart
        self.stop_node(0)
        filename = os.path.join(self.options.tmpdir + "/node0", "bitcoin.conf")
        with open(filename, 'a', encoding='utf8') as f:
            f.write("excessiveblocksize=" + str(self.maxblocksize) + "\n")
        self.start_node(0, [])
        res = self.nodes[0].getexcessiveblock()
        assert_equal(res["excessiveBlockSize"], self.maxblocksize)

        # Remove config file now we've finished with it
        os.remove(filename)
        return True

    def run_test(self):
        self.test_fromcmdline()
        self.test_fromrpc()
        self.test_fromfile()


if __name__ == '__main__':
    BSVEbsOverridden().main()
