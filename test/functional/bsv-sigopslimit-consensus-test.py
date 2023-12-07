#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test sigops consensus limit before and after genesis

Scenario:
Genesis height is 125. Current tip is at 120
Add a block with MAX_BLOCK_SIGOPS_PER_MB sigops at height 121                                               -> OK
Add a block with MAX_BLOCK_SIGOPS_PER_MB +1 sigops at height 122                                            -> REJECTED(bad-blk-sigops)
Revert back to height 121 and add a block with (MAX_BLOCK_SIGOPS_PER_MB / 20) multisigs                     -> OK
Add a block with (MAX_BLOCK_SIGOPS_PER_MB / 20 +1) multisigs                                                -> REJECTED(bad-blk-sigops)
Revert back to height 122 and add a block with MAX_BLOCK_SIGOPS_PER_MB sigops (OP_CHECKSIGVERIFY)           -> OK
Add a block with MAX_BLOCK_SIGOPS_PER_MB +1 sigops (OP_CHECKSIGVERIFY) at height 123                        -> REJECTED(bad-blk-sigops)

-- We are at genesis height; repeat the tests from above to make sure they pass now
Add a block with MAX_BLOCK_SIGOPS_PER_MB sigops at height 125                                               -> OK
Add a block with MAX_BLOCK_SIGOPS_PER_MB +1 sigops at height 126                                            -> OK
Add a block with (MAX_BLOCK_SIGOPS_PER_MB / 20 +1) multisigs at height 127                                  -> OK
Add a block with MAX_BLOCK_SIGOPS_PER_MB sigops (OP_CHECKSIGVERIFY) at height 128                           -> OK
Add a block with MAX_BLOCK_SIGOPS_PER_MB +1 sigops (OP_CHECKSIGVERIFY) at height 129                        -> OK
Add a block with MAX_BLOCK_SIGOPS_PER_MB_POST_GENESIS sigops to check that max size is still allowed        -> OK
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import *
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.script import *
from test_framework.cdefs import LEGACY_MAX_BLOCK_SIZE, MAX_BLOCK_SIGOPS_PER_MB
from time import sleep


class CheckSigTest(ComparisonTestFramework):
    # Can either run this test as 1 node with expected answers, or two and compare them.
    # Change the "outcome" variable from each TestInstance object to only do the comparison.
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 125
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight, "-maxblocksigopspermbpolicy=21000"]]

    def run_test(self):
        self.test.run()

    def get_tests(self):

        # move the tip back to a previous block
        def tip(number):
            self.chain.set_tip(number)

        # shorthand for functions
        block = self.chain.next_block

        node = self.nodes[0]
        gen_hash = int(node.getbestblockhash(), 16)
        self.chain.set_genesis_hash(gen_hash)

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 119, 119)

        yield test

        logger.info("Before Genesis, blocks 2, 4, 6 should be rejected because of too many sigops")
        # Test 3
        # Add a block with MAX_BLOCK_SIGOPS_PER_MB and one with one more sigop
        # Test that a block with a lot of checksigs is okay
        #tip(0) - height = 120

        lots_of_checksigs = CScript([OP_CHECKSIG] * MAX_BLOCK_SIGOPS_PER_MB)
        b1 = block(1, spend=out[0], script=lots_of_checksigs, block_size=ONE_MEGABYTE)
        self.chain.save_spendable_output()
        yield self.accepted()
        # Test 4
        # Test that a block with too many checksigs is rejected
        b2 = block(2, spend=out[1], script=lots_of_checksigs, block_size=ONE_MEGABYTE, extra_sigops=1)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))

        # Test 5
        # Test that a block with a lot of checkmultisigs is ok

        tip(1)
        lots_of_multisigs = CScript([OP_CHECKMULTISIG] * (MAX_BLOCK_SIGOPS_PER_MB // 20))
        b3 = block(3, spend=out[3], script=lots_of_multisigs, block_size=ONE_MEGABYTE)
        assert_equal(get_legacy_sigopcount_block(b3), MAX_BLOCK_SIGOPS_PER_MB)
        yield self.accepted()

        # Test 6
        # this goes over the limit because the coinbase has one sigop
        b4 = block(4, spend=out[4], script=lots_of_multisigs, block_size=ONE_MEGABYTE, extra_sigops=1)
        assert_equal(get_legacy_sigopcount_block(b4), MAX_BLOCK_SIGOPS_PER_MB + 1)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))

        # CHECKSIGVERIFY
        tip(3)
        # Test 7
        lots_of_checksigs = CScript([OP_CHECKSIGVERIFY] * (MAX_BLOCK_SIGOPS_PER_MB))
        b5 = block(5, spend=out[5], script=lots_of_checksigs, block_size=ONE_MEGABYTE)
        assert_equal(get_legacy_sigopcount_block(b5), MAX_BLOCK_SIGOPS_PER_MB)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Test 8
        b6 = block(6, spend=out[6], script=lots_of_checksigs, block_size=ONE_MEGABYTE, extra_sigops=1)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))
        tip(5)
        b7 = block(7)
        yield self.accepted()

        logger.info("Genesis is enabled, all the following blocks should be accepted")

        # Test 10
        # Add a block with MAX_BLOCK_SIGOPS_PER_MB and one with one more sigop
        # Test that a block with a lot of checksigs is okay
        lots_of_checksigs = CScript([OP_CHECKSIG] * MAX_BLOCK_SIGOPS_PER_MB)
        b9 = block(9, spend=out[9], script=lots_of_checksigs, block_size=ONE_MEGABYTE)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Test 11
        # Test that a block with 'too many' checksigs is now accepted
        b10 = block(10, spend=out[10], script=lots_of_checksigs, block_size=ONE_MEGABYTE, extra_sigops=1)
        yield self.accepted()
        self.chain.save_spendable_output()

        # Test 12
        # this goes over the limit because the coinbase has one sigop, but should pass bcs genesis threshold was reached
        b11 = block(11, spend=out[11], script=lots_of_multisigs, block_size=ONE_MEGABYTE, extra_sigops=1)
        yield self.accepted()

        # CHECKSIGVERIFY
        # Test 13
        lots_of_checksigs = CScript([OP_CHECKSIGVERIFY] * MAX_BLOCK_SIGOPS_PER_MB)
        b12 = block(12, spend=out[12], script=lots_of_checksigs, block_size=ONE_MEGABYTE)
        self.chain.save_spendable_output()
        yield self.accepted()
        # Test 14
        b13 = block(13, spend=out[13], script=lots_of_checksigs, block_size=ONE_MEGABYTE, extra_sigops=1)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Test 15
        # Test that a block with MAX_BLOCK_SIGOPS_PER_MB_POST_GENESIS checksigs is OK
        MAX_BLOCK_SIGOPS_PER_MB_POST_GENESIS=1000000
        lots_of_checksigs = CScript([OP_CHECKSIG] * MAX_BLOCK_SIGOPS_PER_MB)
        b14 = block(14, spend=out[14], script=lots_of_checksigs, block_size=ONE_MEGABYTE+500, extra_sigops=MAX_BLOCK_SIGOPS_PER_MB_POST_GENESIS-MAX_BLOCK_SIGOPS_PER_MB)
        yield self.accepted()


if __name__ == '__main__':
    CheckSigTest().main()
