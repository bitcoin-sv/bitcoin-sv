// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/revokemid.h"
#include "key.h"
#include "utilstrencodings.h"

#include <iomanip>
#include <iosfwd>
#include <sstream>
#include <string>

namespace
{
    // Return hex encoded bytes for the given sequence
    template<typename Src>
    std::vector<uint8_t> EncodeHexStr(const Src& src)
    {
        const std::string hex { HexStr(src) };
        std::vector<uint8_t> bytes {};
        transform_hex(hex, back_inserter(bytes));
        return bytes;
    }

    // Hex encode a key field
    template<typename Dest>
    void EncodeHexKey(const CPubKey& key, Dest& dest)
    {
        auto hex { EncodeHexStr(key) };
        assert(hex.size() == 33);
        std::copy(hex.begin(), hex.end(), dest.begin());
    }

    // Hex encode revocation message signature field
    std::vector<uint8_t> HexEncodeRevocationMessageSig(const RevokeMid& msg)
    {
        std::vector<uint8_t> encodedRevocationMessageSig {};

        const std::vector<uint8_t>& sig1 { msg.GetSig1() };
        const std::vector<uint8_t>& sig2 { msg.GetSig2() };

        std::stringstream sig1LenStr {};
        sig1LenStr << std::setfill('0') << std::setw(2) << std::right << std::hex << sig1.size();
        assert(sig1LenStr.str().size() == 2);
        const std::string sig1Str { HexStr(sig1) };

        std::stringstream sig2LenStr {};
        sig2LenStr << std::setfill('0') << std::setw(2) << std::right << std::hex << sig2.size();
        assert(sig2LenStr.str().size() == 2);
        const std::string sig2Str { HexStr(sig2) };

        transform_hex(sig1LenStr.str(), back_inserter(encodedRevocationMessageSig));
        transform_hex(sig1Str, back_inserter(encodedRevocationMessageSig));
        transform_hex(sig2LenStr.str(), back_inserter(encodedRevocationMessageSig));
        transform_hex(sig2Str, back_inserter(encodedRevocationMessageSig));

        return encodedRevocationMessageSig;
    }

    // Make hash of revocation message
    uint256 HashRevocationMessage(const RevokeMid& msg)
    {
        const auto& revocationMessage { msg.GetEncodedRevocationMessage() };
        uint8_t hashRevocationMessage[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(reinterpret_cast<const uint8_t*>(revocationMessage.data()), revocationMessage.size()).Finalize(hashRevocationMessage);
        return uint256 { std::vector<uint8_t> {std::begin(hashRevocationMessage), std::end(hashRevocationMessage)} };
    }
}

RevokeMid::RevokeMid(const CKey& revocationKey, const CKey& minerId, const CPubKey& idToRevoke)
: mRevocationKey{revocationKey.GetPubKey()}, mMinerId{minerId.GetPubKey()},
  mRevocationMessage{idToRevoke}
{
    // Hex encode key fields
    EncodeHexKey(mRevocationKey, mEncodedRevocationKey);
    EncodeHexKey(mMinerId, mEncodedMinerId);
    EncodeHexKey(mRevocationMessage, mEncodedRevocationMessage);

    // Make signatures
    const uint256& hashRevocationMessage { HashRevocationMessage(*this) };
    revocationKey.Sign(hashRevocationMessage, mSig1);
    minerId.Sign(hashRevocationMessage, mSig2);

    // Hex encode revocation message signature field
    mEncodedRevocationMessageSig = HexEncodeRevocationMessageSig(*this);
}

RevokeMid::RevokeMid(const CPubKey& revocationKey, const CPubKey& minerId, const CPubKey& idToRevoke,
    const std::vector<uint8_t>& sig1, const std::vector<uint8_t>& sig2)
: mRevocationKey{revocationKey}, mMinerId{minerId}, mRevocationMessage{idToRevoke},
  mSig1{sig1}, mSig2{sig2}
{
    // Hex encode key fields
    EncodeHexKey(mRevocationKey, mEncodedRevocationKey);
    EncodeHexKey(mMinerId, mEncodedMinerId);
    EncodeHexKey(mRevocationMessage, mEncodedRevocationMessage);

    // Hex encode revocation message signature field
    mEncodedRevocationMessageSig = HexEncodeRevocationMessageSig(*this);
}

// Verify our signatures
bool RevokeMid::VerifySignatures() const
{
    // Get hash of revocation message
    const uint256& hashRevocationMessage { HashRevocationMessage(*this) };
    return mRevocationKey.Verify(hashRevocationMessage, mSig1) && mMinerId.Verify(hashRevocationMessage, mSig2);
}

// Stream output
std::ostream& operator<<(std::ostream& str, const RevokeMid& msg)
{
    str << "Version: " << msg.GetVersion() << std::endl;
    str << "RevocationKey: " << msg.GetRevocationKey().GetHash().ToString() << std::endl;
    str << "EncodedRevocationKey: " << HexStr(msg.GetEncodedRevocationKey()) << std::endl;
    str << "MinerId: " << msg.GetMinerId().GetHash().ToString() << std::endl;
    str << "EncodedMinerId: " << HexStr(msg.GetEncodedMinerId()) << std::endl;
    str << "RevocationMessage: " << msg.GetRevocationMessage().GetHash().ToString() << std::endl;
    str << "EncodedRevocationMessage: " << HexStr(msg.GetEncodedRevocationMessage()) << std::endl;
    str << "Sig1: " << HexStr(msg.GetSig1()) << std::endl;
    str << "Sig2: " << HexStr(msg.GetSig2()) << std::endl;
    str << "EncodedRevocationMessageSig: " << HexStr(msg.GetEncodedRevocationMessageSig()) << std::endl;
    return str;
}

// Equality
bool operator==(const RevokeMid& msg1, const RevokeMid& msg2)
{
    return msg1.GetVersion() == msg2.GetVersion() &&
           msg1.GetRevocationKey() == msg2.GetRevocationKey() &&
           msg1.GetEncodedRevocationKey() == msg2.GetEncodedRevocationKey() &&
           msg1.GetMinerId() == msg2.GetMinerId() &&
           msg1.GetEncodedMinerId() == msg2.GetEncodedMinerId() &&
           msg1.GetRevocationMessage() == msg2.GetRevocationMessage() &&
           msg1.GetEncodedRevocationMessage() == msg2.GetEncodedRevocationMessage() &&
           msg1.GetSig1() == msg2.GetSig1() &&
           msg1.GetSig2() == msg2.GetSig2() &&
           msg1.GetEncodedRevocationMessageSig() == msg2.GetEncodedRevocationMessageSig();
}

// Specialise std::hash
namespace std
{
    size_t hash<RevokeMid>::operator()(const RevokeMid& rmid) const
    {   
        size_t seed {0};
        boost::hash_combine(seed, rmid.GetVersion());
        boost::hash_range(seed, rmid.GetEncodedRevocationKey().begin(), rmid.GetEncodedRevocationKey().end());
        boost::hash_range(seed, rmid.GetEncodedMinerId().begin(), rmid.GetEncodedMinerId().end());
        boost::hash_range(seed, rmid.GetEncodedRevocationMessage().begin(), rmid.GetEncodedRevocationMessage().end());
        boost::hash_range(seed, rmid.GetEncodedRevocationMessageSig().begin(), rmid.GetEncodedRevocationMessageSig().end());
        return seed;
    };
}

