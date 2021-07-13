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

static CScript ScriptFromHex(const char *hex) {
    std::vector<uint8_t> data = ParseHex(hex);
    return CScript(data.begin(), data.end());
}

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

std::string calculatePrevMinerIdSignature(const CKey& prevMinerIdKey, std::string& prevMinerIdPubKey, std::string& minerIdPubKey, std::string& vctxid, std::string version)
{
    std::vector<uint8_t> prevMinerIdSignature;
    std::string dataToSign = prevMinerIdPubKey + minerIdPubKey + vctxid;

    uint8_t hashPrevSignature[CSHA256::OUTPUT_SIZE];

    if (version == "0.1")
    {
        std::vector<uint8_t> dataToSignBytes = std::vector<uint8_t>(dataToSign.begin(), dataToSign.end());
        CSHA256().Write(dataToSignBytes.data(), dataToSignBytes.size()).Finalize(hashPrevSignature);

    }
    else if (version == "0.2")
    {
        std::string dataToSignHex = HexStr(dataToSign);
        CSHA256().Write(reinterpret_cast<const uint8_t*>(&dataToSignHex[0]), dataToSignHex.size()).Finalize(hashPrevSignature);
    }

    BOOST_CHECK(prevMinerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashPrevSignature), std::end(hashPrevSignature)}), prevMinerIdSignature));
    
    return HexStr(prevMinerIdSignature);
}

UniValue createValidCoinbaseDocument(
    const CKey& prevMinerIdKey,
    std::optional<std::string> height,
    std::string prevMinerIdPubKey,
    std::string minerIdPubKey,
    std::string vctxid,
    const UniValue& dataRefs,
    std::string version)
{
    UniValue document(UniValue::VOBJ);
    document.push_back(Pair("version", version));
    if (height) { document.push_back(Pair("height", height.value())); }
    document.push_back(Pair("prevMinerId", prevMinerIdPubKey));
    document.push_back(Pair("prevMinerIdSig",
        calculatePrevMinerIdSignature(prevMinerIdKey, prevMinerIdPubKey, minerIdPubKey, vctxid, version)));
    document.push_back(Pair("minerId", minerIdPubKey));

    UniValue vctx(UniValue::VOBJ);
    vctx.push_back(Pair("txId", vctxid));
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

std::vector<uint8_t> createSignatureStaticCoinbaseDocument(const CKey& minerIdKey, UniValue& coinbaseDocument)
{
    std::string document = coinbaseDocument.write();
    std::vector<uint8_t> documentBytes = std::vector<uint8_t>(document.begin(), document.end());

    uint8_t hashSignature[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(documentBytes.data(), documentBytes.size()).Finalize(hashSignature);

    std::vector<uint8_t> signature;
    BOOST_CHECK(minerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashSignature), std::end(hashSignature)}), signature));

    return signature;
}

std::vector<uint8_t> createSignatureDynamicCoinbaseDocument(CKey dynamicMinerIdKey, UniValue& staticDocument, std::vector<uint8_t>& signatureStaticDocument, UniValue& dynamicDocument)
{
    std::vector<uint8_t> signature;
    std::string dynamicMsgToSign = staticDocument.write() + std::string(signatureStaticDocument.begin(), signatureStaticDocument.end()) + dynamicDocument.write();
    std::vector<uint8_t> dataToSignBytes = std::vector<uint8_t>(dynamicMsgToSign.begin(), dynamicMsgToSign.end());
    uint8_t hashSignature[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(dataToSignBytes.data(), dataToSignBytes.size()).Finalize(hashSignature);

    BOOST_CHECK(dynamicMinerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashSignature), std::end(hashSignature)}), signature));

    return signature;
}

UniValue prepareTransactionOutputStatic(UniValue& finalDocument, std::vector<uint8_t>& signature, CMutableTransaction& tx, int32_t n, bool invalid = false)
{
    std::string coinbaseDocument = invalid ? finalDocument.write() + "}" : finalDocument.write();
    std::vector<uint8_t> coinbaseDocumentBytes(coinbaseDocument.begin(), coinbaseDocument.end());
    tx.vout[n].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << protocolPrefix << coinbaseDocumentBytes << signature;
    tx.vout[n].nValue = Amount(42);

    return finalDocument;
}

