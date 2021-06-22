// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <string>
#include <vector>

#include "primitives/transaction.h"
#include "univalue.h"

/**
 * Encapsulate miner id coinbase document as embedded in an OP_RETURN
 * output.
 *
 * Fields minerContact and extensions are optional in minerId, but we decide not to store them as they are not needed in bitcoind.
 * Field dynamicMinerId is used when verifying signature of dynamic signature, but there is no need to store it though.
 */
class CoinbaseDocument
{
  public:

    struct DataRef {
        std::vector<std::string> brfcIds;
        uint256 txid;
        int32_t vout;
    };

    CoinbaseDocument() = default;
    CoinbaseDocument(const std::string& version, const int32_t height, const std::string& prevMinerId,
        const std::string& prevMinerIdSig, const std::string& minerId, const COutPoint& mVctx)
    : mVersion(version), mHeight(height), mPrevMinerId(prevMinerId), mPrevMinerIdSig(prevMinerIdSig),
      mMinerId(minerId), mVctx(mVctx)
    {
    }

    void SetDataRefs(const std::optional<std::vector<DataRef>>& dataRefs)
    {
        mDataRefs = dataRefs;
    }

    const std::string& GetVersion() const { return mVersion; }
    int32_t GetHeight() const { return mHeight; }
    const std::string& GetPrevMinerId() const { return mPrevMinerId; }
    const std::string& GetPrevMinerIdSig() const { return mPrevMinerIdSig; }
    const std::string& GetMinerId() const { return mMinerId; }
    const COutPoint& GetVctx() const { return mVctx; }
    const std::optional<std::vector<DataRef>>& GetDataRefs() const { return mDataRefs; }

  private:
    // MinerId implementation version number
    std::string mVersion;
    // block height in which MinerId document is included
    int32_t mHeight {-1};
    //previous MinerId public key, a 33 byte hex
    std::string mPrevMinerId;
    // signature on message = concat(prevMinerId, MinerId, vctxid) using the private key
    // associated with the prevMinerId public key, 70-73 byte hex (note that the concatenation is done on the hex encoded bytes)
    std::string mPrevMinerIdSig;
    // current MinerId ECDSA (secp256k1) public key represented in compressed form as a 33 byte hex string
    std::string mMinerId;
    // validity check transaction output that determines whether the MinerId is still valid
    COutPoint mVctx;
    // list of transactions containing additional coinbase document data
    std::optional<std::vector<DataRef>> mDataRefs {std::nullopt};
};

/* The MinerId provides a way of cryptographically identifying miners. A MinerId is a public key of an ECDSA keypair.
 * It is used to sign a coinbase document and is included as an OP_RETURN output in the coinbase transaction of a block.
 * MinerId is a voluntary extra service that miners can offer and is in no way mandatory.

 * MinerId consists of static and dynamic coinbase document.
 * If static coinbase document is present, it must have all the required fields (version, height, prevMinerId, prevMinerIdSig, minerId, vctx) and valid signature.
 * Dynamic coinbase document is not mandatory. If static document is invalid/missing, dynamic document is not even validated.
 * If dynamic document is present, it must have valid signature over concat(staticCoinbaseDocument + sig (staticCoinbaseDocument) + dynamicCoinbaseDocument).
 * It is not valid for a dynamic field to overwrite the value of a field in the static part of the document without specifically being authorised in the static document.
 * Currently, because there is no authorization mechanism, the dynamic value should be ignored when merging the documents.
 */
class MinerId
{
  public:

    MinerId() = default;

    MinerId(const CoinbaseDocument& coinbaseDocument)
    : mCoinbaseDocument(coinbaseDocument)
    {
    };

    CoinbaseDocument& GetCoinbaseDocument() { return mCoinbaseDocument; }

    const CoinbaseDocument& GetCoinbaseDocument() const { return mCoinbaseDocument; }

    const std::string& GetStaticDocumentJson() const
    {
        return mStaticDocumentJson;
    }

    const std::string& GetSignatureStaticDocument() const
    {
        return mSignatureStaticDocument;
    }

    // Parse static coinbase document from coinbaseDocumentData and store it only if it is valid (if method returns true).
    // Parameter tx_out is used only for logging purposes.
    // Also set mStaticDocumentJson and mSignatureStaticDocument if validation was successful.
    bool SetStaticCoinbaseDocument(const UniValue& coinbaseDocumentData, std::vector<uint8_t>& signatureBytes, const COutPoint& tx_out, int32_t blockHeight);

    // Parse dynamic coinbase document from coinbaseDocumentData and store it only if it is valid (if method returns true).
    // Parameter tx_out is used only for logging purposes.
    bool SetDynamicCoinbaseDocument(const UniValue& coinbaseDocumentData, std::vector<uint8_t>& signatureBytes, const COutPoint& tx_out, int32_t blockHeight);

    bool parseCoinbaseDocument(std::string& coinbaseDocumentDataJson, std::vector<uint8_t>& signatureBytes, const COutPoint& tx_out, int32_t blockHeight, bool dynamic);

    static constexpr uint8_t protocol_id[] = { 0xac, 0x1e, 0xed, 0x88 };

    /* Scan coinbase transaction outputs for minerId. When first valid miner id is found, stop scanning.
     * If miner id was not found (or it was invalid), return std::nullopt.
     * Parameter tx is coinbase transaction that we scan for miner id output.
     * Parameter blockHeight is current block height. It should match with height in parsed miner id.
     */
    static std::optional<MinerId> FindMinerId(const CTransaction& tx, int32_t blockHeight);

  private:
    CoinbaseDocument mCoinbaseDocument;
    std::string mStaticDocumentJson;
    std::string mSignatureStaticDocument;
};
