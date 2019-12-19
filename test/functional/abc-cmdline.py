#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Exercise the command line functions specific to ABC functionality.
Currently:

-excessiveblocksize=<blocksize_in_bytes>
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.cdefs import LEGACY_MAX_BLOCK_SIZE, UINT32_MAX, ONE_MEGABYTE


class ABC_CmdLine_Test (BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = False

    def check_excessive(self, expected_value):
        'Check that the excessiveBlockSize is as expected'
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, expected_value)

    def excessiveblocksize_test(self):
        self.log.info("Testing -excessiveblocksize")

        self.log.info("  Set to larger than the default, i.e. %d bytes" % (UINT32_MAX + 6000000))
        self.stop_node(0)
        self.start_node(0, ["-excessiveblocksize=%d" % (UINT32_MAX + 6000000)])
        self.check_excessive(UINT32_MAX + 6000000)

        self.log.info("  Attempt to set below legacy limit of 1MB - try %d bytes" % LEGACY_MAX_BLOCK_SIZE)
        self.stop_node(0)
        self.assert_start_raises_init_error(0, ["-excessiveblocksize=%d" % LEGACY_MAX_BLOCK_SIZE], 'Error:')

        # Make sure we leave the test with a node running as this is what thee
        # framework expects.
        self.start_node(0, [])

    def run_test(self):
        # Run tests on -excessiveblocksize option
        self.excessiveblocksize_test()


if __name__ == '__main__':
    ABC_CmdLine_Test().main()
