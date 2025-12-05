# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_TRUE, OP_FALSE, OP_DROP, OP_VER, OP_EQUAL
from test_framework.script import OP_VERIF, OP_VERNOTIF, OP_ELSE, OP_ENDIF, OP_SUBSTR
from test_framework.script import OP_1, OP_2, OP_4, OP_5, OP_16, OP_LEFT, OP_RIGHT, OP_2MUL, OP_2DIV
from test_framework.script import OP_LSHIFTNUM, OP_RSHIFTNUM

"""
Test that all new/reactivated opcodes are still rejected pre-Chronicle,
but work as intended post-Chronicle.
"""


class OpcodesTestCase(ChronicleHeightBasedSimpleTestsCase):
    NAME = "Chronicle new opcode tests"
    ARGS = ChronicleHeightBasedSimpleTestsCase.ARGS + ['-whitelist=127.0.0.1']

    TESTS = [
        # Before Chronicle
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Attempted to use a disabled opcode)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Attempted to use a disabled opcode)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "PRE_CHRONICLE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),


        # Block before Chronicle grace period
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Attempted to use a disabled opcode)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Attempted to use a disabled opcode)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_PRE_GRACE", CScript(),
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),


        # Start of Chronicle grace period
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (Opcode missing or not understood)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (Attempted to use a disabled opcode)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (Attempted to use a disabled opcode)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_GRACE_BEGIN", CScript(),
                             p2p_reject_reason=b'chronicle-script-verify-flag-failed (NOPx reserved for soft-fork upgrades)',
                             block_reject_reason=b'blk-bad-inputs'),


        # Block before Chronicle activation
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_PRE_ACTIVATION", CScript()),


        # Chronicle activation height
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_ACTIVATION", CScript()),


        # Block after Chronicle activation height
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_POST_ACTIVATION", CScript()),


        # End of chronicle grace period
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_GRACE_END", CScript()),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "CHRONICLE_GRACE_END", CScript()),


        # After Chronicle
        SimpleTestDefinition(None, CScript([OP_VER, OP_DROP, OP_TRUE]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\x00\x00\x00', OP_VERIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([b'\x01\xFF\x00\x00', OP_VERNOTIF, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_4, OP_5, OP_SUBSTR, b'oWorl', OP_EQUAL]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_LEFT, b'Hello', OP_EQUAL]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([b'HelloWorld', OP_5, OP_RIGHT, b'World', OP_EQUAL]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2MUL, OP_2, OP_EQUAL]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([OP_2, OP_2DIV, OP_1, OP_EQUAL]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([OP_1, OP_2, OP_LSHIFTNUM, OP_4, OP_EQUAL]),
                             "POST_CHRONICLE", CScript()),

        SimpleTestDefinition(None, CScript([OP_16, OP_2, OP_RSHIFTNUM, OP_4, OP_EQUAL]),
                             "POST_CHRONICLE", CScript()),
    ]
