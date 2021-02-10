// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#ifndef BITCOIN_TXHASHER_H
#define BITCOIN_TXHASHER_H

#include "primitives/transaction.h"
#include "hash.h"

/**
 * Hasher objects transactions for std::unordered_set and similar hash-based containers.
 */
class StaticHasherSalt
{
protected:
    static const uint64_t k0, k1;
};

class SaltedTxidHasher : private StaticHasherSalt
{
public:
    size_t operator()(const uint256& txid) const
    {
        return SipHashUint256(k0, k1, txid);
    }
};

class SaltedOutpointHasher : private StaticHasherSalt
{
public:
    size_t operator()(const COutPoint& outpoint) const
    {
        return SipHashUint256Extra(k0, k1, outpoint.GetTxId(), outpoint.GetN());
    }
};

#endif // BITCOIN_TXHASHER_H
