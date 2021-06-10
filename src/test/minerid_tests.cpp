// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "chainparams.h"
#include "consensus/validation.h"
#include "validation.h"
#include "utilstrencodings.h"
#include "key.h"
#include "keystore.h"
#include "miner_id.h"
#include "script/script.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(minerid_tests, BasicTestingSetup)

std::vector<uint8_t> protocolPrefix { 0xac, 0x1e, 0xed, 0x88 };

void compareMinerIds(const CoinbaseDocument& first, const CoinbaseDocument& second)
{
    BOOST_CHECK(first.GetVersion() == second.GetVersion());
    BOOST_CHECK(first.GetHeight() == second.GetHeight());
    BOOST_CHECK(first.GetPrevMinerId() == second.GetPrevMinerId());
    BOOST_CHECK(first.GetPrevMinerIdSig() == second.GetPrevMinerIdSig());
    BOOST_CHECK(first.GetMinerId() == second.GetMinerId());
    if (first.GetDataRefs() && second.GetDataRefs())
    {
        BOOST_CHECK(first.GetDataRefs().value().size() == second.GetDataRefs().value().size());

        for (uint32_t i = 0; i < first.GetDataRefs().value().size(); i++)
        {
            BOOST_CHECK(first.GetDataRefs().value()[i].txid.ToString() == second.GetDataRefs().value()[i].txid.ToString());
            BOOST_CHECK(first.GetDataRefs().value()[i].vout == second.GetDataRefs().value()[i].vout);
        }    
    }
}

std::string calculatePrevMinerIdSignature(const CKey& prevMinerIdKey, std::string& prevMinerIdPubKey, std::string& minerIdPubKey, std::string& vctxid)
{
    std::vector<uint8_t> prevMinerIdSignature;
    std::string msgToSign = prevMinerIdPubKey + minerIdPubKey + vctxid;
    BOOST_CHECK(prevMinerIdKey.Sign(Hash(msgToSign.begin(), msgToSign.end()), prevMinerIdSignature));
    
    return HexStr(prevMinerIdSignature);
}

UniValue createValidCoinbaseDocument(
    const CKey& prevMinerIdKey,
    std::optional<int32_t> height, 
    std::string prevMinerIdPubKey,
    std::string minerIdPubKey,
    std::string vctxid,
    const UniValue& dataRefs)
{
    UniValue document(UniValue::VOBJ);
    document.push_back(Pair("version", "0.2"));
    if (height) { document.push_back(Pair("height", height.value())); }
    document.push_back(Pair("prevMinerId", prevMinerIdPubKey));
    document.push_back(Pair("prevMinerIdSig",
        calculatePrevMinerIdSignature(prevMinerIdKey, prevMinerIdPubKey, minerIdPubKey, vctxid)));
    document.push_back(Pair("minerId", minerIdPubKey));

    UniValue vctx(UniValue::VOBJ);
    vctx.push_back(Pair("txid", vctxid));
    vctx.push_back(Pair("vout", 7));
    document.push_back(Pair("vctx", vctx));

    if (!dataRefs.isNull()) { document.push_back(Pair("dataRefs", dataRefs)); }

    return document;
}

UniValue createDataRefs(std::string txid1, std::string txid2)
{
    UniValue dataRefs(UniValue::VOBJ);
    UniValue refs(UniValue::VARR);
    UniValue ref1(UniValue::VOBJ);
    UniValue brfcIds(UniValue::VARR);
    brfcIds.push_back("id1");
    brfcIds.push_back("id2");
    ref1.push_back(Pair("brfcIds", brfcIds));
    ref1.push_back(Pair("txid", txid1));
    ref1.push_back(Pair("vout", 0));
    ref1.push_back(Pair("compress", "compressValue"));
    UniValue ref2(UniValue::VOBJ);
    ref2.push_back(Pair("brfcIds", brfcIds));
    ref2.push_back(Pair("txid", txid2));
    ref2.push_back(Pair("vout", 0));
    refs.push_back(ref1);
    refs.push_back(ref2);
    dataRefs.push_back(Pair("refs", refs));

    return dataRefs;
}

std::string createSignatureStaticCoinbaseDocument(const CKey& minerIdKey, UniValue& coinbaseDocument)
{
    std::string document = coinbaseDocument.write();
    uint256 hashMsg = Hash(document.begin(), document.end());
    std::vector<uint8_t> signature;
    BOOST_CHECK(minerIdKey.Sign(hashMsg, signature));

    return HexStr(signature);
}

std::string createSignatureDynamicCoinbaseDocument(CKey dynamicMinerIdKey, UniValue& staticDocument, std::string& signatureStaticDocument, UniValue& dynamicDocument)
{
    std::vector<uint8_t> signature;
    std::string dynamicMsgToSign = staticDocument.write() + signatureStaticDocument + dynamicDocument.write();
    BOOST_CHECK(dynamicMinerIdKey.Sign(Hash(dynamicMsgToSign.begin(), dynamicMsgToSign.end()), signature));

    return HexStr(signature);
}

