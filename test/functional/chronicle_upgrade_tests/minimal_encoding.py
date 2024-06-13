# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, CScriptOp, OP_TRUE, OP_DROP, OP_PUSHDATA1, OP_0, OP_1ADD

"""
Test that before Chronicle we reject scripts without minimal encoding of
pushdata and script nums.

Test that during the Chronicle grace period we soft-reject such non-minimally
encoded data and script nums.

Test that after Chronicle we accept non-minimally encoded values.
"""


class MinimalEncodingTestCase(ChronicleHeightBasedSimpleTestsCase):
    NAME = "Reject txns without minimal encoding of data & script nums before Chronicle and accept after"

    TESTS = [
        # Before Chronicle
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "PRE_CHRONICLE", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "PRE_CHRONICLE", CScript([OP_PUSHDATA1, b'\x00']),
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)'),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "PRE_CHRONICLE", CScript([CScriptOp(0x02), b'\x00', b'\x00']),
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)'),

        # Block before Chronicle grace period
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_GRACE", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_GRACE", CScript([OP_PUSHDATA1, b'\x00']),
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)'),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_GRACE", CScript([CScriptOp(0x02), b'\x00', b'\x00']),
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)'),

        # Start of Chronicle grace period
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_BEGIN", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_BEGIN", CScript([OP_PUSHDATA1, b'\x00']),
                             p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Data push larger than necessary)'),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_BEGIN", CScript([CScriptOp(0x02), b'\x00', b'\x00']),
                             p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Non-minimally encoded script number)'),

        # Block before Chronicle activation
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_ACTIVATION", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_ACTIVATION", CScript([OP_PUSHDATA1, b'\x00'])),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "CHRONICLE_PRE_ACTIVATION", CScript([CScriptOp(0x02), b'\x00', b'\x00'])),

        # Chronicle activation height
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_ACTIVATION", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_ACTIVATION", CScript([OP_PUSHDATA1, b'\x00'])),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "CHRONICLE_ACTIVATION", CScript([CScriptOp(0x02), b'\x00', b'\x00'])),

        # Block after Chronicle activation height
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_POST_ACTIVATION", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_POST_ACTIVATION", CScript([OP_PUSHDATA1, b'\x00'])),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "CHRONICLE_POST_ACTIVATION", CScript([CScriptOp(0x02), b'\x00', b'\x00'])),

        # End of chronicle grace period
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_END", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_END", CScript([OP_PUSHDATA1, b'\x00'])),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "CHRONICLE_GRACE_END", CScript([CScriptOp(0x02), b'\x00', b'\x00'])),

        # After Chronicle
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "POST_CHRONICLE", CScript([OP_0])),
        SimpleTestDefinition(None, CScript([OP_DROP, OP_TRUE]),
                             "POST_CHRONICLE", CScript([OP_PUSHDATA1, b'\x00'])),
        SimpleTestDefinition(None, CScript([OP_1ADD, OP_DROP, OP_TRUE]),
                             "POST_CHRONICLE", CScript([CScriptOp(0x02), b'\x00', b'\x00'])),
    ]
