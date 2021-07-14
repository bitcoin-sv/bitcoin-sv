// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "chainparams.h"
#include "consensus/validation.h"
#include "key.h"
#include "keystore.h"
#include "miner_id.h"
#include "script/script.h"
#include "test/test_bitcoin.h"
#include "utilstrencodings.h"
#include "validation.h"

#include <bits/stdint-uintn.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(minerid_tests, BasicTestingSetup)

vector<uint8_t> protocolPrefix{0xac, 0x1e, 0xed, 0x88};

template <typename O>
void hash_sha256(const string_view msg, O o)
{
    CSHA256()
        .Write(reinterpret_cast<const uint8_t*>(msg.data()), msg.size())
        .Finalize(o);
}

bool sign(const string_view msg, const CKey& key, vector<uint8_t>& signature)
{
    array<uint8_t, CSHA256::OUTPUT_SIZE> hash;
    hash_sha256(msg, hash.data());

    return key.Sign(uint256(vector<uint8_t>{begin(hash), end(hash)}),
                    signature);
}

static string calculatePrevMinerIdSignature(const CKey& prevMinerIdKey,
                                            const string& prevMinerIdPubKey,
                                            const string& minerIdPubKey,
                                            const string& vctxid,
                                            const string& version)
{
    string dataToSign = prevMinerIdPubKey + minerIdPubKey + vctxid;
    if(version == "0.2")
    {
        const string dataToSignHex = HexStr(dataToSign);
        dataToSign = dataToSignHex;
    }

    vector<uint8_t> prevMinerIdSignature;
    const bool b = sign(dataToSign, prevMinerIdKey, prevMinerIdSignature);
    BOOST_CHECK(b);

    return HexStr(prevMinerIdSignature);
}

static UniValue createValidCoinbaseDocument(const CKey& prevMinerIdKey,
                                            const optional<int> height,
                                            const string& prevMinerIdPubKey,
                                            const string& minerIdPubKey,
                                            const string& vctxid,
                                            const UniValue& dataRefs,
                                            const string& version)
{
    UniValue document(UniValue::VOBJ);
    document.push_back(Pair("version", version));
    if(height)
    {
        document.push_back(Pair("height", height.value()));
    }
    document.push_back(Pair("prevMinerId", prevMinerIdPubKey));
    document.push_back(Pair("prevMinerIdSig",
                            calculatePrevMinerIdSignature(prevMinerIdKey,
                                                          prevMinerIdPubKey,
                                                          minerIdPubKey,
                                                          vctxid,
                                                          version)));
    document.push_back(Pair("minerId", minerIdPubKey));

    UniValue vctx(UniValue::VOBJ);
    vctx.push_back(Pair("txId", vctxid));
    vctx.push_back(Pair("vout", 7));
    document.push_back(Pair("vctx", vctx));

    if(!dataRefs.isNull())
    {
        document.push_back(Pair("dataRefs", dataRefs));
    }

    return document;
}

static UniValue createDataRefs(const string& txid1, const string& txid2)
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

static vector<uint8_t>
createSignatureStaticCoinbaseDocument(const CKey& minerIdKey,
                                      const UniValue& coinbaseDocument)
{
    string document = coinbaseDocument.write();
    vector<uint8_t> signature;
    const bool b{sign(document, minerIdKey, signature)};
    BOOST_CHECK(b);
    return signature;
}

static vector<uint8_t> createSignatureDynamicCoinbaseDocument(
    const CKey& dynamicMinerIdKey,
    const UniValue& staticDocument,
    const vector<uint8_t>& signatureStaticDocument,
    const UniValue& dynamicDocument)
{
    vector<uint8_t> signature;
    string dynamicMsgToSign =
        staticDocument.write() +
        string(signatureStaticDocument.begin(), signatureStaticDocument.end()) +
        dynamicDocument.write();

    const bool b{sign(dynamicMsgToSign, dynamicMinerIdKey, signature)};
    BOOST_CHECK(b);
    return signature;
}

