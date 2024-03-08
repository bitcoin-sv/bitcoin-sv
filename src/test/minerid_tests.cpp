// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <algorithm>
#include <cassert>
#include <cctype>
#include <iterator>

#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/test/tools/old/interface.hpp>

#include "chainparams.h"
#include "consensus/validation.h"
#include "key.h"
#include "keystore.h"
#include "miner_id/miner_id.h"
#include "script/script.h"
#include "utilstrencodings.h"
#include "validation.h"

#include "test/test_bitcoin.h"

namespace ba = boost::algorithm;

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
        string s;
        transform_hex(prevMinerIdPubKey, back_inserter(s));
        transform_hex(minerIdPubKey, back_inserter(s));
        transform_hex(vctxid, back_inserter(s));
        dataToSign = s;
    }

    vector<uint8_t> prevMinerIdSignature;
    const bool b = sign(dataToSign, prevMinerIdKey, prevMinerIdSignature);
    BOOST_CHECK(b);

    return HexStr(prevMinerIdSignature);
}

static UniValue create_coinbase_doc(const CKey& prevMinerIdKey,
                                    const optional<int> height,
                                    const string& prevMinerIdPubKey,
                                    const string& minerIdPubKey,
                                    const string& vctxid,
                                    const UniValue& dataRefs,
                                    const UniValue& minerContact,
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

    if(!minerContact.isNull())
    {
        document.push_back(Pair("minerContact", minerContact));
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

static vector<uint8_t> sign(const CKey& minerIdKey, const UniValue& msg)
{
    vector<uint8_t> signature;
    const bool b{sign(msg.write(), minerIdKey, signature)};
    assert(b);
    return signature;
}

static vector<uint8_t> sign(const CKey& key,
                            const UniValue& staticDocument,
                            const vector<uint8_t>& signatureStaticDocument,
                            const UniValue& dynamicDocument)
{
    vector<uint8_t> signature;
    const string msg{
        staticDocument.write() +
        string{signatureStaticDocument.begin(), signatureStaticDocument.end()} +
        dynamicDocument.write()};

    const bool b{sign(msg, key, signature)};
    assert(b);
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

    CBlock block{};
    block.vtx.resize(1);
    CMutableTransaction tx;
    tx.vout.resize(4);

    // Prepare test data.
    CKey minerIdKey;
    minerIdKey.MakeNewKey(true);
    const CPubKey minerIdPubKey = minerIdKey.GetPubKey();

    CKey prevMinerIdKey;
    prevMinerIdKey.MakeNewKey(true);
    const CPubKey prevMinerIdPubKey = prevMinerIdKey.GetPubKey();
    const string vctxid =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff";
    const string txid1 =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4fa";
    const string txid2 =
        "c6e68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f95755068267";

    UniValue dataRefs = createDataRefs(txid1, txid2);
    constexpr auto block_height{624455};
    UniValue coinbase_doc = create_coinbase_doc(prevMinerIdKey,
                                                block_height,
                                                HexStr(prevMinerIdPubKey),
                                                HexStr(minerIdPubKey),
                                                vctxid,
                                                dataRefs,
                                                NullUniValue,
                                                "0.1");
    vector<uint8_t> signature = sign(minerIdKey, coinbase_doc);
    prepareTransactionOutputStatic(coinbase_doc, signature, tx, 1);

    block.vtx[0] = MakeTransactionRef(tx);
    optional<MinerId> minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId);

    CoinbaseDocument expected_cd{"",
                                 "0.1",
                                 block_height,
                                 HexStr(prevMinerIdPubKey),
                                 coinbase_doc["prevMinerIdSig"].get_str(),
                                 HexStr(minerIdPubKey),
                                 COutPoint(uint256S(vctxid), 7)};
    const vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid1)}, 0, ""},
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid2)}, 0, ""}};
    expected_cd.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    auto actual_cd{minerId.value().GetCoinbaseDocument()};
    BOOST_CHECK_EQUAL(actual_cd, expected_cd);

    // Wrong signature (with correct size)
    const vector<uint8_t> wrongSig(signature.size(), 'a');
    prepareTransactionOutputStatic(coinbase_doc, wrongSig, tx, 1);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);

    // Missing required field "height"
    coinbase_doc = create_coinbase_doc(prevMinerIdKey,
                                       nullopt,
                                       HexStr(prevMinerIdPubKey),
                                       HexStr(minerIdPubKey),
                                       vctxid,
                                       dataRefs,
                                       NullUniValue,
                                       "0.1");
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);

    // Incorrect required field "height"
    coinbase_doc = create_coinbase_doc(prevMinerIdKey,
                                       28,
                                       HexStr(prevMinerIdPubKey),
                                       HexStr(minerIdPubKey),
                                       vctxid,
                                       dataRefs,
                                       NullUniValue,
                                       "0.1");
    signature = sign(minerIdKey, coinbase_doc);
    prepareTransactionOutputStatic(coinbase_doc, signature, tx, 1);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);

    // Missing dataRefs (no optional fields present)
    coinbase_doc = create_coinbase_doc(prevMinerIdKey,
                                       block_height,
                                       HexStr(prevMinerIdPubKey),
                                       HexStr(minerIdPubKey),
                                       vctxid,
                                       NullUniValue,
                                       NullUniValue,
                                       "0.1");
    signature = sign(minerIdKey, coinbase_doc);
    prepareTransactionOutputStatic(coinbase_doc, signature, tx, 1);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    expected_cd.SetDataRefs(nullopt);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    actual_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(actual_cd, expected_cd);

    // Invalid JSON
    coinbase_doc = create_coinbase_doc(prevMinerIdKey,
                                       block_height,
                                       HexStr(prevMinerIdPubKey),
                                       HexStr(minerIdPubKey),
                                       vctxid,
                                       dataRefs,
                                       NullUniValue,
                                       "0.1");
    signature = sign(minerIdKey, coinbase_doc);
    prepareTransactionOutputStatic(coinbase_doc, signature, tx, 1, true);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);

    // Invalid prevMinerId signature
    coinbase_doc.push_back(
        Pair("prevMinerIdSig",
             string(coinbase_doc["prevMinerIdSig"].get_str().size(), 'b')));
    signature = sign(minerIdKey, coinbase_doc);
    prepareTransactionOutputStatic(coinbase_doc, signature, tx, 1);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);

    // Even if first MinerId is invalid, we find the second one which is valid
    coinbase_doc = create_coinbase_doc(prevMinerIdKey,
                                       block_height,
                                       HexStr(prevMinerIdPubKey),
                                       HexStr(minerIdPubKey),
                                       vctxid,
                                       dataRefs,
                                       NullUniValue,
                                       "0.1");
    signature = sign(minerIdKey, coinbase_doc);
    prepareTransactionOutputStatic(coinbase_doc, signature, tx, 2);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    expected_cd.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    actual_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(actual_cd, expected_cd);

    // If we add one more invalid miner id, it does not matter - we already
    // found the valid one in previous output
    coinbase_doc = create_coinbase_doc(prevMinerIdKey,
                                       block_height,
                                       HexStr(prevMinerIdPubKey),
                                       HexStr(minerIdPubKey),
                                       vctxid,
                                       dataRefs,
                                       NullUniValue,
                                       "0.1");
    signature = sign(minerIdKey, coinbase_doc);
    prepareTransactionOutputStatic(coinbase_doc, signature, tx, 3, true);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    actual_cd = minerId.value().GetCoinbaseDocument();
    BOOST_CHECK_EQUAL(actual_cd, expected_cd);
}

