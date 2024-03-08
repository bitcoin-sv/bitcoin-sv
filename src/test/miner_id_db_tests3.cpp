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
#include "miner_id/revokemid.h"
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
    constexpr size_t INITIAL_NUM_BLOCKS { 100 + 20 };
    // Protocol prefixes
    const std::vector<uint8_t> MinerIDProtocolPrefix   { 0xac, 0x1e, 0xed, 0x88 };
    const std::vector<uint8_t> MinerInfoProtocolPrefix { 0x60, 0x1d, 0xfa, 0xce };
    const std::vector<uint8_t> ProtocolIdVersion { 0x00 };

    // v0.2 or v0.3
    enum class MinerIDOrInfo { MINER_ID, MINER_INFO };

    // Additional fields for creating V3 coinbase documents
    struct V3CoinbaseFields
    {
        V3CoinbaseFields() = default;
        V3CoinbaseFields(MinerIDOrInfo mioi) : idOrInfo{mioi} {}
        V3CoinbaseFields(MinerIDOrInfo mioi, const CKey& prevKey, const CKey& key, const std::optional<CoinbaseDocument::RevocationMessage>& rm)
        : idOrInfo{mioi}, prevRevocationKey{prevKey}, revocationKey{key}, revocationMessage{rm}
        {
            prevRevocationPubKey = prevRevocationKey.GetPubKey();
            revocationPubKey = revocationKey.GetPubKey();
        }

        MinerIDOrInfo idOrInfo { MinerIDOrInfo::MINER_INFO };
        CKey prevRevocationKey {};
        CPubKey prevRevocationPubKey {};
        CKey revocationKey {};
        CPubKey revocationPubKey {};
        std::optional<CoinbaseDocument::RevocationMessage> revocationMessage { std::nullopt };
        std::optional<CKey> revocationCurrentMinerIdKey { std::nullopt };
    };

    // Dummy vctx for v0.2 miner IDs
    const std::string vctxid { "6839008199026098cc78bf5f34c9a6bdf7a8009c9f019f8399c7ca1945b4a4ff" };

    // Save current mempool contents and clear it
    std::vector<CTxMemPoolEntry> SaveMempool()
    {
        std::vector<CTxMemPoolEntry> contents {};
        for(const auto& entry : mempool.GetSnapshot())
        {
            contents.push_back(std::move(entry));
        }
        mempool.Clear();

        // Force JBA to sync to the new mempool contents
        CBlockIndex* pindexPrev {nullptr};
        mining::g_miningFactory->GetAssembler()->CreateNewBlock(CScript{}, pindexPrev);

        return contents;
    }

    // Restore mempool from previously saved contents
    void RestoreMempool(const std::vector<CTxMemPoolEntry>& contents)
    {
        mempool.Clear();

        mining::CJournalChangeSetPtr nullChangeSet {nullptr};
        for(const auto& entry : contents)
        {
            mempool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, nullChangeSet);
        }

        // Force JBA to sync to the new mempool contents
        CBlockIndex* pindexPrev {nullptr};
        mining::g_miningFactory->GetAssembler()->CreateNewBlock(CScript{}, pindexPrev);
    }

    // Signature calculation for previous miner ID
    std::string CalculatePrevMinerIdSignature(
        const CKey& prevMinerIdKey,
        const std::string& prevMinerIdPubKey,
        const std::string& minerIdPubKey,
        MinerIDOrInfo idOrInfo)
    {
        std::string dataToSign {};
        transform_hex(prevMinerIdPubKey, back_inserter(dataToSign));
        transform_hex(minerIdPubKey, back_inserter(dataToSign));
        if(idOrInfo == MinerIDOrInfo::MINER_ID)
        {
            transform_hex(vctxid, back_inserter(dataToSign));
        }
        uint8_t hashPrevSignature[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(reinterpret_cast<const uint8_t*>(&dataToSign[0]), dataToSign.size()).Finalize(hashPrevSignature);
        std::vector<uint8_t> prevMinerIdSignature {};
        BOOST_CHECK(prevMinerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashPrevSignature), std::end(hashPrevSignature)}), prevMinerIdSignature));
        return HexStr(prevMinerIdSignature);
    }

    // Signature calculation for previous revocation key
    std::string CalculatePrevRevocationKeySignature(
        const CKey& prevRevocationKey,
        const CPubKey& prevRevocationPubKey,
        const CPubKey& revocationPubKey)
    {
        std::string hexEncodedPrevRevocationPubKey { HexStr(prevRevocationPubKey) };
        std::string hexEncodedRevocationPubKey { HexStr(revocationPubKey) };

        std::string dataToSign {};
        transform_hex(hexEncodedPrevRevocationPubKey, back_inserter(dataToSign));
        transform_hex(hexEncodedRevocationPubKey, back_inserter(dataToSign));

        uint8_t hashPrevSignature[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(reinterpret_cast<const uint8_t*>(&dataToSign[0]), dataToSign.size()).Finalize(hashPrevSignature);
        std::vector<uint8_t> prevRevocationKeySignature {};
        BOOST_CHECK(prevRevocationKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashPrevSignature), std::end(hashPrevSignature)}), prevRevocationKeySignature));
        return HexStr(prevRevocationKeySignature);
    }

    // Signature calculation for miner-info document or miner-info-ref
    template<typename Document>
    std::vector<uint8_t> CreateSignatureOverDocument(const CKey& minerIdKey, const Document& document)
    {
        std::vector<uint8_t> documentBytes { document.begin(), document.end() };
        uint8_t hashSignature[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(documentBytes.data(), documentBytes.size()).Finalize(hashSignature);
        std::vector<uint8_t> signature {};
        BOOST_CHECK(minerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashSignature), std::end(hashSignature)}), signature));
        return signature;
    }

    // Signature calculation for revocation message
    UniValue CreateSignatureRevocationMessage(
        const CoinbaseDocument::RevocationMessage& message,
        const CKey& revocationKey,
        const CKey& minerIdKey)
    {
        const std::vector<uint8_t> dataToSign { ParseHex(message.mCompromisedId) };

        uint8_t hashForSigning[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(dataToSign.data(), dataToSign.size()).Finalize(hashForSigning);
        std::vector<uint8_t> sig1 {};
        BOOST_CHECK(revocationKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashForSigning), std::end(hashForSigning)}), sig1));
        std::vector<uint8_t> sig2 {};
        BOOST_CHECK(minerIdKey.Sign(uint256(std::vector<uint8_t> {std::begin(hashForSigning), std::end(hashForSigning)}), sig2));

        UniValue revocationMessageSig { UniValue::VOBJ };
        revocationMessageSig.push_back(Pair("sig1", HexStr(sig1)));
        revocationMessageSig.push_back(Pair("sig2", HexStr(sig2)));

        return revocationMessageSig;
    }

    // Create a static coinbase document with miner ID details
    UniValue CreateValidCoinbaseDocument(
        const CKey& prevMinerIdKey,
        int32_t height,
        const std::string& prevMinerIdPubKey,
        const std::string& minerIdPubKey,
        const std::string& minerName,
        const std::optional<std::vector<CoinbaseDocument::DataRef>>& dataRefs,
        const V3CoinbaseFields& v3Params)
    {
        UniValue document { UniValue::VOBJ };

        document.push_back(Pair("height", height));
        document.push_back(Pair("minerId", minerIdPubKey));
        document.push_back(Pair("prevMinerId", prevMinerIdPubKey));
        document.push_back(Pair("prevMinerIdSig",
            CalculatePrevMinerIdSignature(prevMinerIdKey, prevMinerIdPubKey, minerIdPubKey, v3Params.idOrInfo)));

        // Differences between 0.2 and 0.3
        if(v3Params.idOrInfo == MinerIDOrInfo::MINER_INFO)
        {
            document.push_back(Pair("version", "0.3"));
            document.push_back(Pair("prevRevocationKey", HexStr(v3Params.prevRevocationPubKey)));
            document.push_back(Pair("prevRevocationKeySig",
                CalculatePrevRevocationKeySignature(v3Params.prevRevocationKey, v3Params.prevRevocationPubKey, v3Params.revocationPubKey)));
            document.push_back(Pair("revocationKey", HexStr(v3Params.revocationPubKey)));
            if(v3Params.revocationMessage)
            {
                UniValue revocationMessage { UniValue::VOBJ };
                revocationMessage.push_back(Pair("compromised_minerId", v3Params.revocationMessage->mCompromisedId));
                document.push_back(Pair("revocationMessage", revocationMessage));
                document.push_back(Pair("revocationMessageSig", CreateSignatureRevocationMessage(
                    v3Params.revocationMessage.value(), v3Params.revocationKey, v3Params.revocationCurrentMinerIdKey.value())));
            }
        }
        else
        {
            document.push_back(Pair("version", "0.2"));
            UniValue vctx { UniValue::VOBJ };
            vctx.push_back(Pair("txId", vctxid));
            vctx.push_back(Pair("vout", 0));
            document.push_back(Pair("vctx", vctx));
        }

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
            UniValue extensions = {UniValue::VOBJ};
            extensions.push_back(Pair("dataRefs", dataRefsJson));
            document.push_back(Pair("extensions", extensions));
        }

        return document;
    }

    // Create a miner ID in coinbase
    void CreateMinerIDInCoinbase(
        const UniValue& baseDocument,
        const std::vector<uint8_t>& signature,
        CBlock& block,
        bool invalid = false)
    {
        std::string coinbaseDocument { baseDocument.write() };
        std::vector<uint8_t> coinbaseDocumentBytes(coinbaseDocument.begin(), coinbaseDocument.end());

        // Update coinbase
        CMutableTransaction coinbase { *(block.vtx[0]) };
        coinbase.vout.resize(2);
        coinbase.vout[1].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << MinerIDProtocolPrefix
                                                  << coinbaseDocumentBytes << signature;
        if(invalid)
        {
            // If we want this block to be invalid, screw up the fees
            coinbase.vout[1].nValue = Amount{1000000000000};
        }
        else
        {
            coinbase.vout[1].nValue = Amount{0};
        }

        block.vtx[0] = MakeTransactionRef(std::move(coinbase));
    }

    // Calculate modified merkle root for blockbind
    uint256 CalcModifiedMerkleRoot(const CMutableTransaction& origCoinbase, const CBlock& block)
    {
        // Modify coinbase txn to replace input scriptSig and output scriptPubKey
        CMutableTransaction coinbase { origCoinbase };
        coinbase.nVersion = 0x00000001;
        std::array<uint8_t, 8> v {};    // 0 initialised
        coinbase.vin[0].scriptSig = CScript { v.cbegin(), v.cend() };
        coinbase.vin[0].prevout = { uint256{}, 0xFFFFFFFF };

        // Calculate merkle root for block with modified coinbase txn
        std::vector<uint256> leaves(block.vtx.size());
        leaves[0] = coinbase.GetId();
        for(size_t i = 1; i < block.vtx.size(); ++i)
        {
            leaves[i] = block.vtx[i]->GetId();
        }

        return ComputeMerkleRoot(leaves);
    }

    // Create miner-info reference in a coinbase transaction
    void CreateMinerInfoRefInCoinbase(
        const uint256& infoTxid,
        const CKey& minerKey,
        CBlock& block,
        bool invalid = false)
    {
        // Create partially populated coinbase
        const std::vector<uint8_t> txidBytes { infoTxid.begin(), infoTxid.end() };
        CMutableTransaction coinbase { *(block.vtx[0]) };
        coinbase.vout.resize(2);
        coinbase.vout[1].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << MinerInfoProtocolPrefix << ProtocolIdVersion << txidBytes;
        if(invalid)
        {
            // If we want this block to be invalid, screw up the fees
            coinbase.vout[1].nValue = Amount{1000000000000};
        }
        else
        {
            coinbase.vout[1].nValue = Amount{0};
        }

        // Calculate modified merkle root
        const uint256 modifiedMerkleRoot { CalcModifiedMerkleRoot(coinbase, block) };

        // Sign SHA256(concat(modifiedMerkleRoot, prevBlockHash))
        std::vector<uint8_t> concatMerklePrevBlock {};
        concatMerklePrevBlock.insert(concatMerklePrevBlock.end(), modifiedMerkleRoot.begin(), modifiedMerkleRoot.end());
        concatMerklePrevBlock.insert(concatMerklePrevBlock.end(), block.hashPrevBlock.begin(), block.hashPrevBlock.end());

        uint8_t hashConcatMerklePrevBlock[CSHA256::OUTPUT_SIZE] {};
        CSHA256().Write(reinterpret_cast<const uint8_t*>(concatMerklePrevBlock.data()), concatMerklePrevBlock.size()).Finalize(hashConcatMerklePrevBlock);
        const std::vector<uint8_t> hashConcatMerklePrevBlockBytes { hashConcatMerklePrevBlock, hashConcatMerklePrevBlock + sizeof(hashConcatMerklePrevBlock) };

        std::vector<uint8_t> signature {};
        BOOST_CHECK(minerKey.Sign(uint256 { hashConcatMerklePrevBlockBytes }, signature));

        // Update coinbase
        coinbase.vout[1].scriptPubKey << hashConcatMerklePrevBlockBytes << signature;
        block.vtx[0] = MakeTransactionRef(std::move(coinbase));
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

            // Setup ID keys
            miner1IdKey1.MakeNewKey(true);
            miner1IdPubKey1 = miner1IdKey1.GetPubKey();
            miner1IdKey2.MakeNewKey(true);
            miner1IdPubKey2 = miner1IdKey2.GetPubKey();
            miner2IdKey1.MakeNewKey(true);
            miner2IdPubKey1 = miner2IdKey1.GetPubKey();
            miner3IdKey1.MakeNewKey(true);
            miner3IdPubKey1 = miner3IdKey1.GetPubKey();
            miner4IdKey1.MakeNewKey(true);
            miner4IdPubKey1 = miner4IdKey1.GetPubKey();

            // Setup revocation keys and create starting v3 coinbase fields
            CKey revocationKey {};
            revocationKey.MakeNewKey(true);
            miner1V3Fields = { MinerIDOrInfo::MINER_INFO, revocationKey, revocationKey, {} };
            revocationKey.MakeNewKey(true);
            miner2V3Fields = { MinerIDOrInfo::MINER_INFO, revocationKey, revocationKey, {} };
            revocationKey.MakeNewKey(true);
            miner3V3Fields = { MinerIDOrInfo::MINER_INFO, revocationKey, revocationKey, {} };
            miner4V3Fields = { MinerIDOrInfo::MINER_ID };

            // Generate a block chain with 2 miners
            int32_t startingHeight { chainActive.Height() };
            for(int32_t height = 1; height <= 20; height++)
            {
                int32_t blockHeight { startingHeight + height };

                if(height == 4 || height == 6 || height == 8)
                {
                    // Include miner ID for Miner 1
                    if(height == 8)
                    {
                        // Miner 1 rotate from key 1 to key 2
                        UniValue baseDocument { CreateValidCoinbaseDocument(
                            miner1IdKey1, blockHeight, HexStr(miner1IdPubKey1), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
                        CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
                    }
                    else
                    {
                        // Miner 1 use key 1
                        UniValue baseDocument { CreateValidCoinbaseDocument(
                            miner1IdKey1, blockHeight, HexStr(miner1IdPubKey1), HexStr(miner1IdPubKey1), "Miner1", {}, miner1V3Fields) };
                        CreateAndProcessBlock({}, baseDocument, miner1IdKey1);
                    }
                }
                else if(height == 10)
                {
                    // Create dataref txns in this block
                    CreateDataRefTxns();

                    // Use datarefs in this miners coinbase doc
                    std::vector<CoinbaseDocument::DataRef> datarefs {
                        { {dataRefTxnBrfcIds[0]}, dataRefTxns[0]->GetId(), 0, ""},
                        { {dataRefTxnBrfcIds[1]}, dataRefTxns[1]->GetId(), 0, ""}
                    };

                    // Miner 2 uses dataref
                    UniValue baseDocument { CreateValidCoinbaseDocument(
                        miner2IdKey1, blockHeight, HexStr(miner2IdPubKey1), HexStr(miner2IdPubKey1), "Miner2", {datarefs}, miner2V3Fields) };
                    CreateAndProcessBlock({}, baseDocument, miner2IdKey1);
                }
                else if(height == 12)
                {
                    // Miner4 starts out using version 0.2 Miner ID & will later switch to v0.3
                    UniValue baseDocument { CreateValidCoinbaseDocument(
                        miner4IdKey1, blockHeight, HexStr(miner4IdPubKey1), HexStr(miner4IdPubKey1), "Miner4", {}, miner4V3Fields) };
                    CreateAndProcessBlock({}, baseDocument, miner4IdKey1, MinerIDOrInfo::MINER_ID);
                }
                else
                {
                    // Generic, non-miner ID block
                    CreateAndProcessBlock();
                }
            }

            // Generate a competing fork for a 3rd miner
            UniValue baseDocument { CreateValidCoinbaseDocument(
                miner3IdKey1, chainActive.Height(), HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), "Miner3", {}, miner3V3Fields) };
            CBlock forkBlock { CreateAndProcessBlock(chainActive.Tip()->GetPrev()->GetBlockHash(),
                baseDocument, miner3IdKey1, MinerIDOrInfo::MINER_INFO, false, true) };
            forkBlockId = forkBlock.GetHash();
        }

        ~SetupMinerIDChain()
        {
            g_dataRefIndex.reset();
            pMerkleTreeFactory.reset();
        }

        // Get a funding txn
        CTransactionRef GetFundingTxn()
        {
            CTransactionRef txn { fundingTxns.front() };
            fundingTxns.erase(fundingTxns.begin());
            return txn;
        }

        // Build txn with miner-info output and append to block
        CTransactionRef AddMinerInfoTxnToBlock(
            const CTransactionRef& fundTxn,
            const std::string& minerInfoJson,
            const std::vector<uint8_t>& signature,
            CBlock& block)
        {
            CMutableTransaction txn {};
            txn.vin.resize(1);
            txn.vin[0].prevout = COutPoint { fundTxn->GetId(), 0 };
            txn.vout.resize(1);
            txn.vout[0].nValue = Amount{0};
            txn.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << MinerInfoProtocolPrefix << ProtocolIdVersion
                                                 << std::vector<uint8_t> { minerInfoJson.begin(), minerInfoJson.end() }
                                                 << signature;

            // Sign
            std::vector<uint8_t> vchSig {};
            CScript scriptPubKey { CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG };
            uint256 hash = SignatureHash(scriptPubKey, CTransaction{txn}, 0, SigHashType().withForkId(), fundTxn->vout[0].nValue);
            BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
            vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
            txn.vin[0].scriptSig << vchSig;

            CTransactionRef txnRef { MakeTransactionRef(std::move(txn)) };

            // Append to block
            block.vtx.push_back(txnRef);

            return txnRef;
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

            // Build and submit dataref txn to mempool
            auto SubmitTxn = [this](const CTransactionRef& fundTxn, const std::string& dataRefJson)
            {
                CMutableTransaction txn {};
                txn.vin.resize(1);
                txn.vin[0].prevout = COutPoint { fundTxn->GetId(), 0 };
                txn.vout.resize(1);
                txn.vout[0].nValue = Amount{0};
                txn.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << MinerInfoProtocolPrefix
                                                     << std::vector<uint8_t> { dataRefJson.begin(), dataRefJson.end() };

                // Sign
                std::vector<uint8_t> vchSig {};
                CScript scriptPubKey { CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG };
                uint256 hash = SignatureHash(scriptPubKey, CTransaction{txn}, 0, SigHashType().withForkId(), fundTxn->vout[0].nValue);
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
                CTransactionRef txn { SubmitTxn(GetFundingTxn(), dataRefJson[i-1]) };
                dataRefTxns.push_back(txn);
            }
        }

        // Create a new block and add it to the blockchain
        CBlock CreateAndProcessBlock(
            const std::optional<uint256> prevBlockHash = {},
            const std::optional<UniValue>& baseDocument = {},
            const std::optional<CKey>& minerKey = {},
            MinerIDOrInfo idOrInfo = MinerIDOrInfo::MINER_INFO,
            bool invalid = false,
            bool newCoinbaseKey = false)
        {
            CKey coinbaseKeyToUse { coinbaseKey };
            if(newCoinbaseKey)
            {
                coinbaseKeyToUse.MakeNewKey(true);
            }

            // Create block template
            const Config& config { GlobalConfig::GetConfig() };
            CBlockIndex* pindexPrev {nullptr};
            CScript scriptPubKey = CScript() << ToByteVector(coinbaseKeyToUse.GetPubKey()) << OP_CHECKSIG;
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
                // Sign base document
                std::vector<uint8_t> signature { CreateSignatureOverDocument(*minerKey, baseDocument->write()) };

                // Update coinbase to include miner ID or miner-info reference
                if(idOrInfo == MinerIDOrInfo::MINER_INFO)
                {
                    // Submit txn containing miner-info document to be included in this block
                    CTransactionRef minerInfoTxn { AddMinerInfoTxnToBlock(GetFundingTxn(), baseDocument->write(), signature, block) };
                    CreateMinerInfoRefInCoinbase(minerInfoTxn->GetId(), *minerKey, block, invalid);
                }
                else
                {
                    CreateMinerIDInCoinbase(*baseDocument, signature, block, invalid);
                }

                block.hashMerkleRoot = BlockMerkleRoot(block);
            }

            // Save coinbase for later spending
            coinbaseTxns.push_back(*block.vtx[0]);
            CTransactionRef coinbaseTxn { MakeTransactionRef(coinbaseTxns[nextCoinbaseIndex++]) };
            fundingTxns.push_back(coinbaseTxn);

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
        CKey miner4IdKey1 {};
        CPubKey miner4IdPubKey1 {};

        // Default additional v3 coinbase fields for each miner
        V3CoinbaseFields miner1V3Fields {};
        V3CoinbaseFields miner2V3Fields {};
        V3CoinbaseFields miner3V3Fields {};
        V3CoinbaseFields miner4V3Fields {};

        // Hash of block at which the fork starts
        uint256 forkBlockId {};

        // List of spendable txns for testing with
        std::vector<CTransactionRef> fundingTxns {};
        unsigned nextCoinbaseIndex {0};

        // Transactions containing dataRefs
        std::vector<CTransactionRef> dataRefTxns {};
        std::vector<std::string> dataRefTxnBrfcIds { "BrfcId1", "BrfcId2" };
    };

    // For ID only
    class miner_id_tests3_id;

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
struct MinerIdDatabase::UnitTestAccess<miner_id_tests3_id>
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
            // Lookup last block we saw from this miner and extract the miner ID
            CBlockIndex* blockindex { mapBlockIndex.Get(entry.second.mLastBlock) };
            BOOST_REQUIRE(blockindex);
            CBlock block {};
            BOOST_REQUIRE(blockindex->ReadBlockFromDisk(block, GlobalConfig::GetConfig()));
            std::optional<MinerId> minerID { FindMinerId(block, blockindex->GetHeight()) };
            BOOST_REQUIRE(minerID);

            // Check for matching minerContact
            const CoinbaseDocument& cbd { minerID->GetCoinbaseDocument() };
            std::optional<UniValue> minerContact { cbd.GetMinerContact() };
            BOOST_REQUIRE(minerContact);
            const auto& minerName { (*minerContact)["name"] };
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
    static bool MinerIdIsRevoked(const MinerIdEntry& id) { return id.mState == MinerIdEntry::State::REVOKED; }
};
using UnitTestAccess = MinerIdDatabase::UnitTestAccess<miner_id_tests3_id>;

