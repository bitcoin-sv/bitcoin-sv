// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_index_store.h"
#include "blockstreams.h"
#include "config.h"
#include "consensus/merkle.h"
#include "merkletreestore.h"
#include "miner_id/dataref_index.h"
#include "miner_id/miner_id.h"
#include "miner_id/miner_id_db.h"
#include "pow.h"
#include "rpc/mining.h"
#include "script/instruction_iterator.h"
#include "txn_validator.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace
{
    // Initial number of blocks to create
    constexpr size_t INITIAL_NUM_BLOCKS { 100 + 1 + 10 };
    // Miner ID protocol prefix
    const std::vector<uint8_t> ProtocolPrefix { 0xac, 0x1e, 0xed, 0x88 };

    // Create a miner ID in a coinbase transaction
    void CreateMinerIDInTxn(
        const UniValue& baseDocument,
        const std::vector<uint8_t>& signature,
        CMutableTransaction& tx,
        bool invalid = false)
    {

        std::string coinbaseDocument { baseDocument.write() };
        std::vector<uint8_t> coinbaseDocumentBytes(coinbaseDocument.begin(), coinbaseDocument.end());
        tx.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << ProtocolPrefix << coinbaseDocumentBytes << signature;
        if(invalid)
        {
            // If we want this block to be invalid, screw up the fees
            tx.vout[0].nValue = Amount{1000000000000};
        }
        else
        {
            tx.vout[0].nValue = Amount{42};
        }
    }

    // Signature calculation for previous miner ID (version 0.2 specific)
    std::string CalculatePrevMinerIdSignature(
        const CKey& prevMinerIdKey,
        const std::string& prevMinerIdPubKey,
        const std::string& minerIdPubKey,
        const std::string& vctxid)
    {
        std::string dataToSign {};
        transform_hex(prevMinerIdPubKey, back_inserter(dataToSign));
        transform_hex(minerIdPubKey, back_inserter(dataToSign));
        transform_hex(vctxid, back_inserter(dataToSign));

        uint8_t hashPrevSignature[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(reinterpret_cast<const uint8_t*>(&dataToSign[0]), dataToSign.size()).Finalize(hashPrevSignature);
        std::vector<uint8_t> prevMinerIdSignature {};
        BOOST_CHECK(prevMinerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashPrevSignature), std::end(hashPrevSignature)}), prevMinerIdSignature));
        return HexStr(prevMinerIdSignature);
    }

    // Signature calculation for static coinbase document
    std::vector<uint8_t> CreateSignatureStaticCoinbaseDocument(const CKey& minerIdKey, const UniValue& coinbaseDocument)
    {
        std::string document { coinbaseDocument.write() };
        std::vector<uint8_t> documentBytes { document.begin(), document.end() };
        uint8_t hashSignature[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(documentBytes.data(), documentBytes.size()).Finalize(hashSignature);
        std::vector<uint8_t> signature {};
        BOOST_CHECK(minerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashSignature), std::end(hashSignature)}), signature));
        return signature;
    }

    // Create a static coinbase document with miner ID details
    UniValue CreateValidCoinbaseDocument(
        const CKey& prevMinerIdKey,
        int32_t height,
        const std::string& prevMinerIdPubKey,
        const std::string& minerIdPubKey,
        const std::string& vctxid,
        const std::string& minerName,
        const std::optional<std::vector<CoinbaseDocument::DataRef>>& dataRefs)
    {
        UniValue document { UniValue::VOBJ };
        document.push_back(Pair("version", "0.2"));
        document.push_back(Pair("height", height));
        document.push_back(Pair("prevMinerId", prevMinerIdPubKey));
        document.push_back(Pair("prevMinerIdSig",
            CalculatePrevMinerIdSignature(prevMinerIdKey, prevMinerIdPubKey, minerIdPubKey, vctxid)));
        document.push_back(Pair("minerId", minerIdPubKey));

        UniValue vctx { UniValue::VOBJ };
        vctx.push_back(Pair("txId", vctxid));
        vctx.push_back(Pair("vout", 0));
        document.push_back(Pair("vctx", vctx));

        UniValue minerContact { UniValue::VOBJ };
        minerContact.push_back(Pair("name", minerName));
        document.push_back(Pair("minerContact", minerContact));

        if(dataRefs)
        {
            UniValue dataRefsJson { UniValue::VOBJ };
            UniValue dataRefsArray { UniValue::VARR };
            for(const auto& ref : *dataRefs)
            {
                UniValue dataRefJson { UniValue::VOBJ };
                UniValue brfcIdsJson { UniValue::VARR };
                for(const auto& brfcid : ref.brfcIds)
                {
                    brfcIdsJson.push_back(brfcid);
                }
                dataRefJson.push_back(Pair("brfcIds", brfcIdsJson));
                dataRefJson.push_back(Pair("txid", ref.txid.ToString()));
                dataRefJson.push_back(Pair("vout", ref.vout));
                dataRefsArray.push_back(dataRefJson);
            }
            dataRefsJson.push_back(Pair("refs", dataRefsArray));
            document.push_back(Pair("dataRefs", dataRefsJson));
        }

        return document;
    }

    // Testing fixture that creates a REGTEST-mode block chain with minerIDs
    struct SetupMinerIDChain : public TestChain100Setup
    {
        SetupMinerIDChain() : TestChain100Setup{}
        {
            // Create dataref index
            int64_t nMerkleTreeIndexDBCache = 10; // MB
            g_dataRefIndex = std::make_unique<DataRefTxnDB>(GlobalConfig::GetConfig());
            pMerkleTreeFactory = std::make_unique<CMerkleTreeFactory>(GetDataDir() / "merkle", static_cast<size_t>(nMerkleTreeIndexDBCache), 4);


            // Setup keys
            miner1IdKey1.MakeNewKey(true);
            miner1IdPubKey1 = miner1IdKey1.GetPubKey();
            miner1IdKey2.MakeNewKey(true);
            miner1IdPubKey2 = miner1IdKey2.GetPubKey();
            miner2IdKey1.MakeNewKey(true);
            miner2IdPubKey1 = miner2IdKey1.GetPubKey();
            miner3IdKey1.MakeNewKey(true);
            miner3IdPubKey1 = miner3IdKey1.GetPubKey();

            // Mine another block so we have 2 coinbase to spend
            CreateAndProcessBlock();


            // Generate a block chain with 2 miners
            int32_t startingHeight { chainActive.Height() };
            for(int32_t height = 1; height <= 10; height++)
            {
                int32_t blockHeight { startingHeight + height };

                if(height == 2 || height == 4 || height == 6)
                {
                    // Include miner ID for Miner 1
                    if(height == 6)
                    {
                        // Miner 1 rotate from key 1 to key 2
                        UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey1, blockHeight, HexStr(miner1IdPubKey1), HexStr(miner1IdPubKey2), vctxid, "Miner1", {}) };
                        std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument) };
                        CreateAndProcessBlock({}, baseDocument, signature);
                    }
                    else
                    {
                        // Miner 1 use key 1
                        UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey1, blockHeight, HexStr(miner1IdPubKey1), HexStr(miner1IdPubKey1), vctxid, "Miner1", {}) };
                        std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey1, baseDocument) };
                        CreateAndProcessBlock({}, baseDocument, signature);
                    }
                }
                else if(height == 8)
                {
                    // Create dataref txns in this block
                    CreateDataRefTxns();

                    // Use datarefs in this miners coinbase doc
                    std::vector<CoinbaseDocument::DataRef> datarefs {
                        { {dataRefTxnBrfcIds[0]}, dataRefTxns[0]->GetId(), 0, ""},
                        { {dataRefTxnBrfcIds[1]}, dataRefTxns[1]->GetId(), 0, ""}
                    };

                    // Miner 2 uses dataref
                    UniValue baseDocument { CreateValidCoinbaseDocument(miner2IdKey1, blockHeight, HexStr(miner2IdPubKey1), HexStr(miner2IdPubKey1), vctxid, "Miner2", {datarefs}) };
                    std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner2IdKey1, baseDocument) };
                    CreateAndProcessBlock({}, baseDocument, signature);
                }
                else
                {
                    // Generic, non-miner ID block
                    CreateAndProcessBlock();
                }
            }

            // Generate a competing fork for a 3rd miner
            UniValue baseDocument { CreateValidCoinbaseDocument(miner3IdKey1, chainActive.Height(), HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), vctxid, "Miner3", {}) };
            std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner3IdKey1, baseDocument) };
            CBlock forkBlock { CreateAndProcessBlock(chainActive.Tip()->GetPrev()->GetBlockHash(), baseDocument, signature) };
            forkBlockId = forkBlock.GetHash();
        }

        ~SetupMinerIDChain()
        {
            g_dataRefIndex.reset();
            pMerkleTreeFactory.reset();
        }

        // Add a couple of datarefs to the mempool so they get mined in the next block
        void CreateDataRefTxns()
        {
            // Create dataRef JSON
            std::vector<std::string> dataRefJson {};
            for(const auto& id : dataRefTxnBrfcIds)
            {
                UniValue document { UniValue::VOBJ };
                UniValue data { UniValue::VOBJ };
                UniValue brfcJson { UniValue::VOBJ };
                brfcJson.push_back(Pair("example", "value"));
                data.push_back(Pair(id, brfcJson));
                document.push_back(Pair("data", data));
                dataRefJson.push_back(document.write());
            }

            // Build and submit txn to mempool
            auto SubmitTxn = [this](const CTransaction& fundTxn, const std::string& dataRefJson)
            {
                CMutableTransaction txn {};
                txn.vin.resize(1);
                txn.vin[0].prevout = COutPoint { fundTxn.GetId(), 0 };
                txn.vout.resize(1);
                txn.vout[0].nValue = Amount{1000};
                txn.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << ProtocolPrefix << std::vector<uint8_t> { dataRefJson.begin(), dataRefJson.end() };

                // Sign
                std::vector<uint8_t> vchSig {};
                CScript scriptPubKey { CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG };
                uint256 hash = SignatureHash(scriptPubKey, CTransaction{txn}, 0, SigHashType().withForkId(), fundTxn.vout[0].nValue);
                BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
                vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
                txn.vin[0].scriptSig << vchSig;

                CTransactionRef txnRef { MakeTransactionRef(std::move(txn)) };

                // Submit to mempool so it gets included in next block
                auto pTxInputData {
                    std::make_shared<CTxInputData>(
                        connman->GetTxIdTracker(),
                        txnRef,
                        TxSource::rpc,
                        TxValidationPriority::normal,
                        TxStorage::memory,
                        GetTime())
                };
                mining::CJournalChangeSetPtr changeSet {nullptr};
                const auto& status { connman->getTxnValidator()->processValidation(pTxInputData, changeSet) };
                BOOST_CHECK(status.IsValid());
                return txnRef;
            };

            // Add 2 datarefs to the mempool
            for(int i = 1; i <= 2; ++i)
            {
                // Use coinbase from block as funding transaction
                auto blockReader { chainActive[i]->GetDiskBlockStreamReader() };
                CTransactionRef txn { SubmitTxn(blockReader->ReadTransaction(), dataRefJson[i-1]) };
                dataRefTxns.push_back(txn);
            }
        }

        // Create a new block and add it to the blockchain
        CBlock CreateAndProcessBlock(
            const std::optional<uint256> prevBlockHash = {},
            const std::optional<UniValue>& baseDocument = {},
            const std::optional<std::vector<uint8_t>>& signature = {},
            bool invalid = false)
        {
            const Config& config { GlobalConfig::GetConfig() };
            CBlockIndex* pindexPrev {nullptr};
            CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
            const auto& pblocktemplate { mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev) };
            CBlockRef blockRef { pblocktemplate->GetBlockRef() };
            CBlock& block { *blockRef };

            // Update previous block if required
            if(prevBlockHash)
            {
                block.hashPrevBlock = *prevBlockHash;
            }

            // IncrementExtraNonce creates a valid coinbase
            unsigned int extraNonce {0};
            IncrementExtraNonce(&block, pindexPrev, extraNonce);

            if(baseDocument)
            {
                // Update coinbase to include miner ID
                CMutableTransaction txCoinbase { *(block.vtx[0]) };
                CreateMinerIDInTxn(*baseDocument, *signature, txCoinbase, invalid);
                block.vtx[0] = MakeTransactionRef(std::move(txCoinbase));
                block.hashMerkleRoot = BlockMerkleRoot(block);
            }

            // Solve block
            while(!CheckProofOfWork(block.GetHash(), block.nBits, config))
            {
                ++block.nNonce;
            }

            const auto shared_pblock { std::make_shared<const CBlock>(block) };
            ProcessNewBlock(GlobalConfig::GetConfig(), shared_pblock, true, nullptr, CBlockSource::MakeLocal("test"));

            return block;
        }

        // Miner IDs
        CKey miner1IdKey1 {};
        CPubKey miner1IdPubKey1 {};
        CKey miner1IdKey2 {};
        CPubKey miner1IdPubKey2 {};
        CKey miner2IdKey1 {};
        CPubKey miner2IdPubKey1 {};
        CKey miner3IdKey1 {};
        CPubKey miner3IdPubKey1 {};

        // Hash of block at which the fork starts
        uint256 forkBlockId {};

        // Dummy vctx
        std::string vctxid { "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff" };

        // Transactions containing dataRefs
        std::vector<CTransactionRef> dataRefTxns {};
        std::vector<std::string> dataRefTxnBrfcIds { "BrfcId1", "BrfcId2" };
    };

    // For ID only
    class miner_id_tests_id;

    // RAII class to instantiate global miner ID database
    class MakeGlobalMinerIdDb
    {
      public:
        MakeGlobalMinerIdDb()
        {
            g_minerIDs = std::make_unique<MinerIdDatabase>(GlobalConfig::GetConfig());
        }
        ~MakeGlobalMinerIdDb()
        {
            g_minerIDs.reset();
        }

        MakeGlobalMinerIdDb(const MakeGlobalMinerIdDb&) = delete;
        MakeGlobalMinerIdDb(MakeGlobalMinerIdDb&&) = delete;
        MakeGlobalMinerIdDb& operator=(const MakeGlobalMinerIdDb&) = delete;
        MakeGlobalMinerIdDb& operator=(MakeGlobalMinerIdDb&&) = delete;
    };
}

