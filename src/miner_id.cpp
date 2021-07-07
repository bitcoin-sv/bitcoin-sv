// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id.h"

#include "hash.h"
#include "logging.h"
#include "pubkey.h"

namespace
{
    // Parse dataRefs field from coinbase document.
    // If signature of current coinbase document is valid, we expect valid transaction references in datarefs field.
    // But it can happen that referenced transactions are not found due to various reasons.
    // Here, we only store transactions and not check their existence. This happens later in the process.
    bool parseDataRefs(const UniValue& coinbaseDocument, std::vector<CoinbaseDocument::DataRef>& dataRefs)
    {
        if (coinbaseDocument.exists("dataRefs"))
        {
            // If dataRefs is present, it has to have correct structure.
            if (!coinbaseDocument["dataRefs"].isObject() ||
                !coinbaseDocument["dataRefs"].exists("refs") ||
                !coinbaseDocument["dataRefs"]["refs"].isArray())
            {
                return false;
            }

            UniValue dataRefsValues = coinbaseDocument["dataRefs"]["refs"].get_array();

            for(size_t dataRefIdx = 0; dataRefIdx < dataRefsValues.size(); dataRefIdx++)
            {
                if (dataRefsValues[dataRefIdx].exists("brfcIds") && dataRefsValues[dataRefIdx]["brfcIds"].isArray() &&
                    dataRefsValues[dataRefIdx].exists("txid") &&  dataRefsValues[dataRefIdx]["txid"].isStr() &&
                    dataRefsValues[dataRefIdx].exists("vout") && dataRefsValues[dataRefIdx]["vout"].isNum())
                {
                    std::vector<std::string> brfcIds;
                    for (size_t brfcIdx = 0; brfcIdx < dataRefsValues[dataRefIdx]["brfcIds"].size(); brfcIdx++)
                    {
                        if (!dataRefsValues[dataRefIdx]["brfcIds"][brfcIdx].isStr())
                        {
                            // Incorrect structure of member in dataRefs list.
                            return false;
                        }
                        brfcIds.push_back(dataRefsValues[dataRefIdx]["brfcIds"][brfcIdx].get_str());
                    }
                    dataRefs.push_back(CoinbaseDocument::DataRef {brfcIds, uint256S(dataRefsValues[dataRefIdx]["txid"].get_str()), dataRefsValues[dataRefIdx]["vout"].get_int()});
                }
                else
                {
                    // Incorrect structure of member in dataRefs list.
                    return false;
                }
            }

            return true;

        }

        return true;
    }
}