BOOST_AUTO_TEST_CASE(staticMinerId_v2)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlock block {};
    block.vtx.resize(1);

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(4);
    tx.vout[0].nValue = Amount(42);

    // Prepare test data.
    CKey minerIdKey;
    minerIdKey.MakeNewKey(true);
    const CPubKey minerIdPubKey = minerIdKey.GetPubKey();

    CKey prevMinerIdKey;
    prevMinerIdKey.MakeNewKey(true);
    const CPubKey prevMinerIdPubKey = prevMinerIdKey.GetPubKey();
    const string vctxid =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff";
    const string txid1 =
        "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4fa";
    const string txid2 =
        "c6e68a930db53b804b6cbc51d4582856079ce075cc305975f7d8f95755068267";

    UniValue minerContact { UniValue::VOBJ };
    minerContact.push_back(Pair("name", "SomeName"));

    const UniValue dataRefs = createDataRefs(txid1, txid2);
    constexpr auto block_height{624455};
    UniValue baseDocument = create_coinbase_doc(prevMinerIdKey,
                                                block_height,
                                                HexStr(prevMinerIdPubKey),
                                                HexStr(minerIdPubKey),
                                                vctxid,
                                                dataRefs,
                                                minerContact,
                                                "0.2");
    vector<uint8_t> signature = sign(minerIdKey, baseDocument);
    prepareTransactionOutputStatic(baseDocument, signature, tx, 1);

    block.vtx[0] = MakeTransactionRef(tx);
    const optional<MinerId> minerId =
        FindMinerId(block, block_height);
    BOOST_CHECK(minerId);

    CoinbaseDocument expected_cd{"",
                                 "0.2",
                                 block_height,
                                 HexStr(prevMinerIdPubKey),
                                 baseDocument["prevMinerIdSig"].get_str(),
                                 HexStr(minerIdPubKey),
                                 COutPoint(uint256S(vctxid), 7),
                                 minerContact};
    vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid1)}, 0, ""},
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid2)}, 0, ""}};
    expected_cd.SetDataRefs(comparingDataRefs);
    const auto acutal_cd{minerId.value().GetCoinbaseDocument()};
    BOOST_CHECK_EQUAL(acutal_cd, expected_cd);
}

