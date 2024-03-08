// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coinbase_doc.h"
#include "primitives/transaction.h"
#include "univalue.h"

class CBlock;
class miner_info;

/* The MinerId provides a way of cryptographically identifying miners. A MinerId
 is a public key of an ECDSA keypair.
 * It is used to sign a coinbase document and is included as an OP_RETURN output
 in the coinbase transaction of a block.
 * MinerId is a voluntary extra service that miners can offer and is in no way
 mandatory.

 * MinerId consists of static and dynamic coinbase document.
 * If static coinbase document is present, it must have all the required fields
 (version, height, prevMinerId, prevMinerIdSig, minerId, vctx) and valid
 signature.
 * Dynamic coinbase document is not mandatory. If static document is
 invalid/missing, dynamic document is not even validated.
 * If dynamic document is present, it must have valid signature over
 concat(staticCoinbaseDocument + sig (staticCoinbaseDocument) +
 dynamicCoinbaseDocument).
 * It is not valid for a dynamic field to overwrite the value of a field in the
 static part of the document without specifically being authorised in the static
 document.
 * Currently, because there is no authorization mechanism, the dynamic value
 should be ignored when merging the documents.
 */
class MinerId
{
public:
    MinerId() = default;
    MinerId(const miner_info& minerInfo);

    const CoinbaseDocument& GetCoinbaseDocument() const
    {
        return coinbaseDocument_;
    }

    const std::optional<TxId>& GetMinerInfoTx() const { return minerInfoTx_; }

    // Parse static coinbase document from coinbaseDocumentData and store it
    // only if it is valid (if method returns true). Parameter tx_out is used
    // only for logging purposes. Also set staticDocumentJson_ and
    // signatureStaticDocument_ if validation was successful.
    bool SetStaticCoinbaseDocument(const UniValue& coinbaseDocumentData,
                                   const std::span<const uint8_t> signature,
                                   const COutPoint& tx_out,
                                   int32_t blockHeight);

    // Parse dynamic coinbase document from coinbaseDocumentData and store it
    // only if it is valid (if method returns true). Parameter tx_out is used
    // only for logging purposes.
    bool SetDynamicCoinbaseDocument(const UniValue& coinbaseDocumentData,
                                    const std::span<const uint8_t> signature,
                                    const COutPoint& tx_out,
                                    int32_t blockHeight);

private:
    CoinbaseDocument coinbaseDocument_;
    std::string staticDocumentJson_;
    std::string signatureStaticDocument_;
    std::optional<TxId> minerInfoTx_;
};

/* Scan coinbase transaction outputs for minerId. When first valid miner id
 * is found, stop scanning. If miner id was not found (or it was invalid),
 * return std::nullopt. Parameter tx is coinbase transaction that we scan
 * for miner id output. Parameter blockHeight is current block height. It
 * should match with height in parsed miner id.
 */
std::optional<MinerId> FindMinerId(const CBlock& block, int32_t blockHeight);

bool parseCoinbaseDocument(MinerId&,
                           const std::string_view coinbaseDocumentDataJson,
                           const std::span<const uint8_t> signatureBytes,
                           const COutPoint& tx_out,
                           int32_t blockHeight,
                           bool dynamic);