// MinerIdDatabase class inspection
template<>
struct MinerIdDatabase::UnitTestAccess<miner_id_tests_id>
{
    static const MinerIdDatabase::Status& GetStatus(const MinerIdDatabase& db)
    {
        return db.mStatus;
    }

    static size_t GetNumMinerIds(const MinerIdDatabase& db)
    {
        return db.GetAllMinerIdsNL().size();
    }

    static const MinerIdDatabase::MinerIdEntry GetMinerIdEntry(
        const MinerIdDatabase& db,
        const uint256& key)
    {
        const auto& entry { db.GetMinerIdFromDatabaseNL(key) };
        if(entry)
        {
            return entry.value();
        }

        throw std::runtime_error("Miner ID not found for key " + key.ToString());
    }

    static MinerId GetLatestMinerIdByName(
        const MinerIdDatabase& db,
        BlockIndexStore& mapBlockIndex,
        const std::string& name)
    {
        // Fetch from latest block from named miner
        const auto& entry { GetMinerUUIdEntryByName(db, mapBlockIndex, name) };
        CBlockIndex* blockindex { mapBlockIndex.Get(entry.second.mLastBlock) };
        BOOST_REQUIRE(blockindex);
        CBlock block {};
        BOOST_REQUIRE(blockindex->ReadBlockFromDisk(block, GlobalConfig::GetConfig()));
        std::optional<MinerId> minerId { FindMinerId(block, blockindex->GetHeight()) };
        BOOST_REQUIRE(minerId);
        return *minerId;
    }