bool MinerId::SetStaticCoinbaseDocument(const UniValue& document, std::vector<uint8_t>& signatureBytes, const COutPoint& tx_out, int32_t blockHeight)
{
    auto LogInvalidDoc = [&]{
        LogPrint(BCLog::TXNVAL,"One or more required parameters from coinbase document missing or incorrect. Coinbase transaction txid %s and output number %d. \n",
            tx_out.GetTxId().ToString(), tx_out.GetN());
    };

    // Check existence and validity of required fields of static coinbase document.
    auto& version = document["version"];
    if(!version.isStr() || !SUPPORTED_VERSIONS.count(version.get_str())) { LogInvalidDoc(); return false; }

    auto& height = document["height"];
    if(!height.isStr()) { LogInvalidDoc(); return false; }
    if (std::stoi(height.get_str()) != blockHeight)
    {
        LogPrint(BCLog::TXNVAL,"Block height in coinbase document is incorrect in coinbase transaction with txid %s and output number %d. \n", tx_out.GetTxId().ToString(), tx_out.GetN());
        return false;
    }

    auto& prevMinerId = document["prevMinerId"];
    if(!prevMinerId.isStr()) { LogInvalidDoc(); return false; }

    auto& prevMinerIdSig = document["prevMinerIdSig"];
    if(!prevMinerIdSig.isStr()) { LogInvalidDoc(); return false; }

    auto& minerId = document["minerId"];
    if(!minerId.isStr()) { LogInvalidDoc(); return false; }

    auto& vctx = document["vctx"];
    if(!vctx.isObject()) { LogInvalidDoc(); return false; }

    auto& vctxTxid = vctx["txId"];
    if(!vctxTxid.isStr()) { LogInvalidDoc(); return false; }

    auto& vctxVout = vctx["vout"];
    if(!vctxVout.isNum()) { LogInvalidDoc(); return false; }

    // Verify signature of static document miner id.
    std::vector<uint8_t> minerIdBytes = ParseHex(minerId.get_str());
    CPubKey minerPubKey(minerIdBytes.begin(), minerIdBytes.end());
    std::string coinbaseDocumentJson = document.write();

    std::vector<uint8_t> coinbaseDocumentBytes = std::vector<uint8_t>(coinbaseDocumentJson.begin(), coinbaseDocumentJson.end());

    uint8_t hashSignature[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(coinbaseDocumentBytes.data(), coinbaseDocumentBytes.size()).Finalize(hashSignature);
    if (!minerPubKey.Verify(uint256(std::vector<uint8_t> {std::begin(hashSignature), std::end(hashSignature)}), signatureBytes))
    {
        LogPrint(BCLog::TXNVAL,"Signature of static coinbase document is invalid in coinbase transaction with txid %s and output number %d. \n", tx_out.GetTxId().ToString(), tx_out.GetN());
        return false;
    }

    // Verify signature of previous miner id.
    std::vector<uint8_t> prevMinerIdBytes = ParseHex(prevMinerId.get_str());
    CPubKey prevMinerPubKey(prevMinerIdBytes.begin(), prevMinerIdBytes.end());
    std::vector<uint8_t> signaturePrevMinerId = ParseHex(prevMinerIdSig.get_str());
    std::string dataToSign =
        prevMinerId.get_str() +
        minerId.get_str() +
        vctxTxid.get_str();

    uint8_t hashPrevSignature[CSHA256::OUTPUT_SIZE];

    if (version.get_str() == "0.1")
    {
        std::vector<uint8_t> dataToSignBytes = std::vector<uint8_t>(dataToSign.begin(), dataToSign.end());
        CSHA256().Write(dataToSignBytes.data(), dataToSignBytes.size()).Finalize(hashPrevSignature);

    }
    else if (version.get_str() == "0.2")
    {
        std::string dataToSignHex = HexStr(dataToSign);
        CSHA256().Write(reinterpret_cast<const uint8_t*>(&dataToSignHex[0]), dataToSignHex.size()).Finalize(hashPrevSignature);
    }
    else
    {
        LogPrint(BCLog::TXNVAL,"Unsupported version in miner id in txid %s and output number %d. \n", tx_out.GetTxId().ToString(), tx_out.GetN());
        return false;
    }

    if (!prevMinerPubKey.Verify(uint256(std::vector<uint8_t> {std::begin(hashPrevSignature), std::end(hashPrevSignature)}), signaturePrevMinerId))
    {
        LogPrint(BCLog::TXNVAL,"Signature of previous miner id in coinbase document is invalid in coinbase transaction with txid %s and output number %d. \n", tx_out.GetTxId().ToString(), tx_out.GetN());
        return false;
    }

    CoinbaseDocument coinbaseDocument(
        version.get_str(),
        std::stoi(height.get_str()),
        prevMinerId.get_str(),
        prevMinerIdSig.get_str(),
        minerId.get_str(),
        COutPoint(uint256S(vctxTxid.get_str()), vctxVout.get_int()));

    std::vector<CoinbaseDocument::DataRef> dataRefs;
    if (!parseDataRefs(document, dataRefs)) { LogInvalidDoc(); return false; }
    if (dataRefs.size() != 0)
    {
        coinbaseDocument.SetDataRefs(dataRefs);
    }

    // Set static coinbase document.
    mCoinbaseDocument = coinbaseDocument;
    // Set fields needed for verifying dynamic miner id.
    mStaticDocumentJson = document.write();
    mSignatureStaticDocument = std::string(signatureBytes.begin(), signatureBytes.end());

    return true;
}

bool MinerId::SetDynamicCoinbaseDocument(const UniValue& document, std::vector<uint8_t>& signatureBytes, const COutPoint& tx_out, int32_t blockHeight)
{
    auto LogInvalidDoc = [&]{
        LogPrint(BCLog::TXNVAL,"Structure in coinbase document is incorrect (incorrect field type) in coinbase transaction with txid %s and output number %d. \n",
            tx_out.GetTxId().ToString(), tx_out.GetN());
    };

    // Dynamic document has no required fields (except for dynamic miner id). Check field types if they exist.
    auto& version = document["version"];
    if(!version.isNull() && (!version.isStr() || !SUPPORTED_VERSIONS.count(version.get_str()))) { LogInvalidDoc(); return false; }

    auto& height = document["height"];
    if (!height.isNull())
    {
        if(!height.isNum()) { LogInvalidDoc(); return false; }
        if (height.get_int() != blockHeight)
        {
            LogPrint(BCLog::TXNVAL,"Block height in coinbase document is incorrect in coinbase transaction with txid %s and output number %d. \n", tx_out.GetTxId().ToString(), tx_out.GetN());
            return false;
        }
    }

    auto& prevMinerId = document["prevMinerId"];
    if(!prevMinerId.isNull() && !prevMinerId.isStr()) { LogInvalidDoc(); return false; }

    auto& prevMinerIdSig = document["prevMinerIdSig"];
    if(!prevMinerIdSig.isNull() && !prevMinerIdSig.isStr()) { LogInvalidDoc(); return false; }

    auto& minerId = document["minerId"];
    if(!minerId.isNull() && !minerId.isStr()) { LogInvalidDoc(); return false; }

    auto& dynamicMinerId = document["dynamicMinerId"];
    if(!dynamicMinerId.isStr()) { LogInvalidDoc(); return false; }

    auto& vctx = document["vctx"];
    if(!vctx.isNull())
    {
        if (!vctx.isObject()) { LogInvalidDoc(); return false; }

        auto& vctxTxid = vctx["txId"];
        if(!vctxTxid.isStr()) { LogInvalidDoc(); return false; }

        auto& vctxVout = vctx["vout"];
        if(!vctxVout.isNum()) { LogInvalidDoc(); return false; }
    }

    // Verify signature of dynamic document miner id.
    std::vector<uint8_t> dynamicMinerIdBytes = ParseHex(dynamicMinerId.get_str());
    CPubKey dynamicMinerIdPubKey(dynamicMinerIdBytes.begin(), dynamicMinerIdBytes.end());
    std::string dataToSign =
        mStaticDocumentJson +
        mSignatureStaticDocument +
        document.write();

    std::vector<uint8_t> dataToSignBytes = std::vector<uint8_t>(dataToSign.begin(), dataToSign.end());
    uint8_t hashSignature[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(dataToSignBytes.data(), dataToSignBytes.size()).Finalize(hashSignature);

    if (!dynamicMinerIdPubKey.Verify(uint256(std::vector<uint8_t> {std::begin(hashSignature), std::end(hashSignature)}), signatureBytes))
    {
        LogPrint(BCLog::TXNVAL,"Signature of dynamic miner id in coinbase document is invalid in coinbase transaction with txid %s and output number %d. \n", tx_out.GetTxId().ToString(), tx_out.GetN());
        return false;
    }

    // set data refs only if they do not exist already
    if (!mCoinbaseDocument.GetDataRefs())
    {
        std::vector<CoinbaseDocument::DataRef> dataRefs;
        if (!parseDataRefs(document, dataRefs)) { LogInvalidDoc(); return false; }
        if (dataRefs.size() != 0)
        {
            mCoinbaseDocument.SetDataRefs(dataRefs);
        }
    }

    return true;
}

bool MinerId::parseCoinbaseDocument(std::string& coinbaseDocumentDataJson, std::vector<uint8_t>& signatureBytes, const COutPoint& tx_out, int32_t blockHeight, bool dynamic)
{

    UniValue coinbaseDocumentData;
    if (!coinbaseDocumentData.read(coinbaseDocumentDataJson))
    {
        LogPrint(BCLog::TXNVAL,"Cannot parse coinbase document in coinbase transaction with txid %s and output number %d.\n", tx_out.GetTxId().ToString(), tx_out.GetN());
        return false;
    }

    if (!dynamic && !SetStaticCoinbaseDocument(coinbaseDocumentData, signatureBytes, tx_out, blockHeight))
    {
        return false;
    }

    if (dynamic && !SetDynamicCoinbaseDocument(coinbaseDocumentData, signatureBytes, tx_out, blockHeight))
    {
        return false;
    }

    return true;
}

std::optional<MinerId> MinerId::FindMinerId(const CTransaction& tx, int32_t blockHeight)
{
    MinerId minerId;

    // Scan coinbase transaction outputs for minerId; stop on first valid minerId
    for (size_t i = 0; i < tx.vout.size(); i++)
    {
        // OP_FALSE OP_RETURN 0x04 0xAC1EED88 OP_PUSHDATA Coinbase Document
        if(IsMinerId(tx.vout[i].scriptPubKey))
        {
            const CScript& pubKey = tx.vout[i].scriptPubKey;

            std::vector<uint8_t> msgBytes {};
            opcodetype opcodeRet {};
            // MinerId coinbase documents starts at 7th byte of the output message
            CScript::const_iterator pc { pubKey.begin() + 7 };
            if(!pubKey.GetOp(pc, opcodeRet, msgBytes))
            {
                LogPrint(BCLog::TXNVAL,"Failed to extract data for static document of minerId from script with txid %s and output number %d.\n", tx.GetId().ToString(), i);
                continue;
            }

            if (msgBytes.empty())
            {
                LogPrint(BCLog::TXNVAL,"Invalid data for MinerId protocol from script with txid %s and output number %d.\n", tx.GetId().ToString(), i);
                continue;
            }

            std::vector<uint8_t> signature {};

            if(!pubKey.GetOp(pc, opcodeRet, signature))
            {
                LogPrint(BCLog::TXNVAL,"Failed to extract signature of static document of minerId from script with txid %s and output number %d.\n", tx.GetId().ToString(), i);
                continue;
            }

            if (signature.empty())
            {
                LogPrint(BCLog::TXNVAL,"Invalid data for MinerId signature from script with txid %s and output number %d.\n", tx.GetId().ToString(), i);
                continue; 
            }

            std::string staticCoinbaseDocumentJson = std::string(msgBytes.begin(), msgBytes.end());

            if (minerId.parseCoinbaseDocument(staticCoinbaseDocumentJson, signature, COutPoint(tx.GetId(), i), blockHeight, false))
            {
                // Static document of MinerId is successful. Check dynamic MinerId.
                if(pc >= tx.vout[i].scriptPubKey.end())
                {
                    // Dynamic miner id is empty. We found first successful miner id - we can stop looking.
                    return minerId;
                }

                if (!pubKey.GetOp(pc, opcodeRet, msgBytes))
                {
                    LogPrint(BCLog::TXNVAL,"Failed to extract data for dynamic document of minerId from script with txid %s and output number %d.\n", tx.GetId().ToString(), i);
                    continue;
                }

                if(!pubKey.GetOp(pc, opcodeRet, signature))
                {
                    LogPrint(BCLog::TXNVAL,"Failed to extract signature of dynamic document of minerId from script with txid %s and output number %d.\n", tx.GetId().ToString(), i);
                    continue;
                }

                std::string dynamicCoinbaseDocumentJson = std::string(msgBytes.begin(), msgBytes.end());
                if (minerId.parseCoinbaseDocument(dynamicCoinbaseDocumentJson, signature, COutPoint(tx.GetId(), i), blockHeight, true))
                {
                    return minerId;
                }

                // Successful static coinbase doc, but failed dynamic coinbase doc: let's reset miner id.
                minerId = MinerId();
            }
        }
    }

    return {};
}
