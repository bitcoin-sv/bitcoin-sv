// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "pubkey.h"
#include "serialize.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

class CKey;

/**
 * A class to encapsulate a miner ID revokemid P2P message.
 */
class RevokeMid
{
    // Fixed size public keys
    static constexpr size_t KEY_LENGTH {33};

  public:

    // The only currently supported message version is 0
    static constexpr uint32_t MSG_VERSION { 0x00 };

    RevokeMid() = default;
    RevokeMid(const CKey& revocationKey, const CKey& minerId, const CPubKey& idToRevoke);
    RevokeMid(const CPubKey& revocationKey, const CPubKey& minerId, const CPubKey& idToRevoke,
              const std::vector<uint8_t>& sig1, const std::vector<uint8_t>& sig2);

    // Accessors
    [[nodiscard]] uint32_t GetVersion() const { return mVersion; }
    [[nodiscard]] const CPubKey& GetRevocationKey() const { return mRevocationKey; }
    [[nodiscard]] const std::array<uint8_t, 33>& GetEncodedRevocationKey() const { return mEncodedRevocationKey; }
    [[nodiscard]] const CPubKey& GetMinerId() const { return mMinerId; }
    [[nodiscard]] const std::array<uint8_t, 33>& GetEncodedMinerId() const { return mEncodedMinerId; }
    [[nodiscard]] const CPubKey& GetRevocationMessage() const { return mRevocationMessage; }
    [[nodiscard]] const std::array<uint8_t, 33>& GetEncodedRevocationMessage() const { return mEncodedRevocationMessage; }
    [[nodiscard]] const std::vector<uint8_t>& GetSig1() const { return mSig1; }
    [[nodiscard]] const std::vector<uint8_t>& GetSig2() const { return mSig2; }
    [[nodiscard]] const std::vector<uint8_t>& GetEncodedRevocationMessageSig() const { return mEncodedRevocationMessageSig; }

    // Verify our signatures
    [[nodiscard]] bool VerifySignatures() const;

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
                throw std::runtime_error("Unsupported RevokeMid message version");
            }
        }

        READWRITE(mEncodedRevocationKey);
        READWRITE(mEncodedMinerId);
        READWRITE(mEncodedRevocationMessage);
        READWRITE(mEncodedRevocationMessageSig);

        if(ser_action.ForRead())
        {
            // Check encoded fields
            if(mEncodedRevocationKey.size() != KEY_LENGTH || mEncodedMinerId.size() != KEY_LENGTH ||
               mEncodedRevocationMessage.size() != KEY_LENGTH)
            {
                throw std::runtime_error("Bad size for RevokeMid message key field");
            }

            // Decode encoded keys
            mRevocationKey =  { mEncodedRevocationKey.begin(), mEncodedRevocationKey.end() };
            mMinerId = { mEncodedMinerId.begin(), mEncodedMinerId.end() };
            mRevocationMessage = { mEncodedRevocationMessage.begin(), mEncodedRevocationMessage.end() };
            if(!mRevocationKey.IsFullyValid() || !mMinerId.IsFullyValid() || !mRevocationMessage.IsFullyValid())
            {
                throw std::runtime_error("Invalid key received in RevokeMid message");
            }

            // Decode encoded signatures
            uint8_t sig1Len { mEncodedRevocationMessageSig.at(0) };
            uint8_t sig2Len { mEncodedRevocationMessageSig.at(sig1Len + 1) }; 
            if(mEncodedRevocationMessageSig.size() != sig1Len + sig2Len + 2U)
            {
                throw std::runtime_error("Bad size for RevokeMid message signature field");
            }
            mSig1 = { mEncodedRevocationMessageSig.begin() + 1, mEncodedRevocationMessageSig.begin() + 1 + sig1Len };
            mSig2 = { mEncodedRevocationMessageSig.begin() + 1 + sig1Len + 1, mEncodedRevocationMessageSig.begin() + 1 + sig1Len + 1 + sig2Len };
        }
    }

    // Unit testing support
    template <typename T>
    struct UnitTestAccess;

  private:

    // Protocol version for this message
    uint32_t mVersion { MSG_VERSION };

    // Current revocation public key of the miner sending this message
    CPubKey mRevocationKey {};
    std::array<uint8_t, KEY_LENGTH> mEncodedRevocationKey {};

    // Current ID of the miner sending this message
    CPubKey mMinerId {};
    std::array<uint8_t, KEY_LENGTH> mEncodedMinerId {};

    // Revocation message field; the compromised miner ID we should revoke back to
    CPubKey mRevocationMessage {};
    std::array<uint8_t, KEY_LENGTH> mEncodedRevocationMessage {};

    // The 2 signatures which comprise the revocationMessageSig field
    std::vector<uint8_t> mSig1 {};
    std::vector<uint8_t> mSig2 {};
    std::vector<uint8_t> mEncodedRevocationMessageSig {};
};

// Stream output
std::ostream& operator<<(std::ostream& str, const RevokeMid& msg);

// Equality
bool operator==(const RevokeMid& msg1, const RevokeMid& msg2);

// Specialise std::hash
namespace std
{
    template<>
    struct hash<RevokeMid>
    {   
        size_t operator()(const RevokeMid&) const;
    };
}