// RevokeMid class inspection
template<>
struct RevokeMid::UnitTestAccess<miner_id_tests3_id>
{
    static void MakeBadRevokeKeySig(RevokeMid& msg)
    {
        msg.mEncodedRevocationMessageSig[msg.mEncodedRevocationMessageSig.size() - 5] += 1;

        // Serialise/deserialise to put bad signature in msg object
        CDataStream ss { SER_NETWORK, 0 };
        ss << msg;
        ss >> msg;
    }
};
using RMIDUnitTestAccess = RevokeMid::UnitTestAccess<miner_id_tests3_id>;

BOOST_AUTO_TEST_SUITE(miner_id_db3)

// Test initial create of miner ID database from an existing blockchain, and saving/restoring from disk
BOOST_FIXTURE_TEST_CASE(InitialiseFromExistingChain, SetupMinerIDChain)
{
    // Set M/N in config
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationM(3, nullptr);
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationN(20, nullptr);

    // Check we've got the expected number of blocks
    CBlockIndex* tip { chainActive.Tip() };
    BOOST_CHECK_EQUAL(tip->GetHeight(), static_cast<int32_t>(INITIAL_NUM_BLOCKS));

    // Check miner ID db contains the expected miner details
    auto dbCheckLambda = [this](const MinerIdDatabase& minerid_db)
    {
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

        // Check miner UUId entry for Miner1
        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        CBlockIndex* expectedFirstBlock { chainActive[104] };  // Miner1 first block was height 104
        CBlockIndex* expectedFirstBlock2ndId { chainActive[108] };  // Miner1 2nd key first block was height 107
        CBlockIndex* expectedLastBlock { chainActive[108] };  // Miner1 last block was height 108
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
        expectedFirstBlock = chainActive[110];  // Miner2 first block was height 110
        expectedLastBlock = chainActive[110];  // Miner2 last block was height 110
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

        // Check miner UUId entry for Miner4
        const auto& miner4Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner4") };
        expectedFirstBlock = chainActive[112];  // Miner4 first block was height 112
        expectedLastBlock = chainActive[112];  // Miner4 last block was height 112
        BOOST_CHECK_EQUAL(expectedFirstBlock->GetBlockHash(), miner4Details.second.mFirstBlock);
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner4Details.second.mLastBlock);
        BOOST_CHECK(! miner4Details.second.mReputation.mVoid);
        BOOST_CHECK_EQUAL(miner4Details.second.mLatestMinerId, miner4IdPubKey1.GetHash());

        // Check miner ID entries for Miner4
        const auto& miner4Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner4IdPubKey1.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner4Key1Details));
        BOOST_CHECK_EQUAL(expectedFirstBlock->GetBlockHash(), miner4Key1Details.mCreationBlock);
        BOOST_CHECK_EQUAL(miner4Key1Details.mPrevMinerId.GetHash(), miner4IdPubKey1.GetHash());
        BOOST_CHECK(! miner4Key1Details.mNextMinerId);
        BOOST_CHECK_EQUAL(miner4Key1Details.mCoinbaseDoc.GetVersion(), "0.2");

        // Check recent block details for Miner4
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner4"), 1U);
        BOOST_CHECK(! MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner4")));
    };

    {
        // Create a miner ID database which should build itself for the first time from the blockchain
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

    // Lambda for checking mempool filtering after reorgs
    auto CheckMempool = []() {
        for(const auto& entry : mempool.InfoAll())
        {
            const auto& tx { entry.GetTx() };
            bool containsMinerId {false};
            for(size_t i = 0; i < tx->vout.size(); i++)
            {
                const span<const uint8_t> script { tx->vout[i].scriptPubKey };
                if(IsMinerId(script) || IsMinerInfo(script))
                {
                    containsMinerId = true;
                    break;
                }
            }

            BOOST_CHECK(!tx->IsCoinBase() && !containsMinerId);
        }
    };

    uint256 miner1LastBlockId {};

    {
        // Extend the current chain
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner1IdKey2);

        // Check the updates to the miner ID database
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        CBlockIndex* expectedLastBlock { chainActive.Tip() };
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner1Details.second.mLastBlock);
        miner1LastBlockId = miner1Details.second.mLastBlock;
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1IdPubKey2.GetHash());
    }

    // Because we're simulating 2 miners mining competing chains but only have a single mempool
    // available from which to assemble blocks, we need to save and restore the mempool
    // contents between reorgs to ensure we don't end up mining blocks with miner-info txns
    // multiple times.
    auto miner1Mempool { SaveMempool() };

    {
        CheckMempool();

        // Extend the fork to force a reorg
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner3IdKey1, chainActive.Height(), HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), "Miner3", {}, miner3V3Fields) };
        CBlock forkBlock { CreateAndProcessBlock(forkBlockId, baseDocument, miner3IdKey1, MinerIDOrInfo::MINER_INFO, false, true) };
        baseDocument = CreateValidCoinbaseDocument(
            miner3IdKey1, chainActive.Height() + 1, HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), "Miner3", {}, miner3V3Fields);
        forkBlock = CreateAndProcessBlock(forkBlock.GetHash(), baseDocument, miner3IdKey1, MinerIDOrInfo::MINER_INFO, false, true);

        // Check the updates to the miner ID database
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 5U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 4U);

        const auto& miner3Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner3") };
        CBlockIndex* expectedLastBlock { chainActive.Tip() };
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner3Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner3"), 3U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
        BOOST_CHECK_EQUAL(miner3Details.second.mLatestMinerId, miner3IdPubKey1.GetHash());

        CheckMempool();
    }

    {
        // Reorg back to the original chain
        RestoreMempool(miner1Mempool);
        CheckMempool();
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height(), HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
        CBlock forkBlock { CreateAndProcessBlock(miner1LastBlockId, baseDocument, miner1IdKey2) };
        // Won't see new blocks from Miner1 until reorg happens
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
        baseDocument = CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
        forkBlock = CreateAndProcessBlock(forkBlock.GetHash(), baseDocument, miner1IdKey2);

        // Check the updates to the miner ID database
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 5U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 4U);

        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        CBlockIndex* expectedLastBlock { chainActive.Tip() };
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner1Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 6U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner3"), 0U);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1IdPubKey2.GetHash());

        CheckMempool();
    }

    {
        // Check we don't count updates unless they come from the current ID
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey1, chainActive.Height() + 1, HexStr(miner1IdPubKey1), HexStr(miner1IdPubKey1), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner1IdKey1);

        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 6U);
        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetPrev()->GetBlockHash(), miner1Details.second.mLastBlock);
    }

    {
        // Check next time we see miner 3 on the main chain we update their ID creation block
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner3IdKey1, chainActive.Height() + 1, HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), "Miner3", {}, miner3V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner3IdKey1);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner3"), 1U);
        const auto& miner3IdDetails { UnitTestAccess::GetMinerIdEntry(minerid_db, miner3IdPubKey1.GetHash()) };
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), miner3IdDetails.mCreationBlock);
    }
}