BOOST_AUTO_TEST_CASE(dynamicMinerId)
{
    SelectParams(CBaseChainParams::MAIN);

    CBlock block{};
    block.vtx.resize(1);

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
    UniValue staticDocument = create_coinbase_doc(prevMinerIdKey,
                                                  block_height,
                                                  HexStr(prevMinerIdPubKey),
                                                  HexStr(minerIdPubKey),
                                                  vctxid,
                                                  dataRefs,
                                                  NullUniValue,
                                                  "0.1");
    vector<uint8_t> staticSignatureBytes = sign(minerIdKey, staticDocument);
    // Prepare data for dynamic signature
    CKey dynamicMinerIdKey;
    dynamicMinerIdKey.MakeNewKey(true);
    CPubKey dynamicMinerIdPubKey = dynamicMinerIdKey.GetPubKey();

    UniValue dynamicDocument(UniValue::VOBJ);
    dynamicDocument.push_back(
        Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));

    vector<uint8_t> dynamicSignature = sign(dynamicMinerIdKey,
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
    block.vtx[0] = MakeTransactionRef(tx);
    optional<MinerId> minerId = FindMinerId(block, block_height);
    CoinbaseDocument expected_cd{"",
                                 "0.1",
                                 block_height,
                                 HexStr(prevMinerIdPubKey),
                                 staticDocument["prevMinerIdSig"].get_str(),
                                 HexStr(minerIdPubKey),
                                 COutPoint(uint256S(vctxid), 7)};
    vector<CoinbaseDocument::DataRef> comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid1)}, 0, ""},
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid2)}, 0, ""}};
    expected_cd.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);

    // Static document has no dataRefs
    staticDocument = create_coinbase_doc(prevMinerIdKey,
                                         block_height,
                                         HexStr(prevMinerIdPubKey),
                                         HexStr(minerIdPubKey),
                                         vctxid,
                                         NullUniValue,
                                         NullUniValue,
                                         "0.1");
    staticSignatureBytes = sign(minerIdKey, staticDocument);
    UniValue dataRefsDynamic = createDataRefs(txid1D, txid2D);
    dynamicDocument.push_back(Pair("dataRefs", dataRefsDynamic));
    dynamicSignature = sign(dynamicMinerIdKey,
                            staticDocument,
                            staticSignatureBytes,
                            dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    comparingDataRefs = {
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid1D)}, 0, ""},
        CoinbaseDocument::DataRef{{"id1", "id2"}, TxId{uint256S(txid2D)}, 0, ""}};
    expected_cd.SetDataRefs(comparingDataRefs);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);

    // Check with wrong signature (with correct size)
    vector<uint8_t> wrongSig = vector<uint8_t>(dynamicSignature.size(), 'a');
    prepareTransactionOutputDynamic(
        tx, 1, staticDocument, staticSignatureBytes, dynamicDocument, wrongSig);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);

    // Check that dynamic document cannot rewrite required field.
    dynamicDocument.push_back(Pair("version", "0.1"));
    dynamicSignature = sign(dynamicMinerIdKey,
                            staticDocument,
                            staticSignatureBytes,
                            dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);

    // Pass empty dynamic document (but with correct signature) : invalid result
    dynamicDocument = UniValue(UniValue::VOBJ);
    dynamicSignature = sign(dynamicMinerIdKey,
                            staticDocument,
                            staticSignatureBytes,
                            dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);

    // Pass in minerId as an object: invalid result
    dynamicDocument.push_back(
        Pair("dynamicMinerId", HexStr(dynamicMinerIdPubKey)));
    dynamicDocument.push_back(Pair("minerId", UniValue(UniValue::VOBJ)));
    dynamicSignature = sign(dynamicMinerIdKey,
                            staticDocument,
                            staticSignatureBytes,
                            dynamicDocument);
    prepareTransactionOutputDynamic(tx,
                                    1,
                                    staticDocument,
                                    staticSignatureBytes,
                                    dynamicDocument,
                                    dynamicSignature);
    block.vtx[0] = MakeTransactionRef(tx);
    minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId == nullopt);
}

