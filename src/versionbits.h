// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CONSENSUS_VERSIONBITS
#define BITCOIN_CONSENSUS_VERSIONBITS

#include <cstdint>

/** What block version to use for new blocks (pre versionbits) */
static const int32_t VERSIONBITS_LAST_OLD_BLOCK_VERSION = 4;
/** Version bits are not used anymore.
    This variable is used only in legacy.cpp for consistency with old code and to set the version of block that we are going to mine. */
static const int32_t VERSIONBITS_TOP_BITS = 0x20000000UL;

#endif