void prepareTransactionOutputDynamic(
    CMutableTransaction& tx,
    int32_t n,
    UniValue staticDocument,
    std::vector<uint8_t>& signatureStaticDocument,
    UniValue dynamicDocument,
    std::vector<uint8_t>& signatureDynamicDocument)
{
    std::string coinbaseStaticDocument = staticDocument.write();
    std::vector<uint8_t> coinbaseStaticDocumentBytes(coinbaseStaticDocument.begin(), coinbaseStaticDocument.end());

    std::string coinbaseDynamicDocument = dynamicDocument.write();
    std::vector<uint8_t> coinbaseDynamicDocumentBytes(coinbaseDynamicDocument.begin(), coinbaseDynamicDocument.end());

    tx.vout[n].scriptPubKey =
        CScript() << OP_FALSE << OP_RETURN << protocolPrefix <<
        coinbaseStaticDocumentBytes << signatureStaticDocument <<
        coinbaseDynamicDocumentBytes << signatureDynamicDocument;
}

BOOST_AUTO_TEST_CASE(staticMinerId_v1) {
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
    UniValue baseDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.1");
    std::vector<uint8_t> signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);

    std::optional<MinerId> minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId);

    CoinbaseDocument comparingCD = CoinbaseDocument("0.1", 624455, HexStr(prevMinerIdPubKey), baseDocument["prevMinerIdSig"].get_str(), HexStr(minerIdPubKey), COutPoint(uint256S(vctxid), 7));
    std::vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0} };
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    auto expected_cd{minerId.value().GetCoinbaseDocument()};
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);

    // Wrong signature (with correct size)
    std::vector<uint8_t> wrongSig = std::vector<uint8_t>(signature.size(), 'a');
    prepareTransactionOutputStatic(baseDocument, wrongSig, tx, 1);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Missing required field "height"
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, std::nullopt, HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Incorrect required field "height"
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, "28", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Missing dataRefs (no optional fields present)
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, NullUniValue, "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = FindMinerId(CTransaction(tx), 624455);
    comparingCD.SetDataRefs(std::nullopt);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    expected_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);

    // Invalid JSON
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1, true);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Invalid prevMinerId signature
    baseDocument.push_back(Pair("prevMinerIdSig", std::string(baseDocument["prevMinerIdSig"].get_str().size(), 'b')));
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Even if first MinerId is invalid, we find the second one which is valid
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 2);
    minerId = FindMinerId(CTransaction(tx), 624455);
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    expected_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);

    // If we add one more invalid miner id, it does not matter - we already found the valid one in previous output
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 3, true);
    minerId = FindMinerId(CTransaction(tx), 624455);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    expected_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);
}