static UniValue prepareTransactionOutputStatic(const UniValue& finalDocument,
                                               const vector<uint8_t>& signature,
                                               CMutableTransaction& tx,
                                               const int32_t n,
                                               const bool invalid = false)
{
    string coinbaseDocument =
        invalid ? finalDocument.write() + "}" : finalDocument.write();
    vector<uint8_t> coinbaseDocumentBytes(coinbaseDocument.begin(),
                                          coinbaseDocument.end());
    tx.vout[n].scriptPubKey = CScript()
                              << OP_FALSE << OP_RETURN << protocolPrefix
                              << coinbaseDocumentBytes << signature;
    tx.vout[n].nValue = Amount(42);

    return finalDocument;
}

static void
prepareTransactionOutputDynamic(CMutableTransaction& tx,
                                const int32_t n,
                                const UniValue& staticDocument,
                                const vector<uint8_t>& signatureStaticDocument,
                                const UniValue& dynamicDocument,
                                const vector<uint8_t>& signatureDynamicDocument)
{
    string coinbaseStaticDocument = staticDocument.write();
    vector<uint8_t> coinbaseStaticDocumentBytes(coinbaseStaticDocument.begin(),
                                                coinbaseStaticDocument.end());

    string coinbaseDynamicDocument = dynamicDocument.write();
    vector<uint8_t> coinbaseDynamicDocumentBytes(
        coinbaseDynamicDocument.begin(), coinbaseDynamicDocument.end());

    tx.vout[n].scriptPubKey =
        CScript() << OP_FALSE << OP_RETURN << protocolPrefix
                  << coinbaseStaticDocumentBytes << signatureStaticDocument
                  << coinbaseDynamicDocumentBytes << signatureDynamicDocument;
}

BOOST_AUTO_TEST_CASE(staticMinerId_v1)
{
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
    string vctxid =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff";
    string txid1 =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4fa";
    string txid2 =
        "c6e68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f95755068267";

    UniValue dataRefs = createDataRefs(txid1, txid2);
    constexpr auto block_height{624455};
    UniValue baseDocument =
        createValidCoinbaseDocument(prevMinerIdKey,
                                    block_height,
                                    HexStr(prevMinerIdPubKey),
                                    HexStr(minerIdPubKey),
                                    vctxid,
                                    dataRefs,
                                    "0.1");
    vector<uint8_t> signature =
        createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);

    optional<MinerId> minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId);

    CoinbaseDocument comparingCD =
        CoinbaseDocument("0.1",
                         block_height,
                         HexStr(prevMinerIdPubKey),
                         baseDocument["prevMinerIdSig"].get_str(),
                         HexStr(minerIdPubKey),
                         COutPoint(uint256S(vctxid), 7));
    vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0}};
    comparingCD.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    auto expected_cd{minerId.value().GetCoinbaseDocument()};
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);

    // Wrong signature (with correct size)
    vector<uint8_t> wrongSig = vector<uint8_t>(signature.size(), 'a');
    prepareTransactionOutputStatic(baseDocument, wrongSig, tx, 1);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);

    // Missing required field "height"
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey,
                                               nullopt,
                                               HexStr(prevMinerIdPubKey),
                                               HexStr(minerIdPubKey),
                                               vctxid,
                                               dataRefs,
                                               "0.1");
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);

    // Incorrect required field "height"
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey,
                                               28,
                                               HexStr(prevMinerIdPubKey),
                                               HexStr(minerIdPubKey),
                                               vctxid,
                                               dataRefs,
                                               "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);

    // Missing dataRefs (no optional fields present)
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey,
                                               block_height,
                                               HexStr(prevMinerIdPubKey),
                                               HexStr(minerIdPubKey),
                                               vctxid,
                                               NullUniValue,
                                               "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = FindMinerId(CTransaction(tx), block_height);
    comparingCD.SetDataRefs(nullopt);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    expected_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);

    // Invalid JSON
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey,
                                               block_height,
                                               HexStr(prevMinerIdPubKey),
                                               HexStr(minerIdPubKey),
                                               vctxid,
                                               dataRefs,
                                               "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1, true);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);

    // Invalid prevMinerId signature
    baseDocument.push_back(
        Pair("prevMinerIdSig",
             string(baseDocument["prevMinerIdSig"].get_str().size(), 'b')));
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);

    // Even if first MinerId is invalid, we find the second one which is valid
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey,
                                               block_height,
                                               HexStr(prevMinerIdPubKey),
                                               HexStr(minerIdPubKey),
                                               vctxid,
                                               dataRefs,
                                               "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 2);
    minerId = FindMinerId(CTransaction(tx), block_height);
    comparingCD.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    expected_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);

    // If we add one more invalid miner id, it does not matter - we already
    // found the valid one in previous output
    baseDocument = createValidCoinbaseDocument(prevMinerIdKey,
                                               block_height,
                                               HexStr(prevMinerIdPubKey),
                                               HexStr(minerIdPubKey),
                                               vctxid,
                                               dataRefs,
                                               "0.1");
    signature = createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 3, true);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    expected_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);
}