    static size_t GetNumMinerUUIds(const MinerIdDatabase& db)
    {
        return db.GetAllMinerUUIdsNL().size();
    }

    static const MinerIdDatabase::MinerUUIdMap::value_type GetMinerUUIdEntryByName(
        const MinerIdDatabase& db,
        BlockIndexStore& mapBlockIndex,
        const std::string& name)
    {
        for(const auto& entry : db.GetAllMinerUUIdsNL())
        {
            // Lookup last block we saw from this miner and extract the miner ID JSON
            CBlockIndex* blockindex { mapBlockIndex.Get(entry.second.mLastBlock) };
            BOOST_REQUIRE(blockindex);
            auto blockReader { blockindex->GetDiskBlockStreamReader() };
            const CTransaction& coinbase { blockReader->ReadTransaction() };

            const CScript& pubKey { coinbase.vout[0].scriptPubKey };
            const auto it { std::next(pubKey.begin_instructions(), 3) };
            BOOST_REQUIRE(it);

            // Parse the JSON and look for the minerContact information
            UniValue document {};
            const std::string_view staticCd { bsv::to_sv(it->operand()) };
            BOOST_REQUIRE(document.read(staticCd.data(), staticCd.size()));
            const auto& contact { document["minerContact"] };
            BOOST_REQUIRE(contact.isObject());
            const auto& minerName { contact["name"] };
            BOOST_REQUIRE(minerName.isStr());
            if(minerName.get_str() == name)
            {
                // Found it
                return entry;
            }
        }

        throw std::runtime_error("Miner not found with name " + name);
    }

