// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "uint256.h"

#include "primitives/transaction.h"
//#include "univalue.h"

static const std::set<std::string> SUPPORTED_VERSIONS = {"0.1", "0.2"};

/**
 * Encapsulate miner id coinbase document as embedded in an OP_RETURN
 * output.
 *
 * Fields minerContact and extensions are optional in minerId, but we decide not
 * to store them as they are not needed in bitcoind. Field dynamicMinerId is
 * used when verifying signature of dynamic signature, but there is no need to
 * store it though.
 */
class CoinbaseDocument
{
public:
    struct DataRef
    {
        std::vector<std::string> brfcIds;
        uint256 txid;
        int32_t vout;
    };

    friend bool operator==(const DataRef&, const DataRef&);

    CoinbaseDocument() = default;
    CoinbaseDocument(const std::string& version,
                     const int32_t height,
                     const std::string& prevMinerId,
                     const std::string& prevMinerIdSig,
                     const std::string& minerId,
                     const COutPoint& vctx)
        : version_(version),
          height_(height),
          prevMinerId_(prevMinerId),
          prevMinerIdSig_(prevMinerIdSig),
          minerId_(minerId),
          vctx_(vctx)
    {
    }

    void SetDataRefs(const std::optional<std::vector<DataRef>>& dataRefs)
    {
        dataRefs_ = dataRefs;
    }

    const std::string& GetVersion() const { return version_; }
    int32_t GetHeight() const { return height_; }
    const std::string& GetPrevMinerId() const { return prevMinerId_; }
    const std::string& GetPrevMinerIdSig() const { return prevMinerIdSig_; }
    const std::string& GetMinerId() const { return minerId_; }
    const COutPoint& GetVctx() const { return vctx_; }
    const std::optional<std::vector<DataRef>>& GetDataRefs() const
    {
        return dataRefs_;
    }

private:
    // MinerId implementation version number: should be present in
    // SUPPORTED_VERSIONS
    std::string version_;
    // block height in which MinerId document is included
    int32_t height_{-1};
    // previous MinerId public key, a 33 byte hex
    std::string prevMinerId_;
    // signature on message = concat(prevMinerId, MinerId, vctxid) using the
    // private key associated with the prevMinerId public key, 70-73 byte hex
    // (note that the concatenation is done on the hex encoded bytes)
    std::string prevMinerIdSig_;
    // current MinerId ECDSA (secp256k1) public key represented in compressed
    // form as a 33 byte hex string
    std::string minerId_;
    // validity check transaction output that determines whether the MinerId is
    // still valid
    COutPoint vctx_;
    // list of transactions containing additional coinbase document data
    std::optional<std::vector<DataRef>> dataRefs_{std::nullopt};

    friend bool operator==(const CoinbaseDocument&, const CoinbaseDocument&);
    friend std::ostream& operator<<(std::ostream&, const CoinbaseDocument&);
};

inline bool operator!=(const CoinbaseDocument& a, const CoinbaseDocument& b)
{
    return !(a == b);
}