BOOST_AUTO_TEST_CASE(staticMinerId_v2)
{
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
    string vctxid =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff";
    string txid1 =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4fa";
    string txid2 =
        "c6e68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f95755068267";

    UniValue dataRefs = createDataRefs(txid1, txid2);
    constexpr auto block_height{624455};
    UniValue baseDocument =
        createValidCoinbaseDocument(prevMinerIdKey,
                                    block_height,
                                    HexStr(prevMinerIdPubKey),
                                    HexStr(minerIdPubKey),
                                    vctxid,
                                    dataRefs,
                                    "0.2");
    vector<uint8_t> signature =
        createSignatureStaticCoinbaseDocument(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);

    optional<MinerId> minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId);

    CoinbaseDocument comparingCD =
        CoinbaseDocument("0.2",
                         block_height,
                         HexStr(prevMinerIdPubKey),
                         baseDocument["prevMinerIdSig"].get_str(),
                         HexStr(minerIdPubKey),
                         COutPoint(uint256S(vctxid), 7));
    vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0}};
    comparingCD.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    const auto expected_cd{minerId.value().GetCoinbaseDocument()};
    BOOST_CHECK_EQUAL(expected_cd, comparingCD);
}

BOOST_AUTO_TEST_CASE(dynamicMinerId)
{
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
    string vctxid =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff";
    string txid1 =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4fa";
    string txid2 =
        "c6e68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f95755068267";
    string txid1D =
        "dd39008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4dd";
    string txid2D =
        "dde68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f957550682dd";

    // Prepare static document data
    UniValue dataRefs = createDataRefs(txid1, txid2);
    constexpr auto block_height{624455};
    UniValue staticDocument =
        createValidCoinbaseDocument(prevMinerIdKey,
                                    block_height,
                                    HexStr(prevMinerIdPubKey),
                                    HexStr(minerIdPubKey),
                                    vctxid,
                                    dataRefs,
                                    "0.1");
    vector<uint8_t> staticSignatureBytes =
        createSignatureStaticCoinbaseDocument(minerIdKey, staticDocument);
    // Prepare data for dynamic signature
    CKey dynamicMinerIdKey;
    dynamicMinerIdKey.MakeNewKey(true);
    CPubKey dynamicMinerIdPubKey = dynamicMinerIdKey.GetPubKey();

    UniValue dynamicDocument(UniValue::VOBJ);
    dynamicDocument.push_back(
        Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));

    vector<uint8_t> dynamicSignature =
        createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey,
                                               staticDocument,
                                               staticSignatureBytes,
                                               dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);

    // Check with valid dynamic document
    optional<MinerId> minerId = FindMinerId(CTransaction(tx), block_height);
    CoinbaseDocument comparingCD =
        CoinbaseDocument("0.1",
                         block_height,
                         HexStr(prevMinerIdPubKey),
                         staticDocument["prevMinerIdSig"].get_str(),
                         HexStr(minerIdPubKey),
                         COutPoint(uint256S(vctxid), 7));
    vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2), 0}};
    comparingCD.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Static document has no dataRefs
    staticDocument = createValidCoinbaseDocument(prevMinerIdKey,
                                                 block_height,
                                                 HexStr(prevMinerIdPubKey),
                                                 HexStr(minerIdPubKey),
                                                 vctxid,
                                                 NullUniValue,
                                                 "0.1");
    staticSignatureBytes =
        createSignatureStaticCoinbaseDocument(minerIdKey, staticDocument);
    UniValue dataRefsDynamic = createDataRefs(txid1D, txid2D);
    dynamicDocument.push_back(Pair("dataRefs", dataRefsDynamic));
    dynamicSignature =
        createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey,
                                               staticDocument,
                                               staticSignatureBytes,
                                               dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), block_height);
    comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid1D), 0},
        CoinbaseDocument::DataRef{{"id1", "id2"}, uint256S(txid2D), 0}};
    comparingCD.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Check with wrong signature (with correct size)
    vector<uint8_t> wrongSig = vector<uint8_t>(dynamicSignature.size(), 'a');
    prepareTransactionOutputDynamic(
        tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, wrongSig);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);

    // Check that dynamic document cannot rewrite required field.
    dynamicDocument.push_back(Pair("version", "0.1"));
    dynamicSignature =
        createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey,
                                               staticDocument,
                                               staticSignatureBytes,
                                               dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);

    // Pass empty dynamic document (but with correct signature) : invalid result
    dynamicDocument = UniValue(UniValue::VOBJ);
    dynamicSignature =
        createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey,
                                               staticDocument,
                                               staticSignatureBytes,
                                               dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);

    // Pass in minerId as an object: invalid result
    dynamicDocument.push_back(
        Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));
    dynamicDocument.push_back(Pair("minerId", UniValue(UniValue::VOBJ)));
    dynamicSignature =
        createSignatureDynamicCoinbaseDocument(dynamicMinerIdKey,
                                               staticDocument,
                                               staticSignatureBytes,
                                               dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    minerId = FindMinerId(CTransaction(tx), block_height);
    BOOST_CHECK(minerId == nullopt);
}