    static const std::vector<MinerIdDatabase::MinerIdEntry> GetMinerIdsForMinerByName(
        const MinerIdDatabase& db,
        BlockIndexStore& mapBlockIndex,
        const std::string& name)
    {
        // Get UUID for named miner
        const MinerIdDatabase::MinerUUId& miner { GetMinerUUIdEntryByName(db, mapBlockIndex, name).first };

        // Get all miner IDs for this miner
        return db.GetMinerIdsForMinerNL(miner);
    }

    static size_t GetNumRecentBlocksForMinerByName(
        const MinerIdDatabase& db,
        BlockIndexStore& mapBlockIndex,
        const std::string& name)
    {
        // Get UUID for named miner
        const MinerIdDatabase::MinerUUId& miner { GetMinerUUIdEntryByName(db, mapBlockIndex, name).first };

        // Get number of recent blocks from this miner
        return db.GetNumRecentBlocksForMinerNL(miner);
    }

    static std::vector<MinerIdDatabase::RecentBlock> GetRecentBlocksOrderedByHeight(const MinerIdDatabase& db)
    {
        const auto& index { db.mLastBlocksTable.get<MinerIdDatabase::TagBlockHeight>() };
        std::vector<MinerIdDatabase::RecentBlock> blocks {};
        for(const auto& block : index)
        {
            blocks.push_back(block);
        }

        return blocks;
    }

