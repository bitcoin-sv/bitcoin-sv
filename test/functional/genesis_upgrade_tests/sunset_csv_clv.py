#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_DROP, OP_1, OP_CHECKSEQUENCEVERIFY, OP_CHECKLOCKTIMEVERIFY


class SunsetCSVCLVTest(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1']
    NAME = "Sunseting CSV and CLV when Genesis is active"

    TESTS = [

        # Pre-Genesis CLV is enabled and because of invalid locktime we should get error
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1, OP_CHECKLOCKTIMEVERIFY, OP_DROP]),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1, OP_CHECKLOCKTIMEVERIFY, OP_DROP]),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_1, OP_CHECKLOCKTIMEVERIFY, OP_DROP]),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_1, OP_CHECKLOCKTIMEVERIFY, OP_DROP]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        # Since Genesis CLV is disabled and therefore operator behaves as NOP
        SimpleTestDefinition("GENESIS", CScript([OP_1, OP_CHECKLOCKTIMEVERIFY, OP_DROP]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        # Pre-Genesis CSV is enabled and because of invalid locktime we should get error
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1, OP_CHECKSEQUENCEVERIFY, OP_DROP]),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1, OP_CHECKSEQUENCEVERIFY, OP_DROP]),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_1, OP_CHECKSEQUENCEVERIFY, OP_DROP]),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_1, OP_CHECKSEQUENCEVERIFY, OP_DROP]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Locktime requirement not satisfied)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        # Since Genesis CSV is disabled and therefore operator behaves as NOP
        SimpleTestDefinition("GENESIS", CScript([OP_1, OP_CHECKSEQUENCEVERIFY, OP_DROP]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
    ]
