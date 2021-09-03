// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "double_spend/dsdetected_message.h"
#include "config.h"

// Convert to JSON suitable for sending to a remote webhoo
UniValue DSDetected::ToJSON(const Config& config) const
{
    UniValue document{UniValue::VOBJ};

    document.push_back(Pair("version", mVersion));

    UniValue blocks{UniValue::VARR};
    for(const auto& block : mBlockList)
    {
        UniValue blockJson{UniValue::VOBJ};

        UniValue headers{UniValue::VARR};
        for(const auto& header : block.mHeaderList)
        {
            UniValue headerJson{UniValue::VOBJ};
            headerJson.push_back(Pair("version", header.nVersion));
            headerJson.push_back(
                Pair("hashPrevBlock", header.hashPrevBlock.ToString()));
            headerJson.push_back(
                Pair("hashMerkleRoot", header.hashMerkleRoot.ToString()));
            headerJson.push_back(
                Pair("time", static_cast<uint64_t>(header.nTime)));
            headerJson.push_back(
                Pair("bits", static_cast<uint64_t>(header.nBits)));
            headerJson.push_back(
                Pair("nonce", static_cast<uint64_t>(header.nNonce)));
            headers.push_back(headerJson);
        }
        blockJson.push_back(Pair("headers", headers));

        blockJson.push_back(
            Pair("merkleProof",
                 block.mMerkleProof.ToJSON(
                     config.GetDoubleSpendDetectedWebhookMaxTxnSize())));

        blocks.push_back(blockJson);
    }
    document.push_back(Pair("blocks", blocks));

    return document;
}