// static CScript ScriptFromHex(const char* hex)
//{
//    vector<uint8_t> data = ParseHex(hex);
//    return CScript(data.begin(), data.end());
//}

// cjg fails due to having height as string
// BOOST_AUTO_TEST_CASE(production_example_tx) {
//
//    const char *data =
//        "006a04ac1eed884dc1017b2276657273696f6e223a22302e31222c2268656967687422"
//        "3a22363234343535222c22707265764d696e65724964223a2230323236303436363564"
//        "3361313836626539363930323331613237396638653138623830306634636537386361"
//        "616332643531393430633863316339326138333534222c22707265764d696e65724964"
//        "536967223a223330343430323230363734353266396439626165656633323731383365"
//        "3266353635633863346437363239393238376436633032353361613133336337353135"
//        "3064373864333037303232303239633964393361633038633139653230613033646333"
//        "3233303763346630613032336537396135303563303262303138353763383464343936"
//        "373061636636222c226d696e65724964223a2230323236303436363564336131383662"
//        "6539363930323331613237396638653138623830306634636537386361616332643531"
//        "393430633863316339326138333534222c2276637478223a7b2274784964223a223635"
//        "3834663533653133323136643334393739303938333632626461333462643336373730"
//        "353863386234653036323162323433393563353736623662616164222c22766f757422"
//        "3a307d7d473045022100ae0bc35173357a3afc52a39c7c6237a0b2f6fdaca3f76667bd"
//        "e966d3c00655ff02206767755766be7b7252a42a00eb3aa38d62aae6acf800faa6ff3e"
//        "a1bb74f4cf05";
//    string minerIdPubKey =
//        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
//    string prevMinerIdPubKey =
//        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
//    string prevMinerIdSignature =
//        "3044022067452f9d9baeef327183e2f565c8c4d76299287d6c0253aa133c75150d78d3"
//        "07022029c9d93ac08c19e20a03dc32307c4f0a023e79a505c02b01857c84d49670acf"
//        "6";
//    string vcTxId =
//        "6584f53e13216d34979098362bda34bd3677058c8b4e0621b24395c576b6baad";
//
//    CMutableTransaction tx;
//    tx.vin.resize(1);
//    tx.vin[0].scriptSig.resize(10);
//    tx.vout.resize(2);
//    tx.vout[0].nValue = Amount(42);
//    tx.vout[1].scriptPubKey = ScriptFromHex(data);
//    tx.vout[1].nValue = Amount(42);
//
//    optional<MinerId> minerId = FindMinerId(CTransaction(tx), block_height);
//    BOOST_CHECK(minerId);
//
//    CoinbaseDocument comparingCD =
//        CoinbaseDocument("0.1", block_height, prevMinerIdPubKey,
//        prevMinerIdSignature,
//                         minerIdPubKey, COutPoint(uint256S(vcTxId), 0));
//    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
//    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), comparingCD);
//}

BOOST_AUTO_TEST_SUITE_END()