BOOST_AUTO_TEST_CASE(staticMinerId_v2) {
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
    UniValue baseDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.2");
    std::vector<uint8_t> signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);

    std::optional<MinerId> minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId);

    CoinbaseDocument comparingCD = CoinbaseDocument("0.2", 624455, HexStr(prevMinerIdPubKey), baseDocument["prevMinerIdSig"].get_str(), HexStr(minerIdPubKey), COutPoint(uint256S(vctxid), 7));
    std::vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0} };
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    const auto expected_cd{minerId.value().GetCoinbaseDocument()};
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);
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
    UniValue staticDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, dataRefs, "0.1");
    std::vector<uint8_t> staticSignatureBytes = createSignatureStaticCoinbaseDocument(minerIdKey, staticDocument);
    // Prepare data for dynamic signature
    CKey dynamicMinerIdKey;
    dynamicMinerIdKey.MakeNewKey(true);
    CPubKey dynamicMinerIdPubKey = dynamicMinerIdKey.GetPubKey();

    UniValue dynamicDocument(UniValue::VOBJ);
    dynamicDocument.push_back(Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));

    std::vector<uint8_t> dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignatureBytes, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, dynamicSignature);

    // Check with valid dynamic document
    std::optional<MinerId> minerId = FindMinerId(CTransaction(tx), 624455);
    CoinbaseDocument comparingCD = CoinbaseDocument("0.1", 624455, HexStr(prevMinerIdPubKey), staticDocument["prevMinerIdSig"].get_str(), HexStr(minerIdPubKey), COutPoint(uint256S(vctxid), 7));
    std::vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0} };
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Static document has no dataRefs
    staticDocument = createValidCoinbaseDocument(prevMinerIdKey, "624455", HexStr(prevMinerIdPubKey), HexStr(minerIdPubKey), vctxid, NullUniValue, "0.1");
    staticSignatureBytes = createSignatureStaticCoinbaseDocument(minerIdKey, staticDocument);
    UniValue dataRefsDynamic = createDataRefs(txid1D, txid2D);
    dynamicDocument.push_back(Pair("dataRefs", dataRefsDynamic));
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignatureBytes, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), 624455);
    comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1D), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2D), 0} };
    comparingCD.SetDataRefs(comparingDataRefs);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Check with wrong signature (with correct size)
    std::vector<uint8_t> wrongSig = std::vector<uint8_t>(dynamicSignature.size(), 'a');
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, wrongSig);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Check that dynamic document cannot rewrite required field.
    dynamicDocument.push_back(Pair("version", "0.1"));
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignatureBytes, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), 624455);
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Pass empty dynamic document (but with correct signature) : invalid result
    dynamicDocument = UniValue(UniValue::VOBJ);
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignatureBytes, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);

    // Pass in minerId as an object: invalid result
    dynamicDocument.push_back(Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));
    dynamicDocument.push_back(Pair("minerId", UniValue(UniValue::VOBJ)));
    dynamicSignature = createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey, staticDocument, staticSignatureBytes, dynamicDocument);
    prepareTransactionOutputDynamic(tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId == std::nullopt);
}

BOOST_AUTO_TEST_CASE(production_example_tx) {

    const char* data = "006a04ac1eed884dc1017b2276657273696f6e223a22302e31222c22686569676874223a22363234343535222c22707265764d696e65724964223a22303232363034363635643361313836626539363930323331613237396638653138623830306634636537386361616332643531393430633863316339326138333534222c22707265764d696e65724964536967223a223330343430323230363734353266396439626165656633323731383365326635363563386334643736323939323837643663303235336161313333633735313530643738643330373032323032396339643933616330386331396532306130336463333233303763346630613032336537396135303563303262303138353763383464343936373061636636222c226d696e65724964223a22303232363034363635643361313836626539363930323331613237396638653138623830306634636537386361616332643531393430633863316339326138333534222c2276637478223a7b2274784964223a2236353834663533653133323136643334393739303938333632626461333462643336373730353863386234653036323162323433393563353736623662616164222c22766f7574223a307d7d473045022100ae0bc35173357a3afc52a39c7c6237a0b2f6fdaca3f76667bde966d3c00655ff02206767755766be7b7252a42a00eb3aa38d62aae6acf800faa6ff3ea1bb74f4cf05";
    std::string minerIdPubKey = "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    std::string prevMinerIdPubKey = "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    std::string prevMinerIdSignature = "3044022067452f9d9baeef327183e2f565c8c4d76299287d6c0253aa133c75150d78d307022029c9d93ac08c19e20a03dc32307c4f0a023e79a505c02b01857c84d49670acf6";
    std::string vcTxId = "6584f53e13216d34979098362bda34bd3677058c8b4e0621b24395c576b6baad";

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(2);
    tx.vout[0].nValue = Amount(42);
    tx.vout[1].scriptPubKey = ScriptFromHex(data);
    tx.vout[1].nValue = Amount(42);

    std::optional<MinerId> minerId = FindMinerId(CTransaction(tx), 624455);
    BOOST_CHECK(minerId);

    CoinbaseDocument comparingCD = CoinbaseDocument("0.1", 624455, prevMinerIdPubKey, prevMinerIdSignature, minerIdPubKey, COutPoint(uint256S(vcTxId), 0));
    compareMinerIds(minerId.value().GetCoinbaseDocument(), comparingCD);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
}

BOOST_AUTO_TEST_SUITE_END()
