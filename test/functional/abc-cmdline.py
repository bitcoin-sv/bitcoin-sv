#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Exercise the command line functions specific to ABC functionality.
Currently:

-excessiveblocksize=<blocksize_in_bytes>
"""

import re
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.cdefs import LEGACY_MAX_BLOCK_SIZE, REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER, ONE_MEGABYTE

MAX_GENERATED_BLOCK_SIZE_ERROR = (
    'Max generated block size (blockmaxsize) cannot exceed the excessive block size (excessiveblocksize)')


class ABC_CmdLine_Test (BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = False

    def check_excessive(self, expected_value):
        'Check that the excessiveBlockSize is as expected'
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, expected_value)

    def check_subversion(self, pattern_str):
        'Check that the subversion is set as expected'
        netinfo = self.nodes[0].getnetworkinfo()
        subversion = netinfo['subversion']
        pattern = re.compile(pattern_str)
        assert(pattern.match(subversion))

    def excessiveblocksize_test(self):
        self.log.info("Testing -excessiveblocksize")

        self.log.info("  Set to larger than the default, i.e. %d bytes" % (REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER + 6000000))
        self.stop_node(0)
        self.start_node(0, ["-excessiveblocksize=%d" % (REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER + 6000000)])
        self.check_excessive(REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER + 6000000)

        # Check for EB correctness in the subver string
        expectedUASize = str((REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER + 6000000) // (ONE_MEGABYTE // 10))
        expectedUASize =  expectedUASize[:-1] + "\." + expectedUASize[-1] # insert \ to escape regexp .
        self.check_subversion("/Bitcoin SV:.*\(EB" + expectedUASize + "; .*\)/")

        self.log.info("  Attempt to set below legacy limit of 1MB - try %d bytes" % LEGACY_MAX_BLOCK_SIZE)
        self.stop_node(0)
        self.assert_start_raises_init_error(0, ["-excessiveblocksize=%d" % LEGACY_MAX_BLOCK_SIZE], 'Error:')
        self.log.info("  Attempt to set below blockmaxsize (mining limit)")
        self.assert_start_raises_init_error(0, ['-blockmaxsize=1500000', '-excessiveblocksize=1300000'], 'Error: ' + MAX_GENERATED_BLOCK_SIZE_ERROR)

        # Make sure we leave the test with a node running as this is what thee
        # framework expects.
        self.start_node(0, [])

    def run_test(self):
        # Run tests on -excessiveblocksize option
        self.excessiveblocksize_test()


if __name__ == '__main__':
    ABC_CmdLine_Test().main()