    static void WaitForSync(const MinerIdDatabase& db)
    {
        db.mFuture.wait();
    }

    static bool MinerIdIsCurrent(const MinerIdEntry& id) { return id.mState == MinerIdEntry::State::CURRENT; }
    static bool MinerIdIsRotated(const MinerIdEntry& id) { return id.mState == MinerIdEntry::State::ROTATED; }
};
using UnitTestAccess = MinerIdDatabase::UnitTestAccess<miner_id_tests_id>;


BOOST_AUTO_TEST_SUITE(miner_id_db)

// Test initial create of miner ID database from an existing blockchain, and saving/restoring from disk
BOOST_FIXTURE_TEST_CASE(InitialiseFromExistingChain, SetupMinerIDChain)
{
    // Set M/N in config
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationM(3, nullptr);
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationN(10, nullptr);

    // Check we've got the expected number of blocks
    CBlockIndex* tip { chainActive.Tip() };
    BOOST_CHECK_EQUAL(tip->GetHeight(), static_cast<int32_t>(INITIAL_NUM_BLOCKS));

    // Check miner ID db contains the expected miner details
    auto dbCheckLambda = [this](const MinerIdDatabase& minerid_db)
    {
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 3U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 2U);

        // Check miner UUId entry for Miner1
        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        CBlockIndex* expectedFirstBlock { chainActive[103] };  // Miner1 first block was height 103
        CBlockIndex* expectedFirstBlock2ndId { chainActive[107] };  // Miner1 2nd key first block was height 107
        CBlockIndex* expectedLastBlock { chainActive[107] };  // Miner1 last block was height 107
        BOOST_CHECK_EQUAL(expectedFirstBlock->GetBlockHash(), miner1Details.second.mFirstBlock);
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner1Details.second.mLastBlock);
        BOOST_CHECK(! miner1Details.second.mReputation.mVoid);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1IdPubKey2.GetHash());

        // Check miner ID entries for Miner1
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK_EQUAL(expectedFirstBlock->GetBlockHash(), miner1Key1Details.mCreationBlock);
        BOOST_CHECK_EQUAL(miner1Key1Details.mPrevMinerId.GetHash(), miner1IdPubKey1.GetHash());
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), miner1IdPubKey2.GetHash());
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key2Details));
        BOOST_CHECK_EQUAL(expectedFirstBlock2ndId->GetBlockHash(), miner1Key2Details.mCreationBlock);
        BOOST_CHECK_EQUAL(miner1Key2Details.mPrevMinerId.GetHash(), miner1IdPubKey1.GetHash());
        BOOST_CHECK(! miner1Key2Details.mNextMinerId);

        // Miner1 doesn't use datarefs
        BOOST_CHECK(! miner1Key1Details.mCoinbaseDoc.GetDataRefs());
        BOOST_CHECK(! miner1Key2Details.mCoinbaseDoc.GetDataRefs());

        // Check recent block details for Miner1
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
        BOOST_CHECK(MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner1")));

        // Check miner UUId entry for Miner2
        const auto& miner2Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner2") };
        expectedFirstBlock = chainActive[109];  // Miner2 first block was height 109
        expectedLastBlock = chainActive[109];  // Miner2 last block was height 109
        BOOST_CHECK_EQUAL(expectedFirstBlock->GetBlockHash(), miner2Details.second.mFirstBlock);
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner2Details.second.mLastBlock);
        BOOST_CHECK(! miner2Details.second.mReputation.mVoid);
        BOOST_CHECK_EQUAL(miner2Details.second.mLatestMinerId, miner2IdPubKey1.GetHash());

        // Check miner ID entries for Miner2
        const auto& miner2Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner2IdPubKey1.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner2Key1Details));
        BOOST_CHECK_EQUAL(expectedFirstBlock->GetBlockHash(), miner2Key1Details.mCreationBlock);
        BOOST_CHECK_EQUAL(miner2Key1Details.mPrevMinerId.GetHash(), miner2IdPubKey1.GetHash());
        BOOST_CHECK(! miner2Key1Details.mNextMinerId);

        // Check datarefs for Miner2
        BOOST_REQUIRE(miner2Key1Details.mCoinbaseDoc.GetDataRefs());
        const auto& datarefs { miner2Key1Details.mCoinbaseDoc.GetDataRefs() };
        BOOST_REQUIRE_EQUAL(datarefs->size(), 2U);
        BOOST_CHECK_EQUAL(datarefs.value()[0].txid, dataRefTxns[0]->GetId());
        BOOST_CHECK_EQUAL(datarefs.value()[0].brfcIds.size(), 1U);
        BOOST_CHECK_EQUAL(datarefs.value()[0].brfcIds[0], dataRefTxnBrfcIds[0]);
        BOOST_CHECK_EQUAL(datarefs.value()[1].txid, dataRefTxns[1]->GetId());
        BOOST_CHECK_EQUAL(datarefs.value()[1].brfcIds.size(), 1U);
        BOOST_CHECK_EQUAL(datarefs.value()[1].brfcIds[0], dataRefTxnBrfcIds[1]);

        // Check recent block details for Miner2
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), 1U);
        BOOST_CHECK(! MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner2")));
    };

    {
        // Create a miner ID database which should build itself for the fist time from the blockchain
        MinerIdDatabase minerid_db { GlobalConfig::GetConfig() };
        UnitTestAccess::WaitForSync(minerid_db);
        dbCheckLambda(minerid_db);

        // Check the db build progressed as expected
        BOOST_CHECK(UnitTestAccess::GetStatus(minerid_db).mRebuiltFromBlockchain);
    }

    {
        // Create a miner ID database which should restore itself from the new database file
        MinerIdDatabase minerid_db { GlobalConfig::GetConfig() };
        UnitTestAccess::WaitForSync(minerid_db);
        dbCheckLambda(minerid_db);

        // Check the db build progressed as expected
        BOOST_CHECK(! UnitTestAccess::GetStatus(minerid_db).mRebuiltFromBlockchain);
    }
}

