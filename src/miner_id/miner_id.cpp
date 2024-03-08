// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "miner_id.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "boost/algorithm/hex.hpp"

#include "hash.h"
#include "logging.h"
#include "miner_id/miner_info.h"
#include "miner_id/miner_info_error.h"
#include "miner_id/miner_info_ref.h"
#include "primitives/block.h"
#include "pubkey.h"
#include "script/instruction.h"
#include "script/instruction_iterator.h"
#include "utilstrencodings.h"

namespace ba = boost::algorithm;

using namespace std;

namespace
{
    // Parse dataRefs field from coinbase document.
    // If signature of current coinbase document is valid, we expect valid
    // transaction references in datarefs field. But it can happen that
    // referenced transactions are not found due to various reasons. Here, we
    // only store transactions and not check their existence. This happens later
    // in the process.
    bool parseDataRefs(const UniValue& coinbaseDocument,
                       std::vector<CoinbaseDocument::DataRef>& dataRefs)
    {
        if(!coinbaseDocument.exists("dataRefs"))
            return true;

        // If dataRefs are present, they have to have the correct structure.
        const auto& data_refs{coinbaseDocument["dataRefs"]};

        if(!data_refs.isObject() || !data_refs.exists("refs") ||
           !data_refs["refs"].isArray())
        {
            return false;
        }

        const UniValue refs = data_refs["refs"].get_array();

        for(size_t i = 0; i < refs.size(); i++)
        {
            if(refs[i].exists("brfcIds") && refs[i]["brfcIds"].isArray() &&
               refs[i].exists("txid") && refs[i]["txid"].isStr() &&
               refs[i].exists("vout") && refs[i]["vout"].isNum())
            {
                std::vector<std::string> brfcIds;
                for(size_t brfcIdx = 0; brfcIdx < refs[i]["brfcIds"].size();
                    brfcIdx++)
                {
                    if(!refs[i]["brfcIds"][brfcIdx].isStr())
                    {
                        // Incorrect structure of member in dataRefs list.
                        return false;
                    }
                    brfcIds.push_back(refs[i]["brfcIds"][brfcIdx].get_str());
                }
                    
                string compress;
                if(refs[i].exists("compress") && refs[i]["compress"].isStr())
                    compress = refs[i]["compress"].get_str();
               
                dataRefs.push_back(CoinbaseDocument::DataRef{
                    brfcIds,
                    TxId { uint256S(refs[i]["txid"].get_str()) },
                    refs[i]["vout"].get_int(),
                    compress});
            }
            else
            {
                // Incorrect structure of member in dataRefs list.
                return false;
            }
        }

        return true;
    }

    template <typename O>
    void hash_sha256(const string_view msg, O o)
    {
        CSHA256()
            .Write(reinterpret_cast<const uint8_t*>(msg.data()), msg.size())
            .Finalize(o);
    }

    bool verify(const string_view msg,
                const vector<uint8_t>& pub_key,
                const std::span<const uint8_t> sig)
    {
        std::vector<uint8_t> hash(CSHA256::OUTPUT_SIZE);
        hash_sha256(msg, hash.data());

        const CPubKey pubKey{pub_key.begin(), pub_key.end()};
        return pubKey.Verify(uint256{hash}, sig);
    }
}

MinerId::MinerId(const miner_info& minerInfo)
: coinbaseDocument_ { minerInfo.raw_mi_doc(), minerInfo.mi_doc() },
  minerInfoTx_ { minerInfo.txid() }
{
}

