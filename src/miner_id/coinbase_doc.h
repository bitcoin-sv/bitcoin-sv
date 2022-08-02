// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "miner_id/miner_info_doc.h"

#include "pubkey.h"
#include "uint256.h"

#include "primitives/transaction.h"
#include "univalue.h"

static const std::set<std::string> SUPPORTED_VERSIONS = {"0.1", "0.2", "0.3"};

/**
 * Encapsulate miner id coinbase document as embedded in an OP_RETURN
 * output.
 *
 * Fields miner_contact and extensions are optional in minerId, but we decide
 * not to store them as they are not needed in bitcoind. Field dynamicMinerId is
 * used when verifying signature of dynamic signature, but there is no need to
 * store it though.
 */
class CoinbaseDocument
{
public:
    struct DataRef
    {
        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(brfcIds);
            READWRITE(txid);
            READWRITE(vout);
            READWRITE(compress);
        }

        std::vector<std::string> brfcIds;
        TxId txid;
        int32_t vout;
        std::string compress;
    };

    struct RevocationMessage
    {
        RevocationMessage() = default;

        RevocationMessage(const std::string& compromisedId)
        : mCompromisedId{compromisedId}
        {}

        RevocationMessage(const CPubKey& key)
        : RevocationMessage{HexStr(key)}
        {}

        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mCompromisedId);
        }

        std::string mCompromisedId {};
    };

    friend bool operator==(const DataRef&, const DataRef&);
    friend bool operator==(const RevocationMessage&, const RevocationMessage&);

    CoinbaseDocument() = default;
    CoinbaseDocument(
        const std::string& rawJSON,
        const std::string& version,
        const int32_t height,
        const std::string& prevMinerId,
        const std::string& prevMinerIdSig,
        const std::string& minerId,
        const COutPoint& vctx,
        const std::optional<UniValue>& miner_contact = std::nullopt)
        : mRawJSON{rawJSON},
          mVersion{version},
          mHeight{height},
          mPrevMinerId{prevMinerId},
          mPrevMinerIdSig{prevMinerIdSig},
          mMinerId{minerId},
          mVctx{vctx},
          mMinerContact{miner_contact}
    {
    }

    CoinbaseDocument(std::string_view rawJSON, const miner_info_doc& minerInfoDoc);

    void SetDataRefs(const std::optional<std::vector<DataRef>>& dataRefs)
    {
        mDataRefs = dataRefs;
    }

    const std::string& GetRawJSON() const { return mRawJSON; }
    const std::string& GetVersion() const { return mVersion; }
    int32_t GetHeight() const { return mHeight; }
    const std::string& GetPrevMinerId() const { return mPrevMinerId; }
    CPubKey GetPrevMinerIdAsKey() const { return {ParseHex(mPrevMinerId)}; }
    const std::string& GetPrevMinerIdSig() const { return mPrevMinerIdSig; }
    const std::string& GetMinerId() const { return mMinerId; }
    CPubKey GetMinerIdAsKey() const { return {ParseHex(mMinerId)}; }
    const COutPoint& GetVctx() const { return mVctx; }
    const std::optional<std::vector<DataRef>>& GetDataRefs() const { return mDataRefs; }
    const std::optional<UniValue>& GetMinerContact() const { return mMinerContact; }

    CPubKey GetPrevRevocationKey() const { return {ParseHex(mPrevRevocationKey)}; }
    CPubKey GetRevocationKey() const { return {ParseHex(mRevocationKey)}; }
    const std::optional<RevocationMessage>& GetRevocationMessage() const { return mRevocationMessage; }

    // Allow serialisation/deserialisation
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mRawJSON);
        READWRITE(mVersion);
        READWRITE(mHeight);
        READWRITE(mPrevMinerId);
        READWRITE(mPrevMinerIdSig);
        READWRITE(mMinerId);
        READWRITE(mPrevRevocationKey);
        READWRITE(mRevocationKey);
        READWRITE(mRevocationMessage);
        READWRITE(mVctx);

        // Optional members
        if(ser_action.ForRead())
        {
            // DataRefs
            bool gotDataRefs{};
            READWRITE(gotDataRefs);
            if(gotDataRefs)
            {
                std::vector<DataRef> dataRefs{};
                READWRITE(dataRefs);
                mDataRefs = dataRefs;
            }
            else
            {
                mDataRefs = std::nullopt;
            }

            // Miner contact details
            bool gotMinerContact{};
            READWRITE(gotMinerContact);
            if(gotMinerContact)
            {
                std::string minerContactStr;
                READWRITE(minerContactStr);
                mMinerContact = UniValue{};
                mMinerContact->read(minerContactStr);
            }
            else
            {
                mMinerContact = std::nullopt;
            }
        }
        else
        {
            // DataRefs
            if(mDataRefs)
            {
                bool gotDataRefs{true};
                READWRITE(gotDataRefs);
                READWRITE(*mDataRefs);
            }
            else
            {
                bool gotDataRefs{false};
                READWRITE(gotDataRefs);
            }

            // Miner contact details
            if(mMinerContact)
            {
                bool gotMinerContact{true};
                READWRITE(gotMinerContact);
                std::string minerContactStr{mMinerContact->write()};
                READWRITE(minerContactStr);
            }
            else
            {
                bool gotMinerContact{false};
                READWRITE(gotMinerContact);
            }
        }
    }

private:
    // Raw JSON we're parsed from
    std::string mRawJSON {};
    // MinerId implementation version number: should be present in
    // SUPPORTED_VERSIONS
    std::string mVersion;
    // block height in which MinerId document is included
    int32_t mHeight{-1};
    // previous MinerId public key, a 33 byte hex
    std::string mPrevMinerId;
    // signature on message = concat(prevMinerId, MinerId, vctxid) using the
    // private key associated with the prevMinerId public key, 70-73 byte hex
    // (note that the concatenation is done on the hex encoded bytes)
    std::string mPrevMinerIdSig;
    // current MinerId ECDSA (secp256k1) public key represented in compressed
    // form as a 33 byte hex string
    std::string mMinerId;
    // validity check transaction output that determines whether the MinerId is
    // still valid
    COutPoint mVctx;
    // list of transactions containing additional coinbase document data
    std::optional<std::vector<DataRef>> mDataRefs{std::nullopt};

    // Revocation key/message
    std::string mPrevRevocationKey;
    std::string mRevocationKey;
    std::optional<RevocationMessage> mRevocationMessage {std::nullopt};

    // miner_contact fields
    std::optional<UniValue> mMinerContact{std::nullopt};

    friend bool operator==(const CoinbaseDocument&, const CoinbaseDocument&);
    friend std::ostream& operator<<(std::ostream&, const CoinbaseDocument&);
};

inline bool operator!=(const CoinbaseDocument& a, const CoinbaseDocument& b)
{
    return !(a == b);
}

std::string to_json(const CoinbaseDocument&);
