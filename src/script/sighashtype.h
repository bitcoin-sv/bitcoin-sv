// Copyright (c) 2017-2018 Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_HASH_TYPE_H
#define BITCOIN_SCRIPT_HASH_TYPE_H

#include "serialize.h"

#include <cstdint>
#include <stdexcept>

/** Signature hash types/flags */
enum {
    SIGHASH_ALL = 1,
    SIGHASH_NONE = 2,
    SIGHASH_SINGLE = 3,
    SIGHASH_CHRONICLE = 0x20,
    SIGHASH_FORKID = 0x40,
    SIGHASH_ANYONECANPAY = 0x80,
};

/**
 * Base signature hash types
 * Base sig hash types not defined in this enum may be used, but they will be
 * represented as UNSUPPORTED.  See transaction
 * c99c49da4c38af669dea436d3e73780dfdb6c1ecf9958baa52960e8baee30e73 for an
 * example where an unsupported base sig hash of 0 was used.
 */
enum class BaseSigHashType : uint8_t {
    UNSUPPORTED = 0,
    ALL = SIGHASH_ALL,
    NONE = SIGHASH_NONE,
    SINGLE = SIGHASH_SINGLE
};

/** Signature hash type wrapper class */
class SigHashType {
private:
    uint32_t sigHash;

    static constexpr uint32_t BASE_SIGHASH_MASK {0x1F}; // Low 5 bits

public:
    constexpr explicit SigHashType() : sigHash(SIGHASH_ALL) {}

    constexpr explicit SigHashType(uint32_t sigHashIn) : sigHash(sigHashIn) {}

    SigHashType withBaseType(BaseSigHashType baseSigHashType) const {
        return SigHashType((sigHash & ~BASE_SIGHASH_MASK) | uint32_t(baseSigHashType));
    }

    SigHashType withForkValue(uint32_t forkId) const {
        return SigHashType((forkId << 8) | (sigHash & 0xff));
    }

    constexpr SigHashType withChronicle(bool chronicle = true) const {
        return SigHashType((sigHash & ~SIGHASH_CHRONICLE) |
                           (chronicle ? SIGHASH_CHRONICLE : 0));
    }

    constexpr SigHashType withForkId(bool forkId = true) const {
        return SigHashType((sigHash & ~SIGHASH_FORKID) |
                           (forkId ? SIGHASH_FORKID : 0));
    }

    constexpr SigHashType withAnyoneCanPay(bool anyoneCanPay = true) const {
        return SigHashType((sigHash & ~SIGHASH_ANYONECANPAY) |
                           (anyoneCanPay ? SIGHASH_ANYONECANPAY : 0));
    }

    constexpr BaseSigHashType getBaseType() const {
        return BaseSigHashType(sigHash & BASE_SIGHASH_MASK);
    }

    constexpr uint32_t getForkValue() const { return sigHash >> 8; }

    constexpr bool isDefined() const {
        const auto baseType =
            BaseSigHashType(sigHash & ~(SIGHASH_CHRONICLE | SIGHASH_FORKID | SIGHASH_ANYONECANPAY));
        return baseType >= BaseSigHashType::ALL &&
               baseType <= BaseSigHashType::SINGLE;
    }

    constexpr bool hasChronicle() const { return (sigHash & SIGHASH_CHRONICLE) != 0; }
    constexpr bool hasForkId() const { return (sigHash & SIGHASH_FORKID) != 0; }
    constexpr bool hasAnyoneCanPay() const { return (sigHash & SIGHASH_ANYONECANPAY) != 0; }

    constexpr uint32_t getRawSigHashType() const { return sigHash; }

    template <typename Stream> void Serialize(Stream &s) const {
        ::Serialize(s, getRawSigHashType());
    }
};

#endif // BITCOIN_SCRIPT_HASH_TYPE_H
