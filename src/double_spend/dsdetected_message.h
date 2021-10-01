// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include "merkleproof.h"
#include "primitives/block.h"
#include "serialize.h"
#include "univalue.h"

#include <stdexcept>
#include <vector>

class Config;

/**
 * A class to encapsulate a double-spend detected P2P message.
 */
class DSDetected
{
public:
    // The only currently supported message version is 0x01
    static constexpr uint16_t MSG_VERSION{0x01};

    DSDetected() = default;

    // Wrapper type for block details
    struct BlockDetails
    {
        // Serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mBlockHeaders);
            if(mBlockHeaders.empty())
                throw std::runtime_error(
                    "Invalid DSDetected message - no block headers");

            READWRITE(mMerkleProof);
            Validate(mMerkleProof);
        }

        static void Validate(const MerkleProof&);

        // List of block headers from the block containing the conflicting
        // transaction back to the last common ancestor of all detailed blocks.
        std::vector<CBlockHeader> mBlockHeaders{};

        // Merkle-proof containing transaction and proof it is in the first
        // block in the above header list,
        MerkleProof mMerkleProof{};
    };
    using blocks_type = std::vector<BlockDetails>;
    using const_iterator = blocks_type::const_iterator;

    // Accessors
    [[nodiscard]] uint16_t GetVersion() const { return mVersion; }

    [[nodiscard]] bool empty() const noexcept { return mBlockList.empty(); }
    [[nodiscard]] auto size() const noexcept { return mBlockList.size(); }

    [[nodiscard]] const_iterator begin() const noexcept
    {
        return mBlockList.cbegin();
    }
    [[nodiscard]] const_iterator end() const noexcept
    {
        return mBlockList.cend();
    }

    // Serialisation/deserialisation
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mVersion);
        if(ser_action.ForRead())
        {
            // Check message version
            if(mVersion != MSG_VERSION)
            {
                throw std::runtime_error(
                    "Unsupported DSDetected message version");
            }
        }

        READWRITE(mBlockList);
        if(ser_action.ForRead())
        {
            // Check message version
            if(mBlockList.size() < 2)
            {
                throw std::runtime_error("DSDetected invalid block count");
            }
        }
    }

    // Convert to JSON suitable for sending to a remote webhook
    [[nodiscard]] UniValue ToJSON(const Config& config) const;

    // Unit testing support
    template <typename T>
    struct UnitTestAccess;

    friend bool operator==(const DSDetected&, const DSDetected&);

private:
    // Protocol version for this message
    uint16_t mVersion{MSG_VERSION};

    // List of details for blocks double-spends are detected in
    blocks_type mBlockList{};
};

inline bool operator!=(const DSDetected& a, const DSDetected& b)
{
    return !(a == b);
}

bool operator==(const DSDetected::BlockDetails&,
                const DSDetected::BlockDetails&);
inline bool operator!=(const DSDetected::BlockDetails& a,
                       const DSDetected::BlockDetails& b)
{
    return !(a == b);
}

namespace std
{
    template<>
    struct hash<DSDetected>
    {
        std::size_t operator()(const DSDetected&) const;
    };
}

std::size_t sort_hasher(const DSDetected&);

bool IsValid(const DSDetected&);
bool ValidateForkCount(const DSDetected&);
bool ValidateCommonAncestor(const DSDetected&);
bool ValidateDoubleSpends(const DSDetected&);
bool AreTxsUnique(const DSDetected&);

bool IsValid(const DSDetected::BlockDetails&);

bool FormsChain(const std::vector<CBlockHeader>&);
bool ContainsDuplicateHeaders(const std::vector<CBlockHeader>&);

const DSDetected::BlockDetails& MaxForkLength(const DSDetected&);