// Test main chain miner ID key rotation
BOOST_FIXTURE_TEST_CASE(KeyRotation, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

    // Check miner IDs for Miner2
    auto checkIds = [&minerid_db, this](unsigned numRotations, const CPubKey& currentPubKey, const CPubKey* prevPubKey)
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

        // Check current and previous revocation keys in the DB are what we expect
        const auto& cbDoc { currMinerIdDetails.mCoinbaseDoc };
        BOOST_CHECK_EQUAL(cbDoc.GetPrevRevocationKey().GetHash(), miner2V3Fields.prevRevocationPubKey.GetHash());
        BOOST_CHECK_EQUAL(cbDoc.GetRevocationKey().GetHash(), miner2V3Fields.revocationPubKey.GetHash());
    };

    // Check intial state of keys
    checkIds(0, miner2IdPubKey1, nullptr);

    // Check a basic revocation key rotation on its own
    {
        CKey newRevocationKey {};
        newRevocationKey.MakeNewKey(true);
        miner2V3Fields.revocationKey = newRevocationKey;
        miner2V3Fields.revocationPubKey = newRevocationKey.GetPubKey();

        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner2IdKey1, chainActive.Height() + 1, HexStr(miner2IdPubKey1), HexStr(miner2IdPubKey1), "Miner2", {}, miner2V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner2IdKey1);
        checkIds(0, miner2IdPubKey1, nullptr);
    }

    // Perform some combined key rotations for Miner2
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

        // Every 3 ID rotations, also rotate the revocation key
        miner2V3Fields.prevRevocationPubKey = miner2V3Fields.revocationPubKey;
        miner2V3Fields.prevRevocationKey = miner2V3Fields.revocationKey;
        if(i % 3 == 0)
        {
            CKey newRevocationKey {};
            newRevocationKey.MakeNewKey(true);
            miner2V3Fields.revocationKey = newRevocationKey;
            miner2V3Fields.revocationPubKey = newRevocationKey.GetPubKey();
        }

        UniValue baseDocument { CreateValidCoinbaseDocument(
            prevKey, chainActive.Height() + 1, HexStr(prevPubKey), HexStr(newPubKey), "Miner2", {}, miner2V3Fields) };
        CreateAndProcessBlock({}, baseDocument, newKey);

        // Allow database pruning to happen
        minerid_db.Prune();

        // Check state of keys
        checkIds(i, newPubKey, &prevPubKey);
    }

    // Expected last block from this miner for the next few tests
    CBlockIndex* expectedLastBlock { chainActive.Tip() };
    size_t expectedNumBlocks { UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2") };

    // Check we reject use of a non-current miner ID
    {
        CKey oldKey { keys[keys.size() - 2] };
        const auto& oldMinerIdDetails { UnitTestAccess::GetMinerIdEntry(minerid_db, oldKey.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(oldMinerIdDetails));

        CBlockIndex* prevTip { chainActive.Tip() };
        UniValue baseDocument { CreateValidCoinbaseDocument(
            oldKey, chainActive.Height() + 1, HexStr(oldKey.GetPubKey()), HexStr(oldKey.GetPubKey()), "Miner2", {}, miner2V3Fields) };
        CreateAndProcessBlock({}, baseDocument, oldKey);
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetPrev()->GetBlockHash(), prevTip->GetBlockHash());

        const auto& miner2Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner2") };
        // We won't have accepted the last block as from Miner2
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner2Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), expectedNumBlocks);
    }

    // Check we reject an attempt to re-roll an already rotated miner ID
    {
        CKey oldKey { keys[keys.size() - 2] };
        const auto& oldMinerIdDetails { UnitTestAccess::GetMinerIdEntry(minerid_db, oldKey.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(oldMinerIdDetails));
        const auto& oldMinerIds { UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2") };

        CKey newKey {};
        newKey.MakeNewKey(true);
        CBlockIndex* prevTip { chainActive.Tip() };
        UniValue baseDocument { CreateValidCoinbaseDocument(
            oldKey, chainActive.Height() + 1, HexStr(oldKey.GetPubKey()), HexStr(newKey.GetPubKey()), "Miner2", {}, miner2V3Fields) };
        CreateAndProcessBlock({}, baseDocument, newKey);
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetPrev()->GetBlockHash(), prevTip->GetBlockHash());

        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2").size(), oldMinerIds.size());
        BOOST_CHECK_THROW(UnitTestAccess::GetMinerIdEntry(minerid_db, newKey.GetPubKey().GetHash()), std::runtime_error);
        const auto& miner2Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner2") };
        // We won't have accepted the last block as from Miner2
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner2Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), expectedNumBlocks);
    }

    // Check we reject an invalid revocation key rotation attempt
    {
        // An attempt by someone who has compromised our miner ID to force a rotation of our revocation key
        CKey newRevocationKey {};
        newRevocationKey.MakeNewKey(true);
        miner2V3Fields.revocationKey = newRevocationKey;
        miner2V3Fields.revocationPubKey = newRevocationKey.GetPubKey();
        CKey wrongPrevRevocationKey {};
        wrongPrevRevocationKey.MakeNewKey(true);
        miner2V3Fields.prevRevocationKey = wrongPrevRevocationKey;
        miner2V3Fields.prevRevocationPubKey = wrongPrevRevocationKey.GetPubKey();

        CBlockIndex* prevTip { chainActive.Tip() };
        CKey currId { keys.back() };
        UniValue baseDocument { CreateValidCoinbaseDocument(
            currId, chainActive.Height() + 1, HexStr(currId.GetPubKey()), HexStr(currId.GetPubKey()), "Miner2", {}, miner2V3Fields) };
        CreateAndProcessBlock({}, baseDocument, currId);
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetPrev()->GetBlockHash(), prevTip->GetBlockHash());

        const auto& miner2Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner2") };
        BOOST_CHECK_EQUAL(miner2Details.second.mLatestMinerId, currId.GetPubKey().GetHash());
        // We won't have accepted the last block as from Miner2
        BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner2Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), expectedNumBlocks);
    }
}