BOOST_AUTO_TEST_CASE(v1_mainet_block_624455)
{
    CBlock block {};
    block.vtx.resize(1);

    CMutableTransaction tx;
    tx.vout.resize(1);

    const char* script{
        "006a04ac1eed884dc1017b2276657273696f6e223a22302e31222c2268656967687422"
        "3a22363234343535222c22707265764d696e65724964223a2230323236303436363564"
        "3361313836626539363930323331613237396638653138623830306634636537386361"
        "616332643531393430633863316339326138333534222c22707265764d696e65724964"
        "536967223a223330343430323230363734353266396439626165656633323731383365"
        "3266353635633863346437363239393238376436633032353361613133336337353135"
        "3064373864333037303232303239633964393361633038633139653230613033646333"
        "3233303763346630613032336537396135303563303262303138353763383464343936"
        "373061636636222c226d696e65724964223a2230323236303436363564336131383662"
        "6539363930323331613237396638653138623830306634636537386361616332643531"
        "393430633863316339326138333534222c2276637478223a7b2274784964223a223635"
        "3834663533653133323136643334393739303938333632626461333462643336373730"
        "353863386234653036323162323433393563353736623662616164222c22766f757422"
        "3a307d7d473045022100ae0bc35173357a3afc52a39c7c6237a0b2f6fdaca3f76667bd"
        "e966d3c00655ff02206767755766be7b7252a42a00eb3aa38d62aae6acf800faa6ff3e"
        "a1bb74f4cf05"};
    vector<uint8_t> v;
    ba::unhex(script, back_inserter(v));
    tx.vout[0].scriptPubKey = CScript{v.begin(), v.end()};

    constexpr auto block_height{624455};
    block.vtx[0] = MakeTransactionRef(tx);
    optional<MinerId> minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId);

    const string minerIdPubKey =
        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    const string prevMinerIdPubKey =
        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    const string prevMinerIdSignature =
        "3044022067452f9d9baeef327183e2f565c8c4d76299287d6c0253aa133c75150d78d3"
        "07022029c9d93ac08c19e20a03dc32307c4f0a023e79a505c02b01857c84d49670acf"
        "6";
    const string vcTxId =
        "6584f53e13216d34979098362bda34bd3677058c8b4e0621b24395c576b6baad";

    const CoinbaseDocument expected_cd{"",
                                       "0.1",
                                       block_height,
                                       prevMinerIdPubKey,
                                       prevMinerIdSignature,
                                       minerIdPubKey,
                                       COutPoint(uint256S(vcTxId), 0)};
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
}