bool MinerId::SetStaticCoinbaseDocument(
    const UniValue& document,
    const std::span<const uint8_t> signatureBytes,
    const COutPoint& tx_out,
    const int32_t blockHeight)
{
    auto LogInvalidDoc = [&] {
        LogPrint(
            BCLog::MINERID,
            "One or more required parameters from coinbase document missing or "
            "incorrect. Coinbase transaction txid %s and output number %d. \n",
            tx_out.GetTxId().ToString(),
            tx_out.GetN());
    };

    // Check existence and validity of required fields of static coinbase
    // document.
    const auto& version = document["version"];
    if(!version.isStr() || !SUPPORTED_VERSIONS.count(version.get_str()))
    {
        LogInvalidDoc();
        return false;
    }

    auto& height = document["height"];
    int32_t block_height{};
    if(height.isNum())
    {
        block_height = height.get_int();
    }
    else if(height.isStr())
    {
        block_height = std::stoi(height.get_str());
    }
    else
    {
        LogInvalidDoc();
        return false;
    }
    if(block_height != blockHeight)
    {
        LogPrint(BCLog::MINERID,
                 "Block height in coinbase document is incorrect in coinbase "
                 "transaction with txid %s and output number %d. \n",
                 tx_out.GetTxId().ToString(),
                 tx_out.GetN());
        return false;
    }

    auto& prevMinerId = document["prevMinerId"];
    if(!prevMinerId.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& prevMinerIdSig = document["prevMinerIdSig"];
    if(!prevMinerIdSig.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& minerId = document["minerId"];
    if(!minerId.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& vctx = document["vctx"];
    if(!vctx.isObject())
    {
        LogInvalidDoc();
        return false;
    }

    auto& vctxTxid = vctx["txId"];
    if(!vctxTxid.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& vctxVout = vctx["vout"];
    if(!vctxVout.isNum())
    {
        LogInvalidDoc();
        return false;
    }

    // Verify signature of static document miner id.
    const std::string cd_json{document.write()};
    const std::vector<uint8_t> minerIdBytes{ParseHex(minerId.get_str())};
    if(!verify(cd_json, minerIdBytes, signatureBytes))
    {
        LogPrint(BCLog::MINERID,
                 "Signature of static coinbase document is invalid incoinbase "
                 "transaction with txid %s and output number%d.\n",
                 tx_out.GetTxId().ToString(),
                 tx_out.GetN());
        return false;
    }

    // Verify signature of previous miner id.
    string dataToVerify;
    if(version.get_str() == "0.1")
    {
        dataToVerify =
            prevMinerId.get_str() + minerId.get_str() + vctxTxid.get_str();
    }
    else if(version.get_str() == "0.2")
    {
        vector<uint8_t> s;
        transform_hex(prevMinerId.get_str(), back_inserter(s));
        transform_hex(minerId.get_str(), back_inserter(s));
        transform_hex(vctxTxid.get_str(), back_inserter(s));
        dataToVerify = string(s.begin(), s.end());
    }
    else
    {
        LogPrint(BCLog::MINERID,
                 "Unsupported version in miner id in txid %s and output number "
                 "%d. \n",
                 tx_out.GetTxId().ToString(),
                 tx_out.GetN());
        return false;
    }

    const vector<uint8_t> signaturePrevMinerId{
        ParseHex(prevMinerIdSig.get_str())};
    const std::vector<uint8_t> prevMinerIdBytes{
        ParseHex(prevMinerId.get_str())};
    if(!verify(dataToVerify, prevMinerIdBytes, signaturePrevMinerId))
    {
        LogPrint(
            BCLog::MINERID,
            "Signature of previous miner id in coinbase document is invalid in "
            "coinbase transaction with txid %s and output number %d. \n",
            tx_out.GetTxId().ToString(),
            tx_out.GetN());
        return false;
    }

    // Look for minerContact details
    std::optional<UniValue> minerContact {};
    auto& contact = document["minerContact"];
    if(contact.isObject())
    {   
        minerContact = contact;
    }

    CoinbaseDocument coinbaseDocument(
        document.write(),
        version.get_str(),
        block_height,
        prevMinerId.get_str(),
        prevMinerIdSig.get_str(),
        minerId.get_str(),
        COutPoint(uint256S(vctxTxid.get_str()), vctxVout.get_int()),
        minerContact);

    std::vector<CoinbaseDocument::DataRef> dataRefs;
    if(!parseDataRefs(document, dataRefs))
    {
        LogInvalidDoc();
        return false;
    }
    if(!dataRefs.empty())
    {
        coinbaseDocument.SetDataRefs(dataRefs);
    }

    // Set static coinbase document.
    coinbaseDocument_ = coinbaseDocument;
    // Set fields needed for verifying dynamic miner id.
    staticDocumentJson_ = document.write();
    signatureStaticDocument_ = std::string(signatureBytes.begin(), signatureBytes.end());

    return true;
}

bool MinerId::SetDynamicCoinbaseDocument(
    const UniValue& document,
    const std::span<const uint8_t> signatureBytes,
    const COutPoint& tx_out,
    const int32_t blockHeight)
{
    auto LogInvalidDoc = [&] {
        LogPrint(BCLog::MINERID,
                 "Structure in coinbase document is incorrect (incorrect field "
                 "type) in coinbase transaction with txid %s and output number "
                 "%d. \n",
                 tx_out.GetTxId().ToString(),
                 tx_out.GetN());
    };

    // Dynamic document has no required fields (except for dynamic miner id).
    // Check field types if they exist.
    auto& version = document["version"];
    if(!version.isNull() &&
       (!version.isStr() || !SUPPORTED_VERSIONS.count(version.get_str())))
    {
        LogInvalidDoc();
        return false;
    }

    auto& height = document["height"];
    if(!height.isNull())
    {
        if(!height.isNum())
        {
            LogInvalidDoc();
            return false;
        }
        if(height.get_int() != blockHeight)
        {
            LogPrint(
                BCLog::MINERID,
                "Block height in coinbase document is incorrect in coinbase "
                "transaction with txid %s and output number %d. \n",
                tx_out.GetTxId().ToString(),
                tx_out.GetN());
            return false;
        }
    }

    auto& prevMinerId = document["prevMinerId"];
    if(!prevMinerId.isNull() && !prevMinerId.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& prevMinerIdSig = document["prevMinerIdSig"];
    if(!prevMinerIdSig.isNull() && !prevMinerIdSig.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& minerId = document["minerId"];
    if(!minerId.isNull() && !minerId.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& dynamicMinerId = document["dynamicMinerId"];
    if(!dynamicMinerId.isStr())
    {
        LogInvalidDoc();
        return false;
    }

    auto& vctx = document["vctx"];
    if(!vctx.isNull())
    {
        if(!vctx.isObject())
        {
            LogInvalidDoc();
            return false;
        }

        auto& vctxTxid = vctx["txId"];
        if(!vctxTxid.isStr())
        {
            LogInvalidDoc();
            return false;
        }

        auto& vctxVout = vctx["vout"];
        if(!vctxVout.isNum())
        {
            LogInvalidDoc();
            return false;
        }
    }

    // Verify signature of dynamic document miner id.
    const vector<uint8_t> dynamicMinerIdBytes{
        ParseHex(dynamicMinerId.get_str())};
    const string dataToVerify{staticDocumentJson_ + signatureStaticDocument_ +
                              document.write()};

    const vector<uint8_t> dataToVerifyBytes{dataToVerify.begin(),
                                            dataToVerify.end()};
    if(!verify(dataToVerify, dynamicMinerIdBytes, signatureBytes))
    {
        LogPrint(BCLog::MINERID,
                 "Signature of dynamic miner id in coinbase document is "
                 "invalidin coinbase transaction with txid %s and output "
                 "number %d.\n",
                 tx_out.GetTxId().ToString(),
                 tx_out.GetN());
        return false;
    }

    // set data refs only if they do not exist already
    if(!coinbaseDocument_.GetDataRefs())
    {
        std::vector<CoinbaseDocument::DataRef> dataRefs;
        if(!parseDataRefs(document, dataRefs))
        {
            LogInvalidDoc();
            return false;
        }
        if(!dataRefs.empty())
        {
            coinbaseDocument_.SetDataRefs(dataRefs);
        }
    }

    return true;
}

bool parseCoinbaseDocument(MinerId& minerId,
                           const std::string_view coinbaseDocumentDataJson,
                           const std::span<const uint8_t> signatureBytes,
                           const COutPoint& tx_out,
                           int32_t blockHeight,
                           bool dynamic)
{

    UniValue coinbaseDocumentData;
    if(!coinbaseDocumentData.read(coinbaseDocumentDataJson.data(),
                                  coinbaseDocumentDataJson.size()))
    {
        LogPrint(BCLog::MINERID,
                 "Cannot parse coinbase document in coinbase transaction with "
                 "txid %s and output number %d.\n",
                 tx_out.GetTxId().ToString(),
                 tx_out.GetN());
        return false;
    }

    if(!dynamic &&
       !minerId.SetStaticCoinbaseDocument(
           coinbaseDocumentData, signatureBytes, tx_out, blockHeight))
    {
        return false;
    }

    if(dynamic &&
       !minerId.SetDynamicCoinbaseDocument(
           coinbaseDocumentData, signatureBytes, tx_out, blockHeight))
    {
        return false;
    }

    return true;
}

static std::optional<MinerId> ExtractMinerId(const CTransaction& tx, size_t i, int32_t blockHeight)
{
    MinerId minerId {};

    const std::span<const uint8_t> script { tx.vout[i].scriptPubKey };

    // MinerId coinbase documents starts at 7th byte of the output message
    bsv::instruction_iterator it{script.last(script.size() - 7)};

    if(!it)
    {
        LogPrint(
            BCLog::MINERID,
            "Failed to extract data for static document of minerId "
            "from script with txid %s and output number %d.\n",
            tx.GetId().ToString(),
            i);
        return {};
    }
    if(it->operand().empty())
    {
        LogPrint(BCLog::MINERID,
                 "Invalid data for MinerId protocol from script with "
                 "txid %s and output number %d.\n",
                 tx.GetId().ToString(),
                 i);
        return {};
    }
    const std::string_view static_cd{bsv::to_sv(it->operand())};

    if(!++it)
    {
        LogPrint(
            BCLog::MINERID,
            "Failed to extract signature of static document of minerId "
            "from script with txid %s and output number %d.\n",
            tx.GetId().ToString(),
            i);
        return {};
    }
    if(it->operand().empty())
    {
        LogPrint(BCLog::MINERID,
                 "Invalid data for MinerId signature from script with "
                 "txid %s and output number %d.\n",
                 tx.GetId().ToString(),
                 i);
        return {};
    }

    if(parseCoinbaseDocument(minerId,
                             static_cd,
                             it->operand(),
                             COutPoint(tx.GetId(), i),
                             blockHeight,
                             false))
    {
        // Static document of MinerId is successful. Check
        // dynamic MinerId.
        if(!++it)
        {
            // Dynamic miner id is empty. We found first
            // successful miner id - we can stop looking.
            return minerId;
        }

        if(!it)
        {
            LogPrint(BCLog::MINERID,
                     "Failed to extract data for dynamic document of "
                     "minerId from script with txid %s and output "
                     " number % d.\n",
                     tx.GetId().ToString(),
                     i);
            return {};
        }
        const string_view dynamic_cd{bsv::to_sv(it->operand())};

        if(!++it)
        {
            LogPrint(BCLog::MINERID,
                     "Failed to extract signature of dynamic document "
                     "of minerId from script with txid %s and output "
                     "number %d.\n",
                     tx.GetId().ToString(),
                     i);
            return {};
        }

        if(parseCoinbaseDocument(minerId,
                                 dynamic_cd,
                                 it->operand(),
                                 COutPoint(tx.GetId(), i),
                                 blockHeight,
                                 true))
        {
            return minerId;
        }
    }

    return {};
}

std::optional<MinerId> FindMinerId(const CBlock& block, int32_t blockHeight)
{
    // Start by looking in coinbase
    if(block.vtx[0])
    {
        const CTransaction& tx { *block.vtx[0] };

        // Scan coinbase transaction outputs for minerId; stop on first valid
        // minerId
        for(size_t i = 0; i < tx.vout.size(); i++)
        {
            const std::span<const uint8_t> script{tx.vout[i].scriptPubKey};
            // OP_FALSE OP_RETURN 0x04 0xAC1EED88 OP_PUSHDATA Coinbase Document
            if(IsMinerId(script))
            {
                std::optional<MinerId> minerId { ExtractMinerId(tx, i, blockHeight) };
                if(minerId)
                {
                    return minerId;
                }
            }
            else if(IsMinerInfo(script))
            {
                const auto var_mi_ref { ParseMinerInfoRef(script) };
                if(std::holds_alternative<miner_info_error>(var_mi_ref))
                {
                    log_parse_error(std::get<miner_info_error>(var_mi_ref), tx.GetId().ToString(), i);
                    break;
                }
                else if(std::holds_alternative<miner_info_ref>(var_mi_ref))
                {
                    const auto mi_ref { std::get<miner_info_ref>(var_mi_ref) };
                    const auto var_mi_doc_sig { ParseMinerInfo(block, mi_ref) };
                    if(std::holds_alternative<miner_info_error>(var_mi_doc_sig))
                    {
                        const auto error_id = get<miner_info_error>(var_mi_doc_sig);
                        if (error_id == miner_info_error::txid_not_found)
                        {
                            std::stringstream ss;
                            ss  << "coinbase references minerinfo txn "
                                << mi_ref.txid().ToString()
                                << " but this txn could not be found in this block";
                            log_parse_error(error_id, tx.GetId().ToString(), i, ss.str());
                        }
                        else
                        {
                            log_parse_error(error_id, tx.GetId().ToString(), i);
                        }
                        break;
                    }

                    return MinerId { std::get<miner_info>(var_mi_doc_sig) };
                }
            }
        }
    }

    return {};
}

