// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <algorithm>

#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

/**
 * Nodes collect new transactions into a block, hash them into a hash tree, and
 * scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements. When they solve the proof-of-work, they broadcast the block to
 * everyone and the block is added to the block chain. The first transaction in
 * the block is a special one that creates a new coin owned by the creator of
 * the block.
 */
class CBlockHeader {
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CBlockHeader() { SetNull(); }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    }

    // Returns header size in bytes
    size_t GetHeaderSize();

    void SetNull() {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const { return (nBits == 0); }

    uint256 GetHash() const;

    int64_t GetBlockTime() const { return (int64_t)nTime; }
};

inline bool operator==(const CBlockHeader& a, const CBlockHeader& b)
{
    return a.nVersion == b.nVersion &&
     a.hashPrevBlock == b.hashPrevBlock &&
     a.hashMerkleRoot == b.hashMerkleRoot &&
     a.nTime == b.nTime &&
     a.nBits == b.nBits && 
     a.nNonce == b.nNonce;
}

inline bool operator!=(const CBlockHeader& a, const CBlockHeader& b)
{
    return !(a == b);
}

std::ostream& operator<<(std::ostream&, const CBlockHeader&);

class CBlock : public CBlockHeader 
{
public:
    // network and disk
    std::vector<CTransactionRef> vtx;

    // memory only
    mutable bool fChecked;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CBlock() { SetNull(); }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CBlock(const CBlockHeader &header) {
        SetNull();
        *((CBlockHeader *)this) = header;
    }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(*(CBlockHeader *)this);
        READWRITE(vtx);
    }

    int32_t
    GetHeightFromCoinbase() const; // Returns the block's height as specified in
                                   // its coinbase transaction

    // Get number of transactions in block
    size_t GetTransactionCount();

    // Returns block size in bytes without coinbase transaction
    size_t GetSizeWithoutCoinbase();

    void SetNull() {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
    }

    CBlockHeader GetBlockHeader() const {
        CBlockHeader block;
        block.nVersion = nVersion;
        block.hashPrevBlock = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime = nTime;
        block.nBits = nBits;
        block.nNonce = nNonce;
        return block;
    }

    std::string ToString() const;

    auto cbegin() const { return vtx.cbegin(); };
    auto cend() const { return vtx.cend(); };
};

inline auto find_tx_by_id(const CBlock& block, const uint256& txid)
{
    return std::find_if(block.cbegin(),
                        block.cend(),
                        [&txid](const auto& tx) {
                            return tx->GetId() == txid;
                        });
}

size_t ser_size(const CBlock&);

typedef std::shared_ptr<CBlock> CBlockRef;

/**
 * Describes a place in the block chain to another node such that if the other
 * node doesn't have the same branch, it can find a recent common trunk.  The
 * further back it is, the further before the fork it may be.
 */
struct CBlockLocator {
    std::vector<uint256> vHave;

    CBlockLocator() {}

    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
    CBlockLocator(const std::vector<uint256> &vHaveIn) { vHave = vHaveIn; }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull() { vHave.clear(); }

    bool IsNull() const { return vHave.empty(); }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