// Test miner ID key rotation on a fork then the main chain
BOOST_FIXTURE_TEST_CASE(KeyRotationFork, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

    CBlockIndex* oldTip { chainActive.Tip() };
    CBlockIndex* miner1LastBlock {nullptr};
    CKey miner1LatestId {};

    {
        // Extend the fork to force a reorg
        CreateAndProcessBlock(forkBlockId, {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 5U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 4U);

        // Perform a key rotation for miner 1 on the fork
        miner1LatestId.MakeNewKey(true);
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1LatestId.GetPubKey()), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner1LatestId, MinerIDOrInfo::MINER_INFO, false, true);
        miner1LastBlock = chainActive.Tip();

        // Check the updates to the miner ID database
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 6U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 4U);

        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        BOOST_CHECK_EQUAL(miner1LastBlock->GetBlockHash(), miner1Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1LatestId.GetPubKey().GetHash());

        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1LatestId.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key2Details));
        BOOST_CHECK_EQUAL(miner1Key2Details.mCreationBlock, miner1LastBlock->GetBlockHash());
    }

    {
        // Reorg back to the main chain
        CBlock lastBlock { CreateAndProcessBlock(oldTip->GetBlockHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true) };
        lastBlock = CreateAndProcessBlock(lastBlock.GetHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
        lastBlock = CreateAndProcessBlock(lastBlock.GetHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), lastBlock.GetHash());
    }

    {
        // Re-apply miner 1 rotation on the main chain
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1LatestId.GetPubKey()), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner1LatestId);
        miner1LastBlock = chainActive.Tip();

        {
            // Check nodes that have seen both forks have the correct view
            const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
            BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), miner1Details.second.mLastBlock);
            BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
            BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1LatestId.GetPubKey().GetHash());

            const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
            const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1LatestId.GetPubKey().GetHash()) };
            BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
            BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key2Details));
            BOOST_CHECK_EQUAL(miner1Key2Details.mCoinbaseDoc.GetHeight(), chainActive.Height());
            BOOST_CHECK_EQUAL(miner1Key2Details.mCreationBlock, miner1LastBlock->GetBlockHash());
        }

        // Check nodes that have only seen the main chain have the correct view
        minerid_db.TriggerSync(true, true);
        UnitTestAccess::WaitForSync(minerid_db);

        {
            const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
            BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), miner1Details.second.mLastBlock);
            BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
            BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, miner1LatestId.GetPubKey().GetHash());

            const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
            const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1LatestId.GetPubKey().GetHash()) };
            BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
            BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key2Details));
            BOOST_CHECK_EQUAL(miner1Key2Details.mCoinbaseDoc.GetHeight(), chainActive.Height());
            BOOST_CHECK_EQUAL(miner1Key2Details.mCreationBlock, miner1LastBlock->GetBlockHash());
        }
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
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), 1U);
    auto blocksList { UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db) };
    size_t blockListStartSize { INITIAL_NUM_BLOCKS + 1 };   // Mined blocks + Genesis
    BOOST_REQUIRE_EQUAL(blocksList.size(), blockListStartSize);
    BOOST_CHECK_EQUAL(blocksList[0].mHeight, 0);
    BOOST_CHECK_EQUAL(blocksList[blockListStartSize - 1].mHeight, static_cast<int32_t>(blockListStartSize - 1));

    // Mine an additional block for each of Miner1, Miner2, Miner3
    {
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
        blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
        BOOST_REQUIRE_EQUAL(blocksList.size(), blockListStartSize + 1);
        BOOST_CHECK_EQUAL(blocksList[blockListStartSize + 1 - 1].mHeight, static_cast<int32_t>(blockListStartSize + 1 - 1));
    }

    {
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner2IdKey1, chainActive.Height() + 1, HexStr(miner2IdPubKey1), HexStr(miner2IdPubKey1), "Miner2", {}, miner2V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner2IdKey1);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner2"), 2U);
        blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
        BOOST_REQUIRE_EQUAL(blocksList.size(), blockListStartSize + 2);
        BOOST_CHECK_EQUAL(blocksList[blockListStartSize + 2 - 1].mHeight, static_cast<int32_t>(blockListStartSize + 2 - 1));
    }

    {
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner3IdKey1, chainActive.Height() + 1, HexStr(miner3IdPubKey1), HexStr(miner3IdPubKey1), "Miner3", {}, miner3V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner3IdKey1);
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
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    }
    blocksList = UnitTestAccess::GetRecentBlocksOrderedByHeight(minerid_db);
    BOOST_REQUIRE_EQUAL(blocksList.size(), GlobalConfig::GetConfig().GetMinerIdReputationN());
    BOOST_CHECK_EQUAL(blocksList.front().mHeight, 0);
    BOOST_CHECK_EQUAL(blocksList.back().mHeight, static_cast<int32_t>(GlobalConfig::GetConfig().GetMinerIdReputationN() - 1));

    // And now take us over the limit
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
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
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationN(20, nullptr);

    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check Miner1 has a good reputation before we ruin it
    auto minerUUIdEntry { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second };
    BOOST_CHECK(! minerUUIdEntry.mReputation.mVoid);
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner1")));

    // First check we can't void a miners reputation using on old (non-current) ID
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey1, chainActive.Height() + 1, HexStr(miner1IdPubKey1), HexStr(miner1IdPubKey1), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, miner1IdKey1, MinerIDOrInfo::MINER_INFO, true);
    minerUUIdEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    BOOST_CHECK(! minerUUIdEntry.mReputation.mVoid);
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner1")));

    // Miner1 now mines an invalid block using their current ID
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2, MinerIDOrInfo::MINER_INFO, true);

    // Reputation should now be voided
    minerUUIdEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    BOOST_CHECK(minerUUIdEntry.mReputation.mVoid);
    BOOST_CHECK_EQUAL(minerUUIdEntry.mReputation.mVoidingId->GetHash(), miner1IdPubKey2.GetHash());
    BOOST_CHECK(! MinerHasGoodReputation(minerid_db, UnitTestAccess::GetLatestMinerIdByName(minerid_db, mapBlockIndex, "Miner1")));
}