UniValue prepareTransactionOutputStatic(UniValue& baseDocument, std::string& signature, CMutableTransaction& tx, int32_t n, bool invalid = false)
{
    UniValue finalDocument(UniValue::VOBJ);
    finalDocument.push_back(Pair("document", baseDocument));
    finalDocument.push_back(Pair("signature", signature));
    std::string coinbaseDocument = invalid ? finalDocument.write() + "}" : finalDocument.write();
    std::vector<uint8_t> coinbaseDocumentBytes(coinbaseDocument.begin(), coinbaseDocument.end());
    tx.vout[n].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << protocolPrefix << coinbaseDocumentBytes;
    tx.vout[n].nValue = Amount(42);

    return finalDocument;
}

void prepareTransactionOutputDynamic(
    CMutableTransaction& tx,
    int32_t n,
    UniValue staticDocument,
    std::string& signatureStaticDocument,
    UniValue dynamicDocument,
    std::string& signatureDynamicDocument)
{
    UniValue finalStaticDocument(UniValue::VOBJ);
    finalStaticDocument.push_back(Pair("document", staticDocument));
    finalStaticDocument.push_back(Pair("signature", signatureStaticDocument));
    std::string coinbaseStaticDocument = finalStaticDocument.write();
    std::vector<uint8_t> coinbaseStaticDocumentBytes(coinbaseStaticDocument.begin(), coinbaseStaticDocument.end());

    UniValue finalDynamicDocument(UniValue::VOBJ);
    finalDynamicDocument.push_back(Pair("document", dynamicDocument));
    finalDynamicDocument.push_back(Pair("signature", signatureDynamicDocument));
    std::string coinbaseDynamicDocument = finalDynamicDocument.write();
    std::vector<uint8_t> coinbaseDynamicDocumentBytes(coinbaseDynamicDocument.begin(), coinbaseDynamicDocument.end());

    tx.vout[n].scriptPubKey =
        CScript() << OP_FALSE << OP_RETURN << protocolPrefix << coinbaseStaticDocumentBytes << coinbaseDynamicDocumentBytes;
}

BOOST_AUTO_TEST_CASE(minerId) {
    SelectParams(CBaseChainParams::MAIN);

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(4);
    tx.vout[0].nValue = Amount(42);

    // Prepare test data.
    CKey minerIdKey;
    minerIdKey.MakeNewKey(true);
    CPubKey minerIdPubKey = minerIdKey.GetPubKey();

    CKey prevMinerIdKey;
    prevMinerIdKey.MakeNewKey(true);
    CPubKey prevMinerIdPubKey = prevMinerIdKey.GetPubKey();
    std::string vctxid = "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff";
    std::string txid1 = "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4fa";
    std::string txid2 = "c6e68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f95755068267";

    UniValue dataRefs = createDataRefs(txid1, txid2);
    UniValue baseDocument = createValidCoinbaseDocument(prevMinerIdKey, 624455, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs);
    std::string signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);

    std::optional<MinerId> minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId);

    CoinbaseDocument comparingCD = CoinbaseDocument("0.2", 624455, HexStr(prevMinerIdPubKey), baseDocument["prevMinerIdSig"].get_str(), HexStr(minerIdPubKey), COutPoint(uint256S(vctxid), 7));
    std::vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0} };
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Wrong signature (with correct size)
    std::string wrongSig = std::string(signature.size(), 'a');
    prepareTransactionOutputStatic(baseDocument, wrongSig, tx, 1);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Missing required field "height"
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, std::nullopt, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs);
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Incorrect required field "height"
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, 28, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs);
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Missing dataRefs (no optional fields present)
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, 624455, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, NullUniValue);
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    comparingCD.SetDataRefs(std::nullopt);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Invalid JSON
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, 624455, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs);
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1, true);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Invalid prevMinerId signature
    baseDocument.push_back(Pair("prevMinerIdSig", std::string(baseDocument["prevMinerIdSig"].get_str().size(), 'b')));
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Even if first MinerId is invalid, we find the second one which is valid
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, 624455, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs);
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 2);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);

    // If we add one more invalid miner id, it does not matter - we already found the valid one in previous output
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, 624455, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs);
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 3, true);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
}

