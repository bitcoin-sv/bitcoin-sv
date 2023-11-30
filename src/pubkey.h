// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PUBKEY_H
#define BITCOIN_PUBKEY_H

#include "hash.h"
#include "serialize.h"
#include "uint256.h"

#include <span>
#include <stdexcept>
#include <vector>

/**
 * secp256k1:
 * const unsigned int PRIVATE_KEY_SIZE = 279;
 * const unsigned int PUBLIC_KEY_SIZE  = 65;
 * const unsigned int SIGNATURE_SIZE   = 72;
 *
 * see www.keylength.com
 * script supports up to 75 for single byte push
 */

const unsigned int BIP32_EXTKEY_SIZE = 74;

/** A reference to a CKey: the Hash160 of its serialized public key */
class CKeyID : public uint160 {
public:
    CKeyID() : uint160() {}
    CKeyID(const uint160 &in) : uint160(in) {}
};

typedef uint256 ChainCode;

/** An encapsulated public key. */
class CPubKey {
private:
    /**
     * Just store the serialized data.
     * Its length can very cheaply be computed from the first byte.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    uint8_t vch[65];

    //! Compute the length of a pubkey with a given first byte.
    static unsigned int GetLen(uint8_t chHeader) {
        if (chHeader == 2 || chHeader == 3) return 33;
        if (chHeader == 4 || chHeader == 6 || chHeader == 7) return 65;
        return 0;
    }

    //! Set this key data to be invalid
    void Invalidate() { vch[0] = 0xFF; }

public:
    //! Construct an invalid public key.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CPubKey() { Invalidate(); }

    //! Initialize a public key using begin/end iterators to byte data.
    template <typename T> void Set(const T pbegin, const T pend) {
        auto len = pend == pbegin ? 0u : GetLen(pbegin[0]);
        if (len && len == (pend - pbegin))
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
            // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
            memcpy(vch, (uint8_t *)&pbegin[0], len);
        else
            Invalidate();
    }

    //! Construct a public key using begin/end iterators to byte data.
    template <typename T> 
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CPubKey(const T pbegin, const T pend) {
        Set(pbegin, pend);
    }

    //! Construct a public key from a byte vector.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CPubKey(const std::vector<uint8_t> &_vch) { Set(_vch.begin(), _vch.end()); }

    //! Simple read-only vector-like interface to the pubkey data.
    unsigned int size() const { return GetLen(vch[0]); }
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    const uint8_t *begin() const { return vch; }
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    const uint8_t *end() const { return vch + size(); }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    const uint8_t &operator[](unsigned int pos) const { return vch[pos]; }

    //! Comparator implementation.
    friend bool operator==(const CPubKey &a, const CPubKey &b) {
        // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
        return a.vch[0] == b.vch[0] && memcmp(a.vch, b.vch, a.size()) == 0;
    }
    friend bool operator!=(const CPubKey &a, const CPubKey &b) {
        return !(a == b);
    }
    friend bool operator<(const CPubKey &a, const CPubKey &b) {
        return a.vch[0] < b.vch[0] ||
               // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
               (a.vch[0] == b.vch[0] && memcmp(a.vch, b.vch, a.size()) < 0);
    }

    //! Implement serialization, as if this was a byte vector.
    template <typename Stream> void Serialize(Stream &s) const {
        unsigned int len = size();
        ::WriteCompactSize(s, len);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
        s.write((char *)vch, len);
    }
    template <typename Stream> void Unserialize(Stream &s) {
        unsigned int len = ::ReadCompactSize(s);
        if (len <= 65) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
            s.read((char *)vch, len);
        } else {
            // invalid pubkey, skip available data
            char dummy; // NOLINT(cppcoreguidelines-init-variables)
            while (len--)
                s.read(&dummy, 1);
            Invalidate();
        }
    }

    //! Get the KeyID of this public key (hash of its serialization)
    // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    CKeyID GetID() const { return CKeyID(Hash160(vch, vch + size())); }

    //! Get the 256-bit hash of this public key.
    // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
    uint256 GetHash() const { return Hash(vch, vch + size()); }

    /*
     * Check syntactic correctness.
     *
     * Note that this is consensus critical as CheckSig() calls it!
     */
    bool IsValid() const { return size() > 0; }

    //! fully validate whether this is a valid public key (more expensive than
    //! IsValid())
    bool IsFullyValid() const;

    //! Check whether this is a compressed public key.
    bool IsCompressed() const { return size() == 33; }

    /**
     * Verify a DER signature (~72 bytes).
     * If this public key is not fully valid, the return value will be false.
     */
    bool Verify(const uint256& hash, const std::vector<uint8_t>& vchSig) const;
    bool Verify(const uint256& hash, const std::span<const uint8_t> sig) const;

    /**
     * Check whether a signature is normalized (lower-S).
     */
    static bool CheckLowS(const std::vector<uint8_t> &vchSig);

    //! Recover a public key from a compact signature.
    bool RecoverCompact(const uint256 &hash,
                        const std::vector<uint8_t> &vchSig);

    //! Turn this public key into an uncompressed public key.
    bool Decompress();

    //! Derive BIP32 child pubkey.
    bool Derive(CPubKey &pubkeyChild, ChainCode &ccChild, unsigned int nChild,
                const ChainCode &cc) const;
};

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct CExtPubKey {
    uint8_t nDepth;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    uint8_t vchFingerprint[4];
    unsigned int nChild;
    ChainCode chaincode;
    CPubKey pubkey;

    friend bool operator==(const CExtPubKey &a, const CExtPubKey &b) {
        return a.nDepth == b.nDepth &&
               memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0],
                      sizeof(vchFingerprint)) == 0 &&
               a.nChild == b.nChild && a.chaincode == b.chaincode &&
               a.pubkey == b.pubkey;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    void Encode(uint8_t code[BIP32_EXTKEY_SIZE]) const;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    void Decode(const uint8_t code[BIP32_EXTKEY_SIZE]);
    bool Derive(CExtPubKey &out, unsigned int nChild) const;

    void Serialize(CSizeComputer &s) const {
        // Optimized implementation for ::GetSerializeSize that avoids copying.
        // add one byte for the size (compact int)
        s.seek(BIP32_EXTKEY_SIZE + 1);
    }
    template <typename Stream> void Serialize(Stream &s) const {
        unsigned int len = BIP32_EXTKEY_SIZE;
        ::WriteCompactSize(s, len);
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
        uint8_t code[BIP32_EXTKEY_SIZE];
        // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
        Encode(code);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
        s.write((const char *)&code[0], len);
    }
    template <typename Stream> void Unserialize(Stream &s) {
        unsigned int len = ::ReadCompactSize(s);
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
        uint8_t code[BIP32_EXTKEY_SIZE];
        if (len != BIP32_EXTKEY_SIZE)
            throw std::runtime_error("Invalid extended key size\n");
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
        s.read((char *)&code[0], len);
        // NOLINTNEXTLINE-cppcoreguidelines-pro-bounds-array-to-pointer-decay,
        Decode(code);
    }
};

#endif // BITCOIN_PUBKEY_H
