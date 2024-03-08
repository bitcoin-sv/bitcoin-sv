// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include "amount.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"
#include <optional>
#include <ostream>

struct TxId;
/**
 * Specialise std::hash for TxId.
 */
namespace std
{
    template<> class hash<TxId> : public hash<uint256> {};
}

/**
 * A TxId is the identifier of a transaction. Currently identical to TxHash but
 * differentiated for type safety.
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct TxId : public uint256 {
    TxId() = default;
    explicit TxId(const uint256 &b) : uint256{b} {}
    TxId(const TxId& b) = default;
    TxId& operator=(const TxId& b) = default;
};

std::istream & operator>>(std::istream&, TxId&);

/**
 * A TxHash is the double sha256 hash of the full transaction data.
 */
struct TxHash : public uint256 {
    explicit TxHash(const uint256 &b) : uint256(b) {}
};

/**
 * An outpoint - a combination of a transaction hash and an index n into its
 * vout.
 */
class COutPoint {
private:
    TxId txid;
    uint32_t n;

public:
    COutPoint() : txid(), n(-1) {}
    COutPoint(uint256 txidIn, uint32_t nIn) : txid(TxId(txidIn)), n(nIn) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(n);
    }

    bool IsNull() const { return txid.IsNull() && n == uint32_t(-1); }

    const TxId &GetTxId() const { return txid; }
    uint32_t GetN() const { return n; }

    friend bool operator<(const COutPoint &a, const COutPoint &b) {
        int cmp = a.txid.Compare(b.txid);
        return cmp < 0 || (cmp == 0 && a.n < b.n);
    }

    friend bool operator==(const COutPoint &a, const COutPoint &b) {
        return (a.txid == b.txid && a.n == b.n);
    }

    friend bool operator!=(const COutPoint &a, const COutPoint &b) {
        return !(a == b);
    }

    std::string ToString() const;

    friend std::istream& operator>>(std::istream&, COutPoint&);
    friend std::ostream& operator<<(std::ostream&, const COutPoint&);
};

std::istream& operator>>(std::istream&, COutPoint&);
std::ostream& operator<<(std::ostream&, const COutPoint&);

/**
 * An input of a transaction. It contains the location of the previous
 * transaction's output that it claims and a signature that matches the output's
 * public key.
 */
class CTxIn {
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;

    /**
     * Setting nSequence to this value for every input in a transaction disables
     * nLockTime.
     */
    static inline constexpr uint32_t SEQUENCE_FINAL = 0xffffffff;

    /* Below flags apply in the context of BIP 68*/
    /**
     * If this flag set, CTxIn::nSequence is NOT interpreted as a relative
     * lock-time.
     */
    static inline constexpr uint32_t SEQUENCE_LOCKTIME_DISABLE_FLAG = (1 << 31);

    /**
     * If CTxIn::nSequence encodes a relative lock-time and this flag is set,
     * the relative lock-time has units of 512 seconds, otherwise it specifies
     * blocks with a granularity of 1.
     */
    static inline constexpr uint32_t SEQUENCE_LOCKTIME_TYPE_FLAG = (1 << 22);

    /**
     * If CTxIn::nSequence encodes a relative lock-time, this mask is applied to
     * extract that lock-time from the sequence field.
     */
    static inline constexpr uint32_t SEQUENCE_LOCKTIME_MASK = 0x0000ffff;

    /**
     * In order to use the same number of bits to encode roughly the same
     * wall-clock duration, and because blocks are naturally limited to occur
     * every 600s on average, the minimum granularity for time-based relative
     * lock-time is fixed at 512 seconds. Converting from CTxIn::nSequence to
     * seconds is performed by multiplying by 512 = 2^9, or equivalently
     * shifting up by 9 bits.
     */
    static inline constexpr int SEQUENCE_LOCKTIME_GRANULARITY = 9;

    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
    CTxIn() { nSequence = SEQUENCE_FINAL; }

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn = CScript(),
                   uint32_t nSequenceIn = SEQUENCE_FINAL)
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        : prevout(prevoutIn), scriptSig(scriptSigIn), nSequence(nSequenceIn) {}
    CTxIn(TxId prevTxId, uint32_t nOut, CScript scriptSigIn = CScript(),
          uint32_t nSequenceIn = SEQUENCE_FINAL)
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        : CTxIn(COutPoint(prevTxId, nOut), scriptSigIn, nSequenceIn) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(nSequence);
    }

    friend bool operator==(const CTxIn &a, const CTxIn &b) {
        return (a.prevout == b.prevout && a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn &a, const CTxIn &b) { return !(a == b); }

    std::string ToString() const;
};

size_t ser_size(const CTxIn&);

// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class Config; // declared in config.h, but including the header file here brings in additional problem
/**
 * An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut {
public:
    Amount nValue;
    CScript scriptPubKey;

    CTxOut() { SetNull(); }

    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    CTxOut(Amount nValueIn, CScript scriptPubKeyIn)
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
        : nValue(nValueIn), scriptPubKey(scriptPubKeyIn) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(nValue);
        READWRITE(scriptPubKey);
    }

    void SetNull() {
        nValue = Amount(-1);
        scriptPubKey.clear();
    }

    bool IsNull() const { return (nValue == Amount(-1)); }

    Amount GetDustThreshold(bool isGenesisEnabled) const {
        // dust threshold is now hardcoded to 1 satoshi per output
        if (scriptPubKey.IsUnspendable(isGenesisEnabled))
            return Amount{0};

        return Amount{1};
    }

    bool IsDust(bool isGenesisEnabled) const {
        return (nValue < GetDustThreshold(isGenesisEnabled));
    }

    friend bool operator==(const CTxOut &a, const CTxOut &b) {
        return (a.nValue == b.nValue && a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut &a, const CTxOut &b) {
        return !(a == b);
    }

    std::string ToString() const;
};

size_t ser_size(const CTxOut&); 

class CMutableTransaction;

/**
 * Basic transaction serialization format:
 * - int32_t nVersion
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - uint32_t nLockTime
 */
template <typename Stream, typename TxType>
inline void UnserializeTransaction(TxType &tx, Stream &s) {
    s >> tx.nVersion;
    tx.vin.clear();
    tx.vout.clear();
    /* Try to read the vin. In case the dummy is there, this will be read as an
     * empty vector. */
    s >> tx.vin;
    /* We read a non-empty vin. Assume a normal vout follows. */
    s >> tx.vout;
    s >> tx.nLockTime;
}

template <typename Stream, typename TxType>
inline void SerializeTransaction(const TxType &tx, Stream &s) {
    s << tx.nVersion;
    s << tx.vin;
    s << tx.vout;
    s << tx.nLockTime;
}

/**
 * The basic transaction that is broadcasted on the network and contained in
 * blocks. A transaction can contain multiple inputs and outputs.
 */
class CTransaction {
public:
    // Default transaction version.
    static const int32_t CURRENT_VERSION = 2;

    // Changing the default transaction version requires a two step process:
    // first adapting relay policy by bumping MAX_STANDARD_VERSION, and then
    // later date bumping the default CURRENT_VERSION at which point both
    // CURRENT_VERSION and MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION = 2;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
    const int32_t nVersion;
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;
    const uint32_t nLockTime;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

private:
    /** Memory only. */
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const uint256 hash;

    uint256 ComputeHash() const;

public:
    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    explicit CTransaction(const CMutableTransaction &tx);
    explicit CTransaction(CMutableTransaction &&tx);

    template <typename Stream> inline void Serialize(Stream &s) const {
        SerializeTransaction(*this, s);
    }

    /**
     * This deserializing constructor is provided instead of an Unserialize
     * method. Unserialize is not possible, since it would require overwriting
     * const fields.
     */
    template <typename Stream>
    CTransaction(deserialize_type, Stream &s)
        : CTransaction(CMutableTransaction(deserialize, s)) {}

    bool IsNull() const { return vin.empty() && vout.empty(); }

    const TxId GetId() const { return TxId(hash); }
    const TxHash GetHash() const { return TxHash(hash); }

    // Return sum of txouts.
    Amount GetValueOut() const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    /**
     * Get the total transaction size in bytes.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool IsCoinBase() const {
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    friend bool operator==(const CTransaction &a, const CTransaction &b) {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransaction &a, const CTransaction &b) {
        return a.hash != b.hash;
    }

    bool HasP2SHOutput() const;

    std::string ToString() const;
};

size_t ser_size(const CTransaction&);

/**
 * A mutable version of CTransaction.
 */
class CMutableTransaction {
public:
    int32_t nVersion;
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    uint32_t nLockTime;

    CMutableTransaction();
    CMutableTransaction(const CTransaction &tx);

    template <typename Stream> inline void Serialize(Stream &s) const {
        SerializeTransaction(*this, s);
    }

    template <typename Stream> inline void Unserialize(Stream &s) {
        UnserializeTransaction(*this, s);
    }

    template <typename Stream>
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CMutableTransaction(deserialize_type, Stream &s) {
        Unserialize(s);
    }

    /**
     * Compute the id and hash of this CMutableTransaction. This is computed on
     * the fly, as opposed to GetId() and GetHash() in CTransaction, which uses
     * a cached result.
     */
    TxId GetId() const;
    TxHash GetHash() const;

    friend bool operator==(const CMutableTransaction &a,
                           const CMutableTransaction &b) {
        return a.GetId() == b.GetId();
    }
};

using CTransactionRef = std::shared_ptr<const CTransaction>;
using CWeakTransactionRef = std::weak_ptr<const CTransaction>;
static inline CTransactionRef MakeTransactionRef() {
    return std::make_shared<const CTransaction>();
}
template <typename Tx>
static inline CTransactionRef MakeTransactionRef(Tx &&txIn) {
    return std::make_shared<const CTransaction>(std::forward<Tx>(txIn));
}

/** Precompute sighash midstate to avoid quadratic hashing */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct PrecomputedTransactionData {
    uint256 hashPrevouts, hashSequence, hashOutputs;

    PrecomputedTransactionData() = default;
    PrecomputedTransactionData(const PrecomputedTransactionData&) = default;
    PrecomputedTransactionData& operator=(const PrecomputedTransactionData&) = default;

    PrecomputedTransactionData(const CTransaction &tx);
};

// Test for double-spend notification enabled output on a transaction
[[nodiscard]] std::pair<bool, size_t> TxnHasDSNotificationOutput(const CTransaction& txn);

#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