// Test updates to the miner ID database after updates to the chain
BOOST_FIXTURE_TEST_CASE(UpdatesToBlockchain, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    uint256 miner1LastBlockId {};

    {
        // Extend the current chain
        UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), vctxid, "Miner1", {}) };
        auto signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument) };
        CreateAndProcessBlock({}, baseDocument, signature);

        // Check the updates to the miner ID database
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 3U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 2U);

        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        CBlockIndex* expectedLastBlock { chainActive.Tip() };
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner1Details.second.mLastBlock);
        miner1LastBlockId = miner1Details.second.mLastBlock;
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1IdPubKey2.GetHash());
    }

    {
        // Extend the fork to force a reorg
        UniValue baseDocument { CreateValidCoinbaseDocument(miner3IdKey1, chainActive.Height(), HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), vctxid, "Miner3", {}) };
        auto signature { CreateSignatureStaticCoinbaseDocument(miner3IdKey1, baseDocument) };
        CBlock forkBlock { CreateAndProcessBlock(forkBlockId, baseDocument, signature) };
        baseDocument = CreateValidCoinbaseDocument(miner3IdKey1, chainActive.Height() + 1, HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), vctxid, "Miner3", {});
        signature = CreateSignatureStaticCoinbaseDocument(miner3IdKey1, baseDocument);
        forkBlock = CreateAndProcessBlock(forkBlock.GetHash(), baseDocument, signature);

        // Check the updates to the miner ID database
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

        const auto& miner3Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner3") };
        CBlockIndex* expectedLastBlock { chainActive.Tip() };
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner3Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner3"), 3U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
        BOOST_CHECK_EQUAL(miner3Details.second.mLatestMinerId, miner3IdPubKey1.GetHash());
    }

    {
        // Reorg back to the original chain
        UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey2, chainActive.Height(), HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), vctxid, "Miner1", {}) };
        auto signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument) };
        CBlock forkBlock { CreateAndProcessBlock(miner1LastBlockId, baseDocument, signature) };
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U); // Won't see new blocks from Miner1 until reorg happens
        baseDocument = CreateValidCoinbaseDocument(miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), vctxid, "Miner1", {});
        signature = CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument);
        forkBlock = CreateAndProcessBlock(forkBlock.GetHash(), baseDocument, signature);

        // Check the updates to the miner ID database
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        CBlockIndex* expectedLastBlock { chainActive.Tip() };
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner1Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 6U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner3"), 0U);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1IdPubKey2.GetHash());
    }
}