BOOST_AUTO_TEST_CASE(v1_mainet_block_697014)
{
    CBlock block {};
    block.vtx.resize(1);

    CMutableTransaction tx;
    tx.vout.resize(1);

    const char* script{
        "006a04ac1eed884d53027b2276657273696f6e223a22302e31222c2268656967687422"
        "3a3639373031342c22707265764d696e65724964223a22303365393264336535633366"
        "3762643934356466626634386537613939333933623162666233663131663338306165"
        "33306432383665376666326165633561323730222c22707265764d696e657249645369"
        "67223a2233303435303232313030643736333630653464323133333163613836663031"
        "3863303436653537633933386631393737353037343733333335333630626533373034"
        "3863616531616633303232303062653636303435343032316266393436346539396635"
        "6139353831613938633963663439353430373539386335396234373334623266646234"
        "383262663937222c226d696e65724964223a2230336539326433653563336637626439"
        "3435646662663438653761393933393362316266623366313166333830616533306432"
        "383665376666326165633561323730222c2276637478223a7b2274784964223a223537"
        "3962343335393235613930656533396133376265336230306239303631653734633330"
        "633832343133663664306132303938653162656137613235313566222c22766f757422"
        "3a307d2c226d696e6572436f6e74616374223a7b22656d61696c223a22696e666f4074"
        "61616c2e636f6d222c226e616d65223a225441414c2044697374726962757465642049"
        "6e666f726d6174696f6e20546563686e6f6c6f67696573222c226d65726368616e7441"
        "5049456e64506f696e74223a2268747470733a2f2f6d65726368616e746170692e7461"
        "616c2e636f6d2f227d7d463044022025dc3aa7ab1aefb4b09f714a5311425f351f024a"
        "0c55e8f6b0258041323b076102204727637a2ba714060fe1fbfabd1d2f98cd5456eb52"
        "dee5cd92ea7224e3781ebe"};

    vector<uint8_t> v;
    ba::unhex(script, back_inserter(v));
    tx.vout[0].scriptPubKey = CScript{v.begin(), v.end()};

    constexpr auto block_height{697014};
    block.vtx[0] = MakeTransactionRef(tx);
    optional<MinerId> minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId);

    //    const string minerIdPubKey =
    //        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    //    const string prevMinerIdPubKey =
    //        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    //    const string prevMinerIdSignature =
    //        "3044022067452f9d9baeef327183e2f565c8c4d76299287d6c0253aa133c75150d78d3"
    //        "07022029c9d93ac08c19e20a03dc32307c4f0a023e79a505c02b01857c84d49670acf"
    //        "6";
    //    const string vcTxId =
    //        "6584f53e13216d34979098362bda34bd3677058c8b4e0621b24395c576b6baad";
    //
    //    const CoinbaseDocument expected_cd{"0.1",
    //                                       block_height,
    //                                       prevMinerIdPubKey,
    //                                       prevMinerIdSignature,
    //                                       minerIdPubKey,
    //                                       COutPoint(uint256S(vcTxId), 0)};
    //    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    //    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
}

BOOST_AUTO_TEST_CASE(v2_stn_block_12170)
{
    CBlock block {};
    block.vtx.resize(1);

    CMutableTransaction tx;
    tx.vout.resize(1);

    const char* script{
        "006a04ac1eed884d79037b2276657273696f6e223a22302e32222c2268656967687422"
        "3a31323137302c22707265764d696e65724964223a2230336236666132333761396131"
        "3937363333643633643465383236366162393433656130383238393565333537613030"
        "356266303230303662646332303131653338222c22707265764d696e65724964536967"
        "223a223330343530323231303038613266353337656161383666363563653562353166"
        "3330663235663363343039636633356533316233636665633764366639663761646161"
        "6363326639316530323230313138383632656264373761316366373238333032623865"
        "3765313134343332623335653031616266343933613833653135333939306630383138"
        "6166643565222c226d696e65724964223a223033623666613233376139613139373633"
        "3364363364346538323636616239343365613038323839356533353761303035626630"
        "3230303662646332303131653338222c2276637478223a7b2274784964223a22393461"
        "3934326662653131613166303034386366633833303166623430656333323364656365"
        "6365386430343434656166326166666363313836326330346137222c22766f7574223a"
        "307d2c22657874656e73696f6e73223a7b22626c6f636b62696e64223a7b2270726576"
        "426c6f636b48617368223a223030303030303030316262313837376130366235313038"
        "3066326431346239306465393231376361313336323138663836396539333337636266"
        "623165336130222c226d6f6469666965644d65726b6c65526f6f74223a226163656638"
        "6166323833353033383461646133393235623331336239633831633962323463363366"
        "336164663534303961643235346236313139313039313836227d2c22626c6f636b696e"
        "666f223a7b227478436f756e74223a3834303030312c2273697a65576974686f757443"
        "6f696e62617365223a3136353334353533337d2c226d696e6572706172616d73223a7b"
        "22706f6c696379223a7b22626c6f636b6d617873697a65223a31303030303030303030"
        "302c226d6178737461636b6d656d6f72797573616765706f6c696379223a3130303030"
        "303030307d2c22636f6e73656e737573223a7b22657863657373697665626c6f636b73"
        "697a65223a3430303030303030302c226d6178737461636b6d656d6f72797573616765"
        "636f6e73656e737573223a3130303030303030307d7d7d7d46304402202b17d13807ae"
        "488c984eae3dcca4560642b27533373f34f5089b5b5481bfdb1c02201e7590d6d02716"
        "d52ff4b08383a84b324ff387e88f35239fb520d9762bce3c3a"};

    vector<uint8_t> v;
    ba::unhex(script, back_inserter(v));
    tx.vout[0].scriptPubKey = CScript{v.begin(), v.end()};

    constexpr auto block_height{12170};
    block.vtx[0] = MakeTransactionRef(tx);
    optional<MinerId> minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId);

    //    const string minerIdPubKey =
    //        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    //    const string prevMinerIdPubKey =
    //        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    //    const string prevMinerIdSignature =
    //        "3044022067452f9d9baeef327183e2f565c8c4d76299287d6c0253aa133c75150d78d3"
    //        "07022029c9d93ac08c19e20a03dc32307c4f0a023e79a505c02b01857c84d49670acf"
    //        "6";
    //    const string vcTxId =
    //        "6584f53e13216d34979098362bda34bd3677058c8b4e0621b24395c576b6baad";
    //
    //    const CoinbaseDocument expected_cd{"0.1",
    //                                       block_height,
    //                                       prevMinerIdPubKey,
    //                                       prevMinerIdSignature,
    //                                       minerIdPubKey,
    //                                       COutPoint(uint256S(vcTxId), 0)};
    //    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    //    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
}