// Test switching from v0.2 to 0.3 without any rotation
BOOST_FIXTURE_TEST_CASE(SwitchVersion, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner4"), 1U);

    // Miner4 attempt to switch from 0.2 to 0.3 but sets up bad (different) revocation key & previous revocation key
    CKey revocationKey {};
    revocationKey.MakeNewKey(true);
    CKey prevRevocationKey {};
    prevRevocationKey.MakeNewKey(true);
    miner4V3Fields = { MinerIDOrInfo::MINER_INFO, prevRevocationKey, revocationKey, {} };
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner4IdKey1, chainActive.Height() + 1, HexStr(miner4IdPubKey1), HexStr(miner4IdPubKey1), "Miner4", {}, miner4V3Fields) };
    CreateAndProcessBlock({}, baseDocument, miner4IdKey1, MinerIDOrInfo::MINER_INFO);

    // Check miner ID changes were rejected
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner4"), 1U);
    auto miner4Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner4IdPubKey1.GetHash()) };
    BOOST_CHECK_EQUAL(miner4Key1Details.mCoinbaseDoc.GetVersion(), "0.2");

    // Miner4 correctly switches from 0.2 to 0.3
    miner4V3Fields = { MinerIDOrInfo::MINER_INFO, revocationKey, revocationKey, {} };
    baseDocument = CreateValidCoinbaseDocument(
        miner4IdKey1, chainActive.Height() + 1, HexStr(miner4IdPubKey1), HexStr(miner4IdPubKey1), "Miner4", {}, miner4V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner4IdKey1, MinerIDOrInfo::MINER_INFO);

    // Check the updates to the miner ID database
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

    miner4Key1Details = UnitTestAccess::GetMinerIdEntry(minerid_db, miner4IdPubKey1.GetHash());
    BOOST_CHECK_EQUAL(miner4Key1Details.mCoinbaseDoc.GetVersion(), "0.3");
    BOOST_CHECK_EQUAL(miner4Key1Details.mCoinbaseDoc.GetRevocationKey().GetHash(), miner4V3Fields.revocationPubKey.GetHash());
    BOOST_CHECK_EQUAL(miner4Key1Details.mCoinbaseDoc.GetPrevRevocationKey().GetHash(), miner4V3Fields.prevRevocationPubKey.GetHash());

    const auto& miner4Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner4") };
    CBlockIndex* expectedLastBlock { chainActive.Tip() };
    BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner4Details.second.mLastBlock);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner4"), 2U);
    BOOST_CHECK_EQUAL(miner4Details.second.mLatestMinerId, miner4IdPubKey1.GetHash());
}