// Test miner ID key rotation
BOOST_FIXTURE_TEST_CASE(KeyRotation, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 2U);

    // Check miner IDs for Miner2
    auto checkIds = [&minerid_db](unsigned numRotations, const CPubKey& currentPubKey, const CPubKey* prevPubKey)
    {
        const auto& minerIds { UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2") };

        // There should be the initial id + however many new rotated keys we have made, upto the maximum kept
        uint64_t expectedNumIds { 1 + numRotations };
        // +1 because we'll always also keep the current ID
        expectedNumIds = std::min(expectedNumIds, GlobalConfig::GetConfig().GetMinerIdsNumToKeep() + 1);
        BOOST_CHECK_EQUAL(minerIds.size(), expectedNumIds);

        for(size_t i = 0; i < minerIds.size(); ++i)
        {
            // All except the first listed key should be rotated out
            if(i == 0)
            {
                BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(minerIds[i]));
            }
            else
            {
                BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(minerIds[i]));
            }
        }

        // Check miner details track the latest miner ID
        const auto& miner2Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner2") };
        BOOST_CHECK_EQUAL(miner2Details.second.mLatestMinerId, currentPubKey.GetHash());

        // Check next miner ID field is set and updated correctly
        const auto& currMinerIdDetails { UnitTestAccess::GetMinerIdEntry(minerid_db, currentPubKey.GetHash()) };
        BOOST_CHECK(! currMinerIdDetails.mNextMinerId);
        if(prevPubKey)
        {
            const auto& prevMinerIdDetails { UnitTestAccess::GetMinerIdEntry(minerid_db, prevPubKey->GetHash()) };
            BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(prevMinerIdDetails));
            BOOST_CHECK_EQUAL(prevMinerIdDetails.mNextMinerId->GetHash(), currentPubKey.GetHash());
        }
    };

    // Check intial state of keys
    checkIds(0, miner2IdPubKey1, nullptr);

    // Perform some key rotations for Miner2
    size_t numRotations { GlobalConfig::GetConfig().GetMinerIdsNumToKeep() * 2 };
    std::vector<CKey> keys { miner2IdKey1 };
    for(size_t i = 1; i < numRotations; ++i)
    {
        // Rotate key
        CKey prevKey { keys.back() };
        const CPubKey& prevPubKey { prevKey.GetPubKey() };
        CKey newKey {};
        newKey.MakeNewKey(true);
        keys.push_back(newKey);
        const CPubKey& newPubKey { newKey.GetPubKey() };

        UniValue baseDocument { CreateValidCoinbaseDocument(prevKey, chainActive.Height() + 1, HexStr(prevPubKey), HexStr(newPubKey), vctxid, "Miner2", {}) };
        std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(newKey, baseDocument) };
        CreateAndProcessBlock({}, baseDocument, signature);

        // Allow datbase pruning to happen
        minerid_db.Prune();

        // Check state of keys
        checkIds(i, newPubKey, &prevPubKey);
    }
}

