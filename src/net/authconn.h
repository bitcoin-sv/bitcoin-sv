// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "key.h"
#include "pubkey.h"

namespace authconn {

/** Some constants */

// The authch message version 0x01
static constexpr uint32_t AUTHCH_V1 = 0x01;
// The size of the authch challenge message in the version 0x01.
static constexpr uint32_t AUTHCH_MSG_SIZE_IN_BYTES_V1 = 0x20;
// Secp256k1 the size of the compressed public key.
static constexpr uint32_t SECP256K1_COMP_PUB_KEY_SIZE_IN_BYTES = 0x21;
// Secp256k1 the min and max DER-encoded signature acceptable size.
// NOTE: The signature consists of the R and S valued which are variable length.
static constexpr uint32_t SECP256K1_DER_SIGN_MIN_SIZE_IN_BYTES = 0x46;
static constexpr uint32_t SECP256K1_DER_SIGN_MAX_SIZE_IN_BYTES = 0x48;

// Reject codes for authconn errors.
static constexpr uint8_t REJECT_AUTH_CONN_SETUP = 0x70;

/**
 * The node's instance needs to keep special data, per each peer,
 * involved into the challenge-response authentication procedure.
 */
struct AuthConnData
{
    uint256 msgHash {}; /** the authch challenge msg */
};
} // namespace authconn
