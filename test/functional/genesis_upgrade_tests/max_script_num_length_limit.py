#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_ADD
from test_framework.cdefs import MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS, MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS, DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS


class MaxScriptNumLengthTestWithPolicy(GenesisHeightBasedSimpleTestsCase):
    POLICY_VALUE = 100
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + [
        '-banscore=1000000', '-whitelist=127.0.0.1', '-maxstdtxvalidationduration=5000', '-maxnonstdtxvalidationduration=5001', '-maxscriptnumlengthpolicy=' + str(POLICY_VALUE)]
    NAME = 'Max script num length limit, -maxscriptnumlengthpolicy=' + str(POLICY_VALUE)

    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), OP_ADD]),
                             "PRE-GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), OP_ADD]),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Script number overflow)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), OP_ADD]),
                             "MEMPOOL AT GENESIS", b""
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), OP_ADD]),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Script number overflow)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), OP_ADD]),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), OP_ADD]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Script number overflow)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("GENESIS", CScript([bytearray([42] * POLICY_VALUE), bytearray([42] * POLICY_VALUE), OP_ADD]),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("GENESIS", CScript([bytearray([42] * (POLICY_VALUE + 1)), bytearray([42] * (POLICY_VALUE + 1)), OP_ADD]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Script number overflow)'
                             ),
    ]


class MaxScriptNumLengthTest(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + [
        '-banscore=1000000', '-whitelist=127.0.0.1', '-maxstdtxvalidationduration=5000', '-maxnonstdtxvalidationduration=5001', '-maxscriptsizepolicy=0']
    NAME = 'Max script num length limit, -maxscriptnumlengthpolicy not defined.'

    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), OP_ADD]),
                             "PRE-GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), OP_ADD]),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Script number overflow)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), OP_ADD]),
                             "MEMPOOL AT GENESIS", b""
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), OP_ADD]),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Script number overflow)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), bytearray([42] * MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS), OP_ADD]),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1)), OP_ADD]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Script number overflow)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("GENESIS", CScript([bytearray([42] * DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS), bytearray([42] * DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS), OP_ADD]),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("GENESIS", CScript([bytearray([42] * (DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS + 1)), bytearray([42] * (DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS + 1)), OP_ADD]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Script number overflow)'
                             ),
        SimpleTestDefinition("GENESIS", CScript([bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)), bytearray([42] * (MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)), OP_ADD]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script number overflow)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
    ]
