#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_TRUE, OP_NOP, OP_DUP, OP_DROP
from test_framework.cdefs import MAX_OPS_PER_SCRIPT_BEFORE_GENESIS


class MaxOpsPerScriptTestWithPolicy(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-maxopsperscriptpolicy=1000']
    NAME = "Max operations per script limit with maxopsperscriptpolicy set to 1000"

    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * MAX_OPS_PER_SCRIPT_BEFORE_GENESIS),
                             "PRE-GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * (MAX_OPS_PER_SCRIPT_BEFORE_GENESIS + 1)),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Operation limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_TRUE] + [OP_NOP] * MAX_OPS_PER_SCRIPT_BEFORE_GENESIS),
                             "MEMPOOL AT GENESIS", b"",
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_TRUE] + [OP_NOP] * (MAX_OPS_PER_SCRIPT_BEFORE_GENESIS + 1)),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Operation limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * MAX_OPS_PER_SCRIPT_BEFORE_GENESIS),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * (MAX_OPS_PER_SCRIPT_BEFORE_GENESIS + 1)),
                             "GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Operation limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("GENESIS", CScript([OP_TRUE] + [OP_NOP] * 1000),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("GENESIS", CScript([OP_TRUE] + [OP_NOP] * 1001),
                             "GENESIS", b"",
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Operation limit exceeded)'
                             ),
    ]


class MaxOpsPerScriptTest(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1']
    NAME = "Max operations per script limit, maxopsperscriptpolicy not defined"

    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * MAX_OPS_PER_SCRIPT_BEFORE_GENESIS),
                             "PRE-GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * (MAX_OPS_PER_SCRIPT_BEFORE_GENESIS + 1)),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Operation limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_TRUE] + [OP_NOP] * MAX_OPS_PER_SCRIPT_BEFORE_GENESIS),
                             "MEMPOOL AT GENESIS", b"",
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_TRUE] + [OP_NOP] * (MAX_OPS_PER_SCRIPT_BEFORE_GENESIS + 1)),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Operation limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * MAX_OPS_PER_SCRIPT_BEFORE_GENESIS),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE] + [OP_NOP] * (MAX_OPS_PER_SCRIPT_BEFORE_GENESIS + 1)),
                             "GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Operation limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("GENESIS", CScript([OP_TRUE] + [OP_NOP] * (MAX_OPS_PER_SCRIPT_BEFORE_GENESIS + 1)),
                             "GENESIS", b""
                             ),
    ]