// Test rotating from v0.2 to 0.3
BOOST_FIXTURE_TEST_CASE(RotateVersion, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner4"), 1U);

    // Create new key to rotate to
    CKey newKey {};
    newKey.MakeNewKey(true);
    const CPubKey& newPubKey { newKey.GetPubKey() };

    // Create initial revocation key to set
    CKey revocationKey {};
    revocationKey.MakeNewKey(true);

    // Perform rotation also switching from v0.2 to v0.3 miner ID
    miner4V3Fields = { MinerIDOrInfo::MINER_INFO, revocationKey, revocationKey, {} };
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner4IdKey1, chainActive.Height() + 1, HexStr(miner4IdPubKey1), HexStr(newPubKey), "Miner4", {}, miner4V3Fields) };
    CreateAndProcessBlock({}, baseDocument, newKey, MinerIDOrInfo::MINER_INFO);

    // Check the updates to the miner ID database
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 5U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

    const auto& miner4Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, newPubKey.GetHash()) };
    BOOST_CHECK_EQUAL(miner4Key2Details.mCoinbaseDoc.GetVersion(), "0.3");
    BOOST_CHECK_EQUAL(miner4Key2Details.mCoinbaseDoc.GetRevocationKey().GetHash(), miner4V3Fields.revocationPubKey.GetHash());
    BOOST_CHECK_EQUAL(miner4Key2Details.mCoinbaseDoc.GetPrevRevocationKey().GetHash(), miner4V3Fields.prevRevocationPubKey.GetHash());

    const auto& miner4Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner4") };
    CBlockIndex* expectedLastBlock { chainActive.Tip() };
    BOOST_CHECK_EQUAL(expectedLastBlock->GetBlockHash(), miner4Details.second.mLastBlock);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner4"), 2U);
    BOOST_CHECK_EQUAL(miner4Details.second.mLatestMinerId, newPubKey.GetHash());
}

// Test partial revocation
BOOST_FIXTURE_TEST_CASE(PartialRevocation, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);

    auto savedV3Fields { miner1V3Fields };

    // Perform another ID rotation for miner 1 so we have 3 IDs for them. Key3 will be one
    // we didn't authorise, so indicates to us that key2 was compromised.
    CKey key3 {};
    key3.MakeNewKey(true);
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, key3);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), miner1IdPubKey2.GetHash());
    }

    // Perform a partial revocation of key2 (and key3), rolling it to a new key4
    CKey key4 {};
    key4.MakeNewKey(true);
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey2 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key4.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key4);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key3Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), key4.GetPubKey().GetHash());
    }

    // Duplicate partial revocation of key2 is handled correctly
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key4.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key4);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key3Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), key4.GetPubKey().GetHash());
        BOOST_CHECK_EQUAL(miner1Key4Details.mCoinbaseDoc.GetHeight(), chainActive.Height());
    }

    // Check a revocation attempt using a wrong revocation key is rejected
    CKey key5 {};
    key5.MakeNewKey(true);
    CKey wrongRevocationKey {};
    wrongRevocationKey.MakeNewKey(true);
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { key4.GetPubKey() };
    miner1V3Fields.revocationCurrentMinerIdKey = key4;
    miner1V3Fields.revocationKey = wrongRevocationKey;
    miner1V3Fields.revocationPubKey = wrongRevocationKey.GetPubKey();
    baseDocument = CreateValidCoinbaseDocument(
        key4, chainActive.Height() + 1, HexStr(key4.GetPubKey()), HexStr(key5.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key5);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_THROW(UnitTestAccess::GetMinerIdEntry(minerid_db, key5.GetPubKey().GetHash()), std::runtime_error);
    }
    miner1V3Fields = savedV3Fields;

    // Check a revocation attempt using wrong revocation and previous revocation fields is rejected
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { key4.GetPubKey() };
    miner1V3Fields.revocationCurrentMinerIdKey = key4;
    miner1V3Fields.revocationKey = wrongRevocationKey;
    miner1V3Fields.revocationPubKey = wrongRevocationKey.GetPubKey();
    miner1V3Fields.prevRevocationKey = wrongRevocationKey;
    miner1V3Fields.prevRevocationPubKey = wrongRevocationKey.GetPubKey();
    baseDocument = CreateValidCoinbaseDocument(
        key4, chainActive.Height() + 1, HexStr(key4.GetPubKey()), HexStr(key5.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key5);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_THROW(UnitTestAccess::GetMinerIdEntry(minerid_db, key5.GetPubKey().GetHash()), std::runtime_error);
    }
    miner1V3Fields = savedV3Fields;

    // Check a revocation attempt incorrectly signed with the wrong revocation key is rejected
    CKey badKeyForSigning {};
    badKeyForSigning.MakeNewKey(true);
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { key4.GetPubKey() };
    miner1V3Fields.revocationCurrentMinerIdKey = key4;
    miner1V3Fields.revocationKey = badKeyForSigning;
    baseDocument = CreateValidCoinbaseDocument(
        key4, chainActive.Height() + 1, HexStr(key4.GetPubKey()), HexStr(key5.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key5);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_THROW(UnitTestAccess::GetMinerIdEntry(minerid_db, key5.GetPubKey().GetHash()), std::runtime_error);
    }
    miner1V3Fields = savedV3Fields;

    // Check a revocation attempt incorrectly signed with the wrong miner ID key is rejected
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { key4.GetPubKey() };
    miner1V3Fields.revocationCurrentMinerIdKey = badKeyForSigning;
    baseDocument = CreateValidCoinbaseDocument(
        key4, chainActive.Height() + 1, HexStr(key4.GetPubKey()), HexStr(key5.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key5);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_THROW(UnitTestAccess::GetMinerIdEntry(minerid_db, key5.GetPubKey().GetHash()), std::runtime_error);
    }
    miner1V3Fields = savedV3Fields;

    // Check we disallow revocation key rolling during partial revocation
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { key4.GetPubKey() };
    miner1V3Fields.revocationCurrentMinerIdKey = key4;
    miner1V3Fields.prevRevocationPubKey = miner1V3Fields.revocationPubKey;
    miner1V3Fields.prevRevocationKey = miner1V3Fields.revocationKey;
    CKey newRevocationKey {};
    newRevocationKey.MakeNewKey(true);
    miner1V3Fields.revocationKey = newRevocationKey;
    miner1V3Fields.revocationPubKey = newRevocationKey.GetPubKey();
    baseDocument = CreateValidCoinbaseDocument(
        key4, chainActive.Height() + 1, HexStr(key4.GetPubKey()), HexStr(key5.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key5);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_THROW(UnitTestAccess::GetMinerIdEntry(minerid_db, key5.GetPubKey().GetHash()), std::runtime_error);
    }
    miner1V3Fields = savedV3Fields;

    // A block claiming to be from a revoked key will not be counted as from this miner
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
}