BOOST_AUTO_TEST_CASE(dynamicMinerId) {
    SelectParams(CBaseChainParams::MAIN);

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(2);
    tx.vout[0].nValue = Amount(42);

    // Prepare test data.
    CKey minerIdKey;
    minerIdKey.MakeNewKey(true);
    CPubKey minerIdPubKey = minerIdKey.GetPubKey();

    CKey prevMinerIdKey;
    prevMinerIdKey.MakeNewKey(true);
    CPubKey prevMinerIdPubKey = prevMinerIdKey.GetPubKey();
    std::string vctxid = "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff";
    std::string txid1 = "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4fa";
    std::string txid2 = "c6e68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f95755068267";
    std::string txid1D = "dd39008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4dd";
    std::string txid2D = "dde68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f957550682dd";

    // Prepare static document data
    UniValue dataRefs = createDataRefs(txid1, txid2);
    UniValue staticDocument = createValidCoinbaseDocument(prevMinerIdKey, 624455, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs);
    std::string staticSignature = createSignatureStaticCoinbaseDocument(minerIdKey, staticDocument);

    // Prepare data for dynamic signature
    CKey dynamicMinerIdKey;
    dynamicMinerIdKey.MakeNewKey(true);
    CPubKey dynamicMinerIdPubKey = dynamicMinerIdKey.GetPubKey();

    UniValue dynamicDocument(UniValue::VOBJ);
    dynamicDocument.push_back(Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));

    std::string dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignature, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignature, dynamicDocument, dynamicSignature);

    // Check with valid dynamic document
    std::optional<MinerId> minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    CoinbaseDocument comparingCD = CoinbaseDocument("0.2", 624455, HexStr(prevMinerIdPubKey), staticDocument["prevMinerIdSig"].get_str(), HexStr(minerIdPubKey), COutPoint(uint256S(vctxid), 7));
    std::vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0} };
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Static document has no dataRefs
    staticDocument = createValidCoinbaseDocument(prevMinerIdKey, 624455, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, NullUniValue);
    staticSignature = createSignatureStaticCoinbaseDocument(minerIdKey, staticDocument);
    UniValue dataRefsDynamic = createDataRefs(txid1D, txid2D);
    dynamicDocument.push_back(Pair("dataRefs", dataRefsDynamic));
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignature, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignature, dynamicDocument, dynamicSignature);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1D), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2D), 0} };
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Check with wrong signature (with correct size)
    std::string wrongSig = std::string(dynamicSignature.size(), 'a');
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignature, dynamicDocument, wrongSig);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Check that dynamic document cannot rewrite required field.
    dynamicDocument.push_back(Pair("version", "0.1"));
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignature, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignature, dynamicDocument, dynamicSignature);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Pass empty dynamic document (but with correct signature) : invalid result
    dynamicDocument = UniValue(UniValue::VOBJ);
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignature, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignature, dynamicDocument, dynamicSignature);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Pass in minerId as an object: invalid result
    dynamicDocument.push_back(Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));
    dynamicDocument.push_back(Pair("minerId", UniValue(UniValue::VOBJ)));
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignature, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignature, dynamicDocument, dynamicSignature);
    minerId = MinerId::FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);
}

BOOST_AUTO_TEST_CASE(CheckMinerIdBytes) {

    std::string sampleDocument = "{"
        "\"minerId\":\"12345\""
      "}";
    std::vector<uint8_t> sampleProtocol { 0xac, 0x1e, 0xed };
    CScript script = CScript() << OP_FALSE << OP_RETURN << sampleProtocol << std::vector<uint8_t>(sampleDocument.begin(), sampleDocument.end());
    // Wrong protocol bytes
    BOOST_CHECK(!IsProtocolPrefixOP_RETURN(MinerId::protocol_id, script));

    // Too short script
    script = CScript() << OP_FALSE << OP_RETURN << sampleProtocol;
    BOOST_CHECK(!IsProtocolPrefixOP_RETURN(MinerId::protocol_id, script));

    // Missing OP_RETURN
    sampleProtocol = { 0xac, 0x1e, 0xed, 0x88 };
    script = CScript() << OP_FALSE << sampleProtocol << std::vector<uint8_t>(sampleDocument.begin(), sampleDocument.end());
    BOOST_CHECK(!IsProtocolPrefixOP_RETURN(MinerId::protocol_id, script));

    // Missing data after protocol id
    script = CScript() << OP_FALSE << OP_RETURN << sampleProtocol;
    BOOST_CHECK(!IsProtocolPrefixOP_RETURN(MinerId::protocol_id, script));

    // No PUSHDATA after protocol id (NOP instead of PUSHDATA)
    script = CScript() << OP_FALSE << OP_RETURN << sampleProtocol << OP_NOP;
    BOOST_CHECK(!IsProtocolPrefixOP_RETURN(MinerId::protocol_id, script));

    // OP_0 after protocol id is valid (null data)
    script = CScript() << OP_FALSE << OP_RETURN << sampleProtocol << OP_0;
    BOOST_CHECK(IsProtocolPrefixOP_RETURN(MinerId::protocol_id, script));

    // OK
    script = CScript() << OP_FALSE << OP_RETURN << sampleProtocol << std::vector<uint8_t>(sampleDocument.begin(), sampleDocument.end());
    BOOST_CHECK(IsProtocolPrefixOP_RETURN(MinerId::protocol_id, script));
}

BOOST_AUTO_TEST_SUITE_END()
