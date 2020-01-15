#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Exercise the Bitcoin SV RPC calls.

import time
import random
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (assert_equal, assert_raises_rpc_error)
from test_framework.cdefs import (ONE_MEGABYTE,
                                  LEGACY_MAX_BLOCK_SIZE,
                                  REGTEST_DEFAULT_MAX_BLOCK_SIZE)


class ABC_RPC_Test (BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.tip = None
        self.setup_clean_chain = True
        self.extra_args = [['-norelaypriority',
                            '-whitelist=127.0.0.1']]

    def test_excessiveblock(self):
        # Check that we start with REGTEST_DEFAULT_MAX_BLOCK_SIZE
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, REGTEST_DEFAULT_MAX_BLOCK_SIZE)

        # Check that setting to legacy size is ok
        self.nodes[0].setexcessiveblock(LEGACY_MAX_BLOCK_SIZE + 1)
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, LEGACY_MAX_BLOCK_SIZE + 1)

        x = self.nodes[0].setexcessiveblock, LEGACY_MAX_BLOCK_SIZE

        # Check that going below legacy size is not accepted
        assert_raises_rpc_error(-8, 'Excessive block size (excessiveblocksize) must be larger than %d' %
                                LEGACY_MAX_BLOCK_SIZE, self.nodes[0].setexcessiveblock, LEGACY_MAX_BLOCK_SIZE)
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, LEGACY_MAX_BLOCK_SIZE + 1)

        # Check setting to 2MB
        self.nodes[0].setexcessiveblock(2 * ONE_MEGABYTE)
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, 2 * ONE_MEGABYTE)

        # Check setting to 13MB
        self.nodes[0].setexcessiveblock(13 * ONE_MEGABYTE)
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, 13 * ONE_MEGABYTE)

        # Check setting to 13.14MB
        self.nodes[0].setexcessiveblock(13140000)
        getsize = self.nodes[0].getexcessiveblock()
        ebs = getsize['excessiveBlockSize']
        assert_equal(ebs, 13.14 * ONE_MEGABYTE)

    def run_test(self):
        self.genesis_hash = int(self.nodes[0].getbestblockhash(), 16)
        self.test_excessiveblock()


if __name__ == '__main__':
    ABC_RPC_Test().main()
