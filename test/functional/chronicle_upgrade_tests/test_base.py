#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.height_based_test_framework import HeightBasedTestsCase, HeightBasedSimpleTestsCase


class ChronicleHeightTestsCase(HeightBasedTestsCase):

    NAME = None

    GENESIS_ACTIVATION_HEIGHT = 150
    GRACE_PERIOD = 5
    GENESIS_ACTIVATED_HEIGHT = GENESIS_ACTIVATION_HEIGHT + GRACE_PERIOD
    CHRONICLE_ACTIVATION_HEIGHT = 175
    CHRONICLE_GRACE_START_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT - GRACE_PERIOD
    CHRONICLE_GRACE_END_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT + GRACE_PERIOD

    ARGS = [f"-genesisactivationheight={GENESIS_ACTIVATION_HEIGHT}",
            f"-chronicleactivationheight={CHRONICLE_ACTIVATION_HEIGHT}",
            f"-maxgenesisgracefulperiod={GRACE_PERIOD}",
            f"-maxchroniclegracefulperiod={GRACE_PERIOD}"]

    TESTING_HEIGHTS = [
        (GENESIS_ACTIVATION_HEIGHT - 2,    None,                        "PRE_GENESIS"),                # No-test, just build UTXOs
        (GENESIS_ACTIVATED_HEIGHT,         None,                        "PRE_CHRONICLE"),              # No-test, just build UTXOs
        (GENESIS_ACTIVATED_HEIGHT + 1,     "PRE_CHRONICLE",             "CHRONICLE_PRE_GRACE"),        # Post-Genesis but pre anything Chronicle
        (CHRONICLE_GRACE_START_HEIGHT - 1, "CHRONICLE_PRE_GRACE",       "CHRONICLE_GRACE_BEGIN"),      # Block before Chronicle grace period starts
        (CHRONICLE_GRACE_START_HEIGHT,     "CHRONICLE_GRACE_BEGIN",     "CHRONICLE_PRE_ACTIVATION"),   # First block in Chronicle grace period
        (CHRONICLE_ACTIVATION_HEIGHT - 1,  "CHRONICLE_PRE_ACTIVATION",  "CHRONICLE_ACTIVATION"),       # Block before Chronicle activation
        (CHRONICLE_ACTIVATION_HEIGHT,      "CHRONICLE_ACTIVATION",      "CHRONICLE_POST_ACTIVATION"),  # Chronicle activates
        (CHRONICLE_ACTIVATION_HEIGHT + 1,  "CHRONICLE_POST_ACTIVATION", "CHRONICLE_GRACE_END"),        # Block after Chronicle activation
        (CHRONICLE_GRACE_END_HEIGHT - 2,   "CHRONICLE_GRACE_END",       "POST_CHRONICLE"),             # Last block in Chronicle grace period
        (CHRONICLE_GRACE_END_HEIGHT,       "POST_CHRONICLE",            None),                         # Chronicle is activated and we've left the grace period
    ]


class ChronicleHeightBasedSimpleTestsCase(HeightBasedSimpleTestsCase):

    GENESIS_ACTIVATION_HEIGHT = 150
    GRACE_PERIOD = 5
    GENESIS_ACTIVATED_HEIGHT = GENESIS_ACTIVATION_HEIGHT + GRACE_PERIOD
    CHRONICLE_ACTIVATION_HEIGHT = 175
    CHRONICLE_GRACE_START_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT - GRACE_PERIOD
    CHRONICLE_GRACE_END_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT + GRACE_PERIOD

    ARGS = [f"-genesisactivationheight={GENESIS_ACTIVATION_HEIGHT}",
            f"-chronicleactivationheight={CHRONICLE_ACTIVATION_HEIGHT}",
            f"-maxgenesisgracefulperiod={GRACE_PERIOD}",
            f"-maxchroniclegracefulperiod={GRACE_PERIOD}"]

    TESTING_HEIGHTS = [
        (GENESIS_ACTIVATED_HEIGHT + 1,     "PRE_CHRONICLE",             None),  # Post-Genesis but pre anything Chronicle
        (CHRONICLE_GRACE_START_HEIGHT - 1, "CHRONICLE_PRE_GRACE",       None),  # Block before Chronicle grace period starts
        (CHRONICLE_GRACE_START_HEIGHT,     "CHRONICLE_GRACE_BEGIN",     None),  # First block in Chronicle grace period
        (CHRONICLE_ACTIVATION_HEIGHT - 1,  "CHRONICLE_PRE_ACTIVATION",  None),  # Block before Chronicle activation
        (CHRONICLE_ACTIVATION_HEIGHT,      "CHRONICLE_ACTIVATION",      None),  # Chronicle activates
        (CHRONICLE_ACTIVATION_HEIGHT + 1,  "CHRONICLE_POST_ACTIVATION", None),  # Block after Chronicle activation
        (CHRONICLE_GRACE_END_HEIGHT - 2,   "CHRONICLE_GRACE_END",       None),  # Last block in Chronicle grace period
        (CHRONICLE_GRACE_END_HEIGHT,       "POST_CHRONICLE",            None),  # Chronicle is activated and we've left the grace period
    ]
