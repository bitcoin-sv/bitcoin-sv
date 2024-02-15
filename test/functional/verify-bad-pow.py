#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import create_coinbase, create_block_from_candidate, merkle_root_from_merkle_proof, solve_bad
from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import *
from test_framework.util import *

# This test checks verifyblockcandidate RPC call. VerifyBlock tests a block template for validity without a valid PoW.
# Test scenario: a block with invalid POW is created.
#   Submitblock RPC call should return 'high-hash' failure, which means POW was invalid.
#   VerifyBlock should return no failure.


class VerifyWithoutPowTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-standalone"]]

    def run_test(self):
        node = self.nodes[0]

        # Mine a block to leave initial block download
        node.generate(1)
        candidate = node.getminingcandidate()

        [block, coinbase] = create_block_from_candidate(candidate, False)
        solve_bad(block)

        submitResult = node.submitblock(ToHex(block))
        assert submitResult == 'high-hash'

        submitResult = node.verifyblockcandidate(ToHex(block))
        assert submitResult == None


if __name__ == '__main__':
    VerifyWithoutPowTest().main()
