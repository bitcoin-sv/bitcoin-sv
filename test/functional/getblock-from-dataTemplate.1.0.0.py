#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test getblock RPCs on pregenerated data with node version 1.0.0.
Blocks that were created before the introduction of CDiskBlockMetaData class, don't have information about block size written to disk.
"""
import shutil

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class GetBlockRPCTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_chain(self):
        self.log.info("Initializing test directory " + self.options.tmpdir)

        #copy pregenerated data with node version 1.0.0 to tmpdir
        for i in range(self.num_nodes):
            initialize_datadir(self.options.tmpdir, i)
            from_dir = os.path.normpath(os.path.dirname(os.path.realpath(__file__)) + "/../../test/functional/data/dataTemplate.1.0.0/node"+str(i)+"/regtest")
            to_dir = os.path.join(self.options.tmpdir, "node"+str(i)+"/regtest")
            shutil.copytree(from_dir, to_dir)

    def run_test(self):
        # getblock with verbosity DECODE_HEADER_AND_COINBASE = 3 doesn't write metadata data to disk
        block = self.nodes[0].getblock("56fe3682f1d08b3fc9c21349b5a56b6faca0930fbbe2073b183942fe6e9a912c", 3)
        assert_equal(block['hash'], "56fe3682f1d08b3fc9c21349b5a56b6faca0930fbbe2073b183942fe6e9a912c")
        # Duplicate getblock with verbosity DECODE_HEADER_AND_COINBASE = 3 call, to make sure that first call didn't write metadata to disk.
        block = self.nodes[0].getblock("56fe3682f1d08b3fc9c21349b5a56b6faca0930fbbe2073b183942fe6e9a912c", 3)
        # Blocks that were created before the introduction of CDiskBlockMetaData class,
        # don't have information about block size written to disk.
        assert 'size' not in block

        # Block size information can be calculated and written to disk for any block without this data
        # when RPC getblock method is called and whole block is read from the disk.
        block = self.nodes[0].getblock("56fe3682f1d08b3fc9c21349b5a56b6faca0930fbbe2073b183942fe6e9a912c", 2)
        assert_equal(block['hash'], "56fe3682f1d08b3fc9c21349b5a56b6faca0930fbbe2073b183942fe6e9a912c")
        assert_equal(block['size'], 834)


if __name__ == '__main__':
    GetBlockRPCTest().main()
