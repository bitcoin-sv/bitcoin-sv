# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_TRUE

"""
P2P txns that don't leave a clean stack are rejected until we hit one block below
Chronicle activation height and are then accepted for mining into the next block.

Blocks containing txns that don't clean their stack are always accepted as before.
"""


class CleanStackTestCase(ChronicleHeightBasedSimpleTestsCase):
    NAME = "Reject txns without a clean stack before Chronicle and accept after"

    TESTS = [
        # Before Chronicle
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "PRE_CHRONICLE", CScript([OP_TRUE]),
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)'),

        # Block before Chronicle grace period
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "CHRONICLE_PRE_GRACE", CScript([OP_TRUE]),
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)'),

        # Start of Chronicle grace period
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "CHRONICLE_GRACE_BEGIN", CScript([OP_TRUE]),
                             p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Script did not clean its stack)'),

        # Block before Chronicle activation
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "CHRONICLE_PRE_ACTIVATION", CScript([OP_TRUE])),

        # Chronicle activation height
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "CHRONICLE_ACTIVATION", CScript([OP_TRUE])),

        # Block after Chronicle activation height
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "CHRONICLE_POST_ACTIVATION", CScript([OP_TRUE])),

        # End of chronicle grace period
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "CHRONICLE_GRACE_END", CScript([OP_TRUE])),

        # After Chronicle
        SimpleTestDefinition(None, CScript([OP_TRUE]),
                             "POST_CHRONICLE", CScript([OP_TRUE])),
    ]
