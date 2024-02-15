#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_CAT, OP_DUP
from test_framework.cdefs import MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS, MAX_STACK_ELEMENTS_BEFORE_GENESIS, ELEMENT_OVERHEAD


class MaxStackSizeTestWithCustomSize(GenesisHeightBasedSimpleTestsCase):
    # In all test cases, we have 2 elements on stack, which means that we have to add 2*32 bytes for covering ELEMENT_OVERHEAD
    MAX_STACK_MEMORY_USAGE_POLICY = 500
    MAX_STACK_MEMORY_USAGE_CONSENSUS = 600
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + \
        ['-maxstackmemoryusagepolicy=%d' % (MAX_STACK_MEMORY_USAGE_POLICY + 2 * ELEMENT_OVERHEAD),
         '-maxstackmemoryusageconsensus=%d' % (MAX_STACK_MEMORY_USAGE_CONSENSUS + 2 * ELEMENT_OVERHEAD),
         '-banscore=1000000', '-whitelist=127.0.0.1', '-maxstdtxvalidationduration=5000', '-maxnonstdtxvalidationduration=5001']
    NAME = "Max stack size with custom maxstackmemoryusagepolicy and maxstackmemoryusageconsensus"

    TESTS = [
        # Before genesis, sum of concatenating elements sizes should be <= 520.
        # For following cases, UTXO is made before genesis.
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"*(MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS - 1), b"b", OP_CAT]),
                             "PRE-GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"*MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS, b"b", OP_CAT]),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Push value size limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"*(MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS - 1), b"b", OP_CAT]),
                             "MEMPOOL AT GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"*MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS, b"b", OP_CAT]),
                             "MEMPOOL AT GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Push value size limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"*(MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS - 1), b"b", OP_CAT]),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"*MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS, b"b", OP_CAT]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Push value size limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        # UTXO is in block at height "MEMPOOL AT GENESIS" which means pre genesis rules apply to this block
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([b"a"*(MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS - 1), b"b", OP_CAT]),
                             "GENESIS", b"",
                             ),
        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([b"a"*MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS, b"b", OP_CAT]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Push value size limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

        # For following cases, UTXO is made after genesis, so new rules apply here (500 bytes for policy and 600 bytes for consensus)
        # At some point of performing OP_CAT the stack size is size_of_first + 2 * size_of_second which in our case produce stack oversized error
        SimpleTestDefinition("GENESIS", CScript([b"a"*(MAX_STACK_MEMORY_USAGE_POLICY - 1), b"b", OP_CAT]),
                             "GENESIS", b""
                             ),
        SimpleTestDefinition("GENESIS", CScript([b"a"*MAX_STACK_MEMORY_USAGE_POLICY, b"b", OP_CAT]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Stack size limit exceeded)',
                             block_reject_reason=None
                             ),
        SimpleTestDefinition("GENESIS", CScript([b"a"*(MAX_STACK_MEMORY_USAGE_CONSENSUS - 1), b"b", OP_CAT]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Stack size limit exceeded)',
                             block_reject_reason=None
                             ),
        SimpleTestDefinition("GENESIS", CScript([b"a"*MAX_STACK_MEMORY_USAGE_CONSENSUS, b"b", OP_CAT]),
                             "GENESIS", b"",
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Stack size limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),

    ]


class MaxStackSizeTestWithElementsCount(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-maxstdtxvalidationduration=5000', '-maxnonstdtxvalidationduration=5001']
    NAME = "Max stack size before genesis: MAX_STACK_ELEMENTS_COUNT limit"

    TESTS = [
        # Script did not clean its stack means that stack size limit checks were successful
        # Usage of OP_DROP here would cause problems with MAX_OPS_PER_SCRIPT which is not configurable before genesis.
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"]*MAX_STACK_ELEMENTS_BEFORE_GENESIS),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)',
                             block_reject_reason=None
                             ),
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"]*(MAX_STACK_ELEMENTS_BEFORE_GENESIS + 1)),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Stack size limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("GENESIS", CScript([b"a"]*(MAX_STACK_ELEMENTS_BEFORE_GENESIS + 1)),
                             "GENESIS", b"",
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)',
                             block_reject_reason=None
                             ),
    ]


class MaxStackSizeTest(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-maxstdtxvalidationduration=5000', '-maxnonstdtxvalidationduration=5001']
    NAME = "Max stack size with default policy and consensus size"

    # After genesis, default max stack memory usage is MAX_INT. For policy 100 MB.
    # We mock big stack with the help of OP_DUP and OP_CAT combination which generates strings with the size of powers of 2.
    # Transactions: 2^26 = 67 MB  bytes (OK), 2^27 = 134 MB  bytes (FAIL because > 100 MB)
    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", CScript([b"a"] + [OP_DUP, OP_CAT]*18),
                             "PRE-GENESIS", b"",
                             p2p_reject_reason=b'genesis-script-verify-flag-failed (Push value size limit exceeded)',
                             block_reject_reason=b'blk-bad-inputs'
                             ),
        SimpleTestDefinition("GENESIS", CScript([b"a"] + [OP_DUP, OP_CAT]*26),
                             "GENESIS", b""
                             ),
    ]