// Test partial revocation across a fork
BOOST_FIXTURE_TEST_CASE(PartialRevocationFork, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

    CBlockIndex* oldTip { chainActive.Tip() };

    // Extend the fork to force a reorg
    CreateAndProcessBlock(forkBlockId, {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 5U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);

    // Perform a partial revocation of miner 1 key2 on the fork, rolling it to a new key3
    CKey key3 {};
    key3.MakeNewKey(true);
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey2 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, key3);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), key3.GetPubKey().GetHash());
    }

    // Reorg back to the main chain
    CBlock lastBlock { CreateAndProcessBlock(oldTip->GetBlockHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true) };
    lastBlock = CreateAndProcessBlock(lastBlock.GetHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
    lastBlock = CreateAndProcessBlock(lastBlock.GetHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
    BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), lastBlock.GetHash());
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 2U);

    // Reapply revocation on the main chain
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key3);

    {
        // Check nodes that have seen both forks have the correct view
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), miner1Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, key3.GetPubKey().GetHash());

        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), key3.GetPubKey().GetHash());
        BOOST_CHECK_EQUAL(miner1Key3Details.mCreationBlock, chainActive.Tip()->GetBlockHash());
        BOOST_CHECK_EQUAL(miner1Key3Details.mCoinbaseDoc.GetHeight(), chainActive.Height());
    }

    {
        // Check nodes that have only seen the main chain have the correct view
        minerid_db.TriggerSync(true, true);
        UnitTestAccess::WaitForSync(minerid_db);

        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), miner1Details.second.mLastBlock);
        BOOST_CHECK_EQUAL(miner1Details.second.mLatestMinerId, key3.GetPubKey().GetHash());

        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), key3.GetPubKey().GetHash());
        BOOST_CHECK_EQUAL(miner1Key3Details.mCreationBlock, chainActive.Tip()->GetBlockHash());
        BOOST_CHECK_EQUAL(miner1Key3Details.mCoinbaseDoc.GetHeight(), chainActive.Height());
    }
}

// Test partial revocation beyond our pruned history
BOOST_FIXTURE_TEST_CASE(PartialRevocationPruned, SetupMinerIDChain)
{
    // Keep just 2 rotated IDs
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdsNumToKeep(2, nullptr);

    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2").size(), 1U);

    // Perform some rotations
    std::vector<CKey> keys { miner2IdKey1 };
    for(size_t i = 1; i < 5; ++i)
    {
        // Rotate key
        CKey prevKey { keys.back() };
        CKey newKey {};
        newKey.MakeNewKey(true);
        keys.push_back(newKey);

        UniValue baseDocument { CreateValidCoinbaseDocument(
            prevKey, chainActive.Height() + 1, HexStr(prevKey.GetPubKey()), HexStr(newKey.GetPubKey()), "Miner2", {}, miner2V3Fields) };
        CreateAndProcessBlock({}, baseDocument, newKey);
    }
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2").size(), 5U);

    // Allow database pruning to happen
    minerid_db.Prune();
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2").size(), 3U);

    // Partial revocation of all keys except our first; will need to revoke back beyond pruned IDs
    CKey newKey {};
    newKey.MakeNewKey(true);
    CKey& currKey { keys.back() };
    miner2V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { keys[1].GetPubKey() };
    miner2V3Fields.revocationCurrentMinerIdKey = currKey;

    UniValue baseDocument { CreateValidCoinbaseDocument(
        currKey, chainActive.Height() + 1, HexStr(currKey.GetPubKey()), HexStr(newKey.GetPubKey()), "Miner2", {}, miner2V3Fields) };
    CreateAndProcessBlock({}, baseDocument, newKey);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2").size(), 4U);

    // Check state of miner IDs for miner2
    unsigned currentCount {0};
    for(const auto& idEntry : UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner2"))
    {
        // Every key we still have except the current one will be revoked
        if(! UnitTestAccess::MinerIdIsCurrent(idEntry))
        {
            BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(idEntry));
        }
        else
        {
            ++currentCount;
        }
    }
    BOOST_CHECK_EQUAL(currentCount, 1U);
}

// Test partial revocation via a revokemid message
BOOST_FIXTURE_TEST_CASE(RevokemidRevocation, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);

    // Perform another ID rotation for miner 1 so we have 3 IDs for them. Key3 will be one
    // we didn't authorise, so indicates to us that key2 was compromised.
    CKey key3 {};
    key3.MakeNewKey(true);
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, key3);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash().ToString(), miner1IdPubKey2.GetHash().ToString());
    }

    // Send a revokemid message with the wrong revocation key
    CKey badRevocationKey {};
    badRevocationKey.MakeNewKey(true);
    RevokeMid revokemidMsg { badRevocationKey, miner1IdKey2, miner1IdPubKey2 };
    BOOST_CHECK_THROW(minerid_db.ProcessRevokemidMessage(revokemidMsg), std::runtime_error);
    {
        // No change to the state of miner 1's IDs
        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash().ToString(), miner1IdPubKey2.GetHash().ToString());
    }

    // Send a revokemid message with a bad signature
    revokemidMsg = { miner1V3Fields.revocationKey, miner1IdKey2, miner1IdPubKey2 };
    RMIDUnitTestAccess::MakeBadRevokeKeySig(revokemidMsg);
    BOOST_CHECK_THROW(minerid_db.ProcessRevokemidMessage(revokemidMsg), std::runtime_error);
    {
        // No change to the state of miner 1's IDs
        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash().ToString(), miner1IdPubKey2.GetHash().ToString());
    }

    // Perform a partial revocation of key2 (and key3) via a revokemid msg
    revokemidMsg = { miner1V3Fields.revocationKey, miner1IdKey2, miner1IdPubKey2 };
    BOOST_CHECK_NO_THROW(minerid_db.ProcessRevokemidMessage(revokemidMsg));
    {
        // Check revocation state of miner 1's IDs
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 2U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash().ToString(), miner1IdPubKey2.GetHash().ToString());
    }

    // Check we can't now use revoked ID
    baseDocument = CreateValidCoinbaseDocument(
        key3, chainActive.Height() + 1, HexStr(key3.GetPubKey()), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key3);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 2U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);

    // Put revocation in a block on chain as well, that also rotates to new ID key4
    CKey key4 {};
    key4.MakeNewKey(true);
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey2 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key4.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key4);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 4U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        const auto& miner1Key4Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key4.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key3Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key4Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash().ToString(), key4.GetPubKey().GetHash().ToString());
    }
}