BOOST_AUTO_TEST_CASE(ri)
{
    CBlock block {};
    block.vtx.resize(1);

    CMutableTransaction tx;
    tx.vout.resize(1);

    const char* script{
        "006a04ac1eed884dbb017b2276657273696f6e223a22302e32222c226865696768742"
        "23a34322c22707265764d696e65724964223a22303363336563613932613666303836"
        "363164393932643764336164343934323161616164626136303963623463396131353"
        "038643832646639383663663030303731222c22707265764d696e6572496453696722"
        "3a2233303434303232303665313265396433396531636637646165393037393032643"
        "862333162636466316538623265386239646538323461666136336535623865326565"
        "633862633630323230373637313638633338626161383335313038633462393735393"
        "266653064343963313861333231306165643335326135393130623537326264323834"
        "35333839222c226d696e65724964223a2230336333656361393261366630383636316"
        "439393264376433616434393432316161616462613630396362346339613135303864"
        "3832646639383663663030303731222c2276637478223a7b2274784964223a2233393"
        "134303739383536616131656462663035366236623439303434646266343631643331"
        "616133636132653033376636373266373938336435666537373835222c22766f75742"
        "23a307d7d463044022032f3d6caf49b2e19670ca77e0f89b22c114f1ba26b12ea6380"
        "ab6d74c929900002207181259593f9f34ed7500cb50bf81939c988269f211c5efdbbd"
        "8271bf8ae13f4"};

    vector<uint8_t> v;
    ba::unhex(script, back_inserter(v));
    tx.vout[0].scriptPubKey = CScript{v.begin(), v.end()};

    constexpr auto block_height{42};
    block.vtx[0] = MakeTransactionRef(tx);
    optional<MinerId> minerId = FindMinerId(block, block_height);
    BOOST_CHECK(minerId);

    //    const string minerIdPubKey =
    //        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    //    const string prevMinerIdPubKey =
    //        "022604665d3a186be9690231a279f8e18b800f4ce78caac2d51940c8c1c92a8354";
    //    const string prevMinerIdSignature =
    //        "3044022067452f9d9baeef327183e2f565c8c4d76299287d6c0253aa133c75150d78d3"
    //        "07022029c9d93ac08c19e20a03dc32307c4f0a023e79a505c02b01857c84d49670acf"
    //        "6";
    //    const string vcTxId =
    //        "6584f53e13216d34979098362bda34bd3677058c8b4e0621b24395c576b6baad";
    //
    //    const CoinbaseDocument expected_cd{"0.1",
    //                                       block_height,
    //                                       prevMinerIdPubKey,
    //                                       prevMinerIdSignature,
    //                                       minerIdPubKey,
    //                                       COutPoint(uint256S(vcTxId), 0)};
    //    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
    //    BOOST_CHECK_EQUAL(minerId.value().GetCoinbaseDocument(), expected_cd);
}

BOOST_AUTO_TEST_SUITE_END()
