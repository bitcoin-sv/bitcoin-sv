#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.height_based_test_framework import HeightBasedTestsCase, HeightBasedSimpleTestsCase


class GenesisHeightTestsCaseBase(HeightBasedTestsCase):

    NAME = None
    GENESIS_ACTIVATION_HEIGHT = 150
    ARGS = [f"-genesisactivationheight={GENESIS_ACTIVATION_HEIGHT}"]
    TESTING_HEIGHTS = [
        (GENESIS_ACTIVATION_HEIGHT - 3, None                         , "PRE-GENESIS"),
        (GENESIS_ACTIVATION_HEIGHT - 2, "PRE-GENESIS"                , "MEMPOOL AT GENESIS"),
        (GENESIS_ACTIVATION_HEIGHT - 1, "MEMPOOL AT GENESIS"         , "GENESIS"),
        (GENESIS_ACTIVATION_HEIGHT    , "GENESIS"                    , None),
        #(GENESIS_ACTIVATION_HEIGHT - 1, "MEMPOOL AT GENESIS ROLBACK" , None),
        #(GENESIS_ACTIVATION_HEIGHT - 2, "PRE-GENESIS ROLBACK"        , None),
    ]


class GenesisHeightBasedSimpleTestsCase(HeightBasedSimpleTestsCase):

    GENESIS_ACTIVATION_HEIGHT = 150
    ARGS = [f"-genesisactivationheight={GENESIS_ACTIVATION_HEIGHT}"]
    TESTING_HEIGHTS = [
        (GENESIS_ACTIVATION_HEIGHT - 3, None                         , "PRE-GENESIS"),
        (GENESIS_ACTIVATION_HEIGHT - 2, "PRE-GENESIS"                , "MEMPOOL AT GENESIS"),
        (GENESIS_ACTIVATION_HEIGHT - 1, "MEMPOOL AT GENESIS"         , "GENESIS"),
        (GENESIS_ACTIVATION_HEIGHT    , "GENESIS"                    , None),
        #(GENESIS_ACTIVATION_HEIGHT - 1, "MEMPOOL AT GENESIS ROLBACK" , None),
        #(GENESIS_ACTIVATION_HEIGHT - 2, "PRE-GENESIS ROLBACK"        , None),
    ]