// Test recent blocks tracking and expiry
BOOST_FIXTURE_TEST_CASE(RecentBlocksTracking, SetupMinerIDChain)
{
    // Increase speed of test by reducing the number of blocks we will need to mine
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationN(200, nullptr);

    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 2U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), 1U);
    auto blocksList { UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db) };
    size_t blockListStartSize { INITIAL_NUM_BLOCKS + 1 };   // Mined blocks + Genesis
    BOOST_REQUIRE_EQUAL(blocksList.size(), blockListStartSize);
    BOOST_CHECK_EQUAL(blocksList[0].mHeight, 0);
    BOOST_CHECK_EQUAL(blocksList[blockListStartSize - 1].mHeight, static_cast<int32_t>(blockListStartSize - 1));

    // Mine an additional block for each of Miner1, Miner2, Miner3
    {
        UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), vctxid, "Miner1", {}) };
        std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument) };
        CreateAndProcessBlock({}, baseDocument, signature);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
        blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
        BOOST_REQUIRE_EQUAL(blocksList.size(), blockListStartSize + 1);
        BOOST_CHECK_EQUAL(blocksList[blockListStartSize + 1 - 1].mHeight, static_cast<int32_t>(blockListStartSize + 1 - 1));
    }

    {
        UniValue baseDocument { CreateValidCoinbaseDocument(miner2IdKey1, chainActive.Height() + 1, HexStr(miner2IdPubKey1), HexStr(miner2IdPubKey1), vctxid, "Miner2", {}) };
        std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner2IdKey1, baseDocument) };
        CreateAndProcessBlock({}, baseDocument, signature);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), 2U);
        blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
        BOOST_REQUIRE_EQUAL(blocksList.size(), blockListStartSize + 2);
        BOOST_CHECK_EQUAL(blocksList[blockListStartSize + 2 - 1].mHeight, static_cast<int32_t>(blockListStartSize + 2 - 1));
    }

    {
        UniValue baseDocument { CreateValidCoinbaseDocument(miner3IdKey1, chainActive.Height() + 1, HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), vctxid, "Miner3", {}) };
        std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner3IdKey1, baseDocument) };
        CreateAndProcessBlock({}, baseDocument, signature);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner3"), 1U);
        blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
        BOOST_REQUIRE_EQUAL(blocksList.size(), blockListStartSize + 3);
        BOOST_CHECK_EQUAL(blocksList[blockListStartSize + 3 - 1].mHeight, static_cast<int32_t>(blockListStartSize + 3 - 1));
    }

    // Calculate how many additional blocks we need to mine to overflow the configured number of blocks to track
    size_t numAdditionalBlocks { GlobalConfig::GetConfig().GetMinerIdReputationN() - blocksList.size() };
    // Take us upto, but not over, that limit
    for(size_t i = 1; i <= numAdditionalBlocks; ++i)
    {
        UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), vctxid, "Miner1", {}) };
        std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument) };
        CreateAndProcessBlock({}, baseDocument, signature);
    }
    blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
    BOOST_REQUIRE_EQUAL(blocksList.size(), GlobalConfig::GetConfig().GetMinerIdReputationN());
    BOOST_CHECK_EQUAL(blocksList.front().mHeight, 0);
    BOOST_CHECK_EQUAL(blocksList.back().mHeight, static_cast<int32_t>(GlobalConfig::GetConfig().GetMinerIdReputationN() - 1));

    // And now take us over the limit
    UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), vctxid, "Miner1", {}) };
    std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument) };
    CreateAndProcessBlock({}, baseDocument, signature);
    blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
    BOOST_REQUIRE_EQUAL(blocksList.size(), GlobalConfig::GetConfig().GetMinerIdReputationN());
    BOOST_CHECK_EQUAL(blocksList.front().mHeight, 1);
    BOOST_CHECK_EQUAL(blocksList.back().mHeight, static_cast<int32_t>(GlobalConfig::GetConfig().GetMinerIdReputationN()));
}

// Test processing of an invalid block
BOOST_FIXTURE_TEST_CASE(InvalidBlock, SetupMinerIDChain)
{
    // Set M/N in config
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationM(3, nullptr);
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationN(10, nullptr);

    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check Miner1 has a good reputation before we ruin it
    auto minerUUIdEntry { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second };
    BOOST_CHECK(! minerUUIdEntry.mReputation.mVoid);
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner1")));

    // Miner1 now mines an invalid block
    UniValue baseDocument { CreateValidCoinbaseDocument(miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), vctxid, "Miner1", {}) };
    std::vector<uint8_t> signature { CreateSignatureStaticCoinbaseDocument(miner1IdKey2, baseDocument) };
    CreateAndProcessBlock({}, baseDocument, signature, true);

    // Reputation should now be voided
    minerUUIdEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    BOOST_CHECK(minerUUIdEntry.mReputation.mVoid);
    BOOST_CHECK(! MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner1")));
}

BOOST_AUTO_TEST_SUITE_END()