// Test a miner can recover their reputation after revoking a compromised ID
BOOST_FIXTURE_TEST_CASE(RecoverReputation, SetupMinerIDChain)
{
    // Set M nice and low
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdReputationM(5, nullptr);

    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);
    BOOST_CHECK(! MinerHasGoodReputation(minerid_db, miner1IdPubKey2));
    BOOST_CHECK(! UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second.mReputation.mVoid);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second.mReputation.mM,
        GlobalConfig::GetConfig().GetMinerIdReputationM());

    // Mine enough blocks that miner 1 has a good reputation
    for(int i = 0; i < 2; ++i)
    {
        UniValue baseDocument { CreateValidCoinbaseDocument(
            miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    }
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 5U);
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, miner1IdPubKey2));

    // Check if GetMinerCoinbaseDocInfo function returns expected results.
    const auto& result { GetMinerCoinbaseDocInfo(minerid_db, miner1IdPubKey2) };
    const CoinbaseDocument& coinbaseDoc { result->first };
    BOOST_CHECK_EQUAL(coinbaseDoc.GetMinerId(), HexStr(miner1IdPubKey2));
    BOOST_CHECK_EQUAL(coinbaseDoc.GetPrevMinerId(), HexStr(miner1IdPubKey2));
    const std::string& minerIdStatus { result->second };
    BOOST_CHECK_EQUAL(minerIdStatus, "CURRENT");

    // Send bad block to void miner 1 reputation
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2, MinerIDOrInfo::MINER_INFO, true);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 5U);
    BOOST_CHECK(! MinerHasGoodReputation(minerid_db, miner1IdPubKey2));
    auto minerEntry { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second };
    BOOST_CHECK(minerEntry.mReputation.mVoid);
    BOOST_CHECK_EQUAL(minerEntry.mReputation.mVoidingId->GetHash(), miner1IdPubKey2.GetHash());

    // Revoke ID that produced bad block and rotate to new clean ID
    CKey key3 {};
    key3.MakeNewKey(true);
    auto savedFields { miner1V3Fields };
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey2 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, key3);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    miner1V3Fields = savedFields;

    // Check miner reputation is no longer void
    minerEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    BOOST_CHECK(! minerEntry.mReputation.mVoid);
    BOOST_CHECK(! minerEntry.mReputation.mVoidingId);

    // Check that M for this miner has been increased
    uint32_t expectedNewM { static_cast<uint32_t>(GlobalConfig::GetConfig().GetMinerIdReputationM() *
        GlobalConfig::GetConfig().GetMinerIdReputationMScale()) };
    BOOST_CHECK_EQUAL(minerEntry.mReputation.mM, expectedNewM);

    // Check that even though they have unvoided their reputation, they no longer meet the M/N criteria
    BOOST_CHECK(! MinerHasGoodReputation(minerid_db, key3.GetPubKey()));

    // Mine enough blocks to take them up to M/N
    for(int i = 0; i < 4; ++i)
    {
        UniValue baseDocument { CreateValidCoinbaseDocument(
            key3, chainActive.Height() + 1, HexStr(key3.GetPubKey()), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields) };
        CreateAndProcessBlock({}, baseDocument, key3);
    }
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 7U);

    // Check they again have a good reputation
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, key3.GetPubKey()));

    // Move time on 24 hours & check M for this miner has decreased by 1
    SetMockTime( GetTime() + (60 * 60 * 24));
    minerid_db.Prune();
    minerEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    expectedNewM -= 1;
    BOOST_CHECK_EQUAL(minerEntry.mReputation.mM, expectedNewM);
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, key3.GetPubKey()));

    // Move time on 12 hours & check M for this miner hasn't changed
    SetMockTime( GetTime() + (60 * 60 * 12));
    minerid_db.Prune();
    minerEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    BOOST_CHECK_EQUAL(minerEntry.mReputation.mM, expectedNewM);

    // One more 12 hours and miner has reduced back to the configured M
    SetMockTime( GetTime() + (60 * 60 * 12));
    minerid_db.Prune();
    minerEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    expectedNewM -= 1;
    BOOST_CHECK_EQUAL(minerEntry.mReputation.mM, expectedNewM);
    BOOST_CHECK_EQUAL(minerEntry.mReputation.mM, GlobalConfig::GetConfig().GetMinerIdReputationM());
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, key3.GetPubKey()));

    // Check another 24 hours doesn't reduce the M further
    SetMockTime( GetTime() + (60 * 60 * 24));
    minerid_db.Prune();
    minerEntry = UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1").second;
    BOOST_CHECK_EQUAL(minerEntry.mReputation.mM, GlobalConfig::GetConfig().GetMinerIdReputationM());
    BOOST_CHECK(MinerHasGoodReputation(minerid_db, key3.GetPubKey()));
}

// Test full revocation
BOOST_FIXTURE_TEST_CASE(FullRevocation, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);

    auto savedV3Fields { miner1V3Fields };

    // Perform another key roll so we have 3 IDs for Miner1. Seeing this key roll the miner decides to
    // fully revoke their current ID chain.
    CKey key3 {};
    key3.MakeNewKey(true);
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(key3.GetPubKey()), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, key3);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRotated(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
        BOOST_CHECK_EQUAL(miner1Key1Details.mNextMinerId->GetHash(), miner1IdPubKey2.GetHash());
    }

    // Check a full revocation attempt using a wrong (completely unknown) revocation key is rejected
    CKey wrongRevocationKey {};
    wrongRevocationKey.MakeNewKey(true);
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    miner1V3Fields.revocationKey = wrongRevocationKey;
    miner1V3Fields.revocationPubKey = wrongRevocationKey.GetPubKey();
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
    }
    miner1V3Fields = savedV3Fields;

    // Check a full revocation attempt using wrong (completely unknown) revocation and previous revocation keys is rejected
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    miner1V3Fields.revocationKey = wrongRevocationKey;
    miner1V3Fields.revocationPubKey = wrongRevocationKey.GetPubKey();
    miner1V3Fields.prevRevocationKey = wrongRevocationKey;
    miner1V3Fields.prevRevocationPubKey = wrongRevocationKey.GetPubKey();
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
    }
    miner1V3Fields = savedV3Fields;

    // Check a full revocation attempt incorrectly signed with the wrong revocation key is rejected
    CKey badKeyForSigning {};
    badKeyForSigning.MakeNewKey(true);
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    miner1V3Fields.revocationKey = badKeyForSigning;
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
    }
    miner1V3Fields = savedV3Fields;

    // Check a full revocation attempt incorrectly signed with the wrong miner ID key is rejected
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = badKeyForSigning;
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
    }
    miner1V3Fields = savedV3Fields;

    // Check we disallow revocation key rolling during full revocation
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    miner1V3Fields.prevRevocationPubKey = miner1V3Fields.revocationPubKey;
    miner1V3Fields.prevRevocationKey = miner1V3Fields.revocationKey;
    CKey newRevocationKey {};
    newRevocationKey.MakeNewKey(true);
    miner1V3Fields.revocationKey = newRevocationKey;
    miner1V3Fields.revocationPubKey = newRevocationKey.GetPubKey();
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsCurrent(miner1Key3Details));
    }
    miner1V3Fields = savedV3Fields;

    // Check correct full revocation
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 0U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key3Details));
    }
    miner1V3Fields = savedV3Fields;

    // Check we handle a duplicate full revocation
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 0U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 3U);
    {
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        const auto& miner1Key3Details { UnitTestAccess::GetMinerIdEntry(minerid_db, key3.GetPubKey().GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key3Details));
    }
    miner1V3Fields = savedV3Fields;
}

// Test full revocation across a fork
BOOST_FIXTURE_TEST_CASE(FullRevocationFork, SetupMinerIDChain)
{
    // Create global miner ID database into which updates will be applied
    MakeGlobalMinerIdDb makedb {};
    MinerIdDatabase& minerid_db { *g_minerIDs };
    UnitTestAccess::WaitForSync(minerid_db);

    // Check initial state
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 3U);

    CBlockIndex* oldTip { chainActive.Tip() };

    // Extend the fork to force a reorg
    CreateAndProcessBlock(forkBlockId, {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerIds(minerid_db), 5U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumMinerUUIds(minerid_db), 4U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 3U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);

    // Perform a full revocation for Miner1 on the fork
    miner1V3Fields.revocationMessage = CoinbaseDocument::RevocationMessage { miner1IdPubKey1 };
    miner1V3Fields.revocationCurrentMinerIdKey = miner1IdKey2;
    UniValue baseDocument { CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields) };
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 0U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
    }

    // Reorg back to the main chain
    CBlock lastBlock { CreateAndProcessBlock(oldTip->GetBlockHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true) };
    lastBlock = CreateAndProcessBlock(lastBlock.GetHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
    lastBlock = CreateAndProcessBlock(lastBlock.GetHash(), {}, {}, MinerIDOrInfo::MINER_INFO, false, true);
    BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), lastBlock.GetHash());
    BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 0U);
    BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);
    {
        // Check state of all miner 1's IDs
        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
    }

    // Reapply revocation on the main chain
    baseDocument = CreateValidCoinbaseDocument(
        miner1IdKey2, chainActive.Height() + 1, HexStr(miner1IdPubKey2), HexStr(miner1IdPubKey2), "Miner1", {}, miner1V3Fields);
    CreateAndProcessBlock({}, baseDocument, miner1IdKey2);

    {
        // Check nodes that have seen both forks have the correct view
        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 0U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);
        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), miner1Details.second.mLastBlock);

        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
    }

    {
        // Check nodes that have only seen the main chain have the correct view
        minerid_db.TriggerSync(true, true);
        UnitTestAccess::WaitForSync(minerid_db);

        BOOST_CHECK_EQUAL(UnitTestAccess::GetNumRecentBlocksForMinerByName(minerid_db, mapBlockIndex, "Miner1"), 0U);
        BOOST_CHECK_EQUAL(UnitTestAccess::GetMinerIdsForMinerByName(minerid_db, mapBlockIndex, "Miner1").size(), 2U);
        const auto& miner1Details { UnitTestAccess::GetMinerUUIdEntryByName(minerid_db, mapBlockIndex, "Miner1") };
        BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockHash(), miner1Details.second.mLastBlock);

        const auto& miner1Key1Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey1.GetHash()) };
        const auto& miner1Key2Details { UnitTestAccess::GetMinerIdEntry(minerid_db, miner1IdPubKey2.GetHash()) };
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key1Details));
        BOOST_CHECK(UnitTestAccess::MinerIdIsRevoked(miner1Key2Details));
    }
}

BOOST_AUTO_TEST_SUITE_END()

