// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "amount.h"
#include "base58.h"
#include "config.h"
#include "dstencode.h"
#include "keystore.h"
#include "miner_id/dataref_index.h"
#include "miner_id/miner_id_db.h"
#include "miner_id/miner_info_error.h"
#include "miner_id/miner_info_tracker.h"
#include "miner_id/revokemid.h"
#include "miner_id/miner_info.h"
#include "netmessagemaker.h"
#include "rpc/server.h"
#include "script/instruction_iterator.h"
#include "script/script.h"
#include "script/script_num.h"
#include "script/sign.h"
#include "txdb.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <univalue.h>
#include <variant>
#include <vector>

namespace mining {

static const fs::path fundingPath = fs::path("miner_id") / "Funding";
static const std::string fundingKeyFile = ".minerinfotxsigningkey.dat";
static const std::string fundingSeedFile = "minerinfotxfunding.dat";



template<typename InitFunc, typename ExitFunc>
struct MinerInfoClass_ScopeExit {
	InitFunc initFunc;
	ExitFunc exitFunc;
	bool success = false;
    MinerInfoClass_ScopeExit (InitFunc initFunc, ExitFunc exitFunc)
	    : initFunc(initFunc)
	    , exitFunc(exitFunc)
    {
        initFunc();
    }

    void good() {
        success = true;
    }
    ~MinerInfoClass_ScopeExit () {
        if (!success)
            exitFunc();
    }
};

static CKey privKeyFromStringBIP32(std::string const & strkey)
{
    // parse the BIP32 key and convert it to ECDSA format.
    CKey key;
    CBitcoinExtKey bip32ExtPrivKey {strkey};
    CExtKey newKey = bip32ExtPrivKey.GetKey();
    key.Set(newKey.key.begin(), newKey.key.end(), true);
    return key;
}

auto ReadFileToUniValue (fs::path const & path, std::string filename) -> UniValue {
    auto dir = (GetDataDir() / path);
    auto filepath = dir / filename;

    if (!fs::exists(dir))
        throw std::runtime_error("funding directory does not exist: " + dir.string());

    if (!fs::exists(filepath))
        throw std::runtime_error("funding data file does not exist: " + filepath.string());
    std::ifstream file;
    file.open(filepath.string(), std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Cannot open funding data file: " + filepath.string());

    std::streamsize const size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
        throw std::runtime_error("Cannot read funding data from file: " + filepath.string());
    file.close();

    UniValue uv;
    uv.read(&(buffer.front()), size);
    return uv;
};

std::optional<CoinWithScript> GetSpendableCoin (COutPoint const & outpoint) {
    LOCK(cs_main);
    CoinsDBView const tipView{ *pcoinsTip };
    CCoinsViewMemPool const mempoolView {tipView, mempool};
    CCoinsViewCache const view{mempoolView};
    auto coin = view.GetCoinWithScript(outpoint);
    if (coin.has_value() && !coin->IsSpent())
        return coin;
    return std::nullopt;
};

void WriteUniValueToFile (fs::path const & path, std::string filename, UniValue const & uv) {
    auto dir = (GetDataDir() / path);
    auto filepath = dir / filename;

    if (!fs::exists(dir))
        fs::create_directory(dir);
    std::ofstream file;
    file.open(filepath.string(), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        throw std::runtime_error("Cannot open and truncate funding data file: " + filepath.string());

    file << uv.write(1,3);
}

struct CurrentMinerInfoTx {
    TxId txid;
    int32_t height;
};

static std::optional<CurrentMinerInfoTx> currentMinerInfoTx;
static std::mutex mut;

class DatarefFunding {
    class FundingKey
    {
        CKey privKey{};
        CTxDestination destination{};
    public:
        FundingKey(std::string const & privKey, std::string const & destination, Config const & config)
                : privKey(privKeyFromStringBIP32(privKey))
                , destination(DecodeDestination(destination, config.GetChainParams()))
        {
        }
        [[nodiscard]] CKey const & getPrivKey() const {return privKey;};
        [[nodiscard]]CTxDestination const & getDestination() const {return destination;};
    };
    COutPoint fundingSeed; // Funding for the first minerinfo-txn of this miner
    FundingKey fundingKey; // Keys needed to spend the funding seed and also the minerinfo-txns
public:
    DatarefFunding (COutPoint const & fundingSeed, std::string const & privateKey, std::string const & destination, Config const & config)
            : fundingSeed{fundingSeed}
            , fundingKey{FundingKey(privateKey, destination, config)}
    {
    }

    std::pair<COutPoint, COutPoint> FundAndSignMinerInfoTx (const Config &config, CMutableTransaction & mtx, int32_t blockheight)
    {
        try {
            // A potential new funding seed has preceedence.
            std::optional<COutPoint> fundingOutPoint;
            std::optional<CoinWithScript> coin;
            if (!mempool.IsSpent(fundingSeed))
                coin = GetSpendableCoin(fundingSeed);
            if (coin.has_value())
                fundingOutPoint = fundingSeed;

            if (!fundingOutPoint) {

                // First we check if we have something to spend in the mempool tracking.
                // Funding transactions, that have been re-mined via a reorg will not have been relayed, hence,
                // we know the funding transaction is either tracked in the mempool, or in a block, but never
                // untracked in the mempool

                std::optional<COutPoint> fund = g_MempoolDatarefTracker->funds_back();
                if (fund) {
                    fundingOutPoint = fund;
                    coin = GetSpendableCoin(fundingOutPoint.value());
                } else {
                    auto hasSpendableFunds = [] (const COutPoint& outp) -> std::optional<CoinWithScript> {
                        return GetSpendableCoin(outp);
                    };

                    // find our minerinfo/dataref funding tx if exists.
                    std::optional<std::pair<COutPoint, std::optional<CoinWithScript>>> output =
                            g_BlockDatarefTracker->find_fund(blockheight, hasSpendableFunds);
                    if (output && output->second.has_value() ) {
                        fundingOutPoint = output->first;
                        coin = std::move(output->second);
                    }
                }
            }

            if (!fundingOutPoint)
                throw std::runtime_error("Cannot find spendable funding transaction");
            if (!coin)
                throw std::runtime_error("Cannot find funding UTXO's");

            const CScript &prevPubKey = coin->GetTxOut().scriptPubKey;
            const Amount fundingAmount = coin->GetTxOut().nValue;

            // sign the new mininginfo-txn with the funding keys
            SignatureData sigdata;
            CBasicKeyStore keystore;
            SigHashType sigHash;
            CScript const & scriptPubKey = GetScriptForDestination(fundingKey.getDestination()); //p2pkh script

            mtx.vout.push_back(CTxOut {Amount{fundingAmount}, scriptPubKey});
            mtx.vin.emplace_back(*fundingOutPoint, CTxIn::SEQUENCE_FINAL);

            keystore.AddKeyPubKey(fundingKey.getPrivKey(), fundingKey.getPrivKey().GetPubKey());
            ProduceSignature(config, true, MutableTransactionSignatureCreator(
                                     &keystore, &mtx, 0, fundingAmount, sigHash.withForkId()),
                             true, true, prevPubKey, sigdata);
            UpdateTransaction(mtx, 0, sigdata); // funding transactions only have one input
            COutPoint newOutPoint {mtx.GetId(), static_cast<uint32_t>(mtx.vout.size() - 1)};
            return {newOutPoint, *fundingOutPoint};
        } catch (UniValue const & e) {
            throw std::runtime_error("Could not fund minerinfo transaction: " + e["message"].get_str());
        } catch (std::exception const & e) {
            throw std::runtime_error("Could not fund minerinfo transaction: " + std::string(e.what()));
        }
    }
};

static DatarefFunding CreateDatarefFundingFromFile (Config const & config, const fs::path & path, std::string const & keyFile, std::string const & seedFile)
{
    try {
        // read funding info from json formatted files
        UniValue fundingSeed = ReadFileToUniValue (path, seedFile);
        UniValue fundingKey = ReadFileToUniValue (path, keyFile);

        RPCTypeCheckObj(
                fundingKey,
                {
                        {"fundingKey", UniValueType(UniValue::VOBJ )},
                }, false, false);
        RPCTypeCheckObj(
                fundingKey["fundingKey"],
                {
                        {"privateBIP32", UniValueType(UniValue::VSTR )},
                }, false, false);


        // check file format
        RPCTypeCheckObj(
                fundingSeed,
                {
                        {"fundingDestination", UniValueType(UniValue::VOBJ )},
                        {"firstFundingOutpoint", UniValueType(UniValue::VOBJ )},
                }, false, false);
        RPCTypeCheckObj(
                fundingSeed["fundingDestination"],
                {
                        {"addressBase58", UniValueType(UniValue::VSTR )}
                }, false, false);
        RPCTypeCheckObj(
                fundingSeed["firstFundingOutpoint"],
                {
                        {"txid", UniValueType(UniValue::VSTR )},
                        {"n", UniValueType(UniValue::VNUM )},
                }, false, false);

        // Create and return the DatarefFunding object
        UniValue const keys = fundingKey["fundingKey"];
        UniValue const destination = fundingSeed["fundingDestination"];
        UniValue const outpoint = fundingSeed["firstFundingOutpoint"];

        std::string const sPrivKey = keys["privateBIP32"].get_str();
        std::string const sDestination = destination["addressBase58"].get_str();
        std::string const sFundingSeedId = outpoint["txid"].get_str();
        uint32_t fundingSeedIndex = outpoint["n"].get_int();

        auto fundingOutPoint = COutPoint {uint256S(sFundingSeedId), fundingSeedIndex};
        return {fundingOutPoint, sPrivKey, sDestination, config};

    } catch (UniValue const & e) {
        throw std::runtime_error(strprintf("Could not fund minerinfo transaction: %s", e["message"].get_str()));
    } catch (std::exception const & e) {
        throw std::runtime_error(strprintf("Could not fund minerinfo transaction: %s", e.what()));
    }
}


std::string CreateDatarefTx(const Config& config, const std::vector<CScript>& scriptPubKeys)
{
    // We need to lock because we need to ensure there is only
    // one such minerid info document transaction
    std::lock_guard lock{mut};

    auto blockHeight = chainActive.Height() + 1;

    // create and fund minerinfo txn
    CMutableTransaction mtx;
    for(const CScript& script: scriptPubKeys)
    {
        if(!IsMinerInfo(script))
            throw std::runtime_error("invalid miner info script");

        const auto var_data_obj = VerifyDataScript(script);
        if(std::holds_alternative<miner_info_error>(var_data_obj))
        {
            const auto err{std::get<miner_info_error>(var_data_obj)};
            throw std::runtime_error(enum_cast<std::string>(err));
        }

        mtx.vout.push_back(CTxOut{Amount{0}, script});
    }        

    auto funding = CreateDatarefFundingFromFile(config, fundingPath, fundingKeyFile, fundingSeedFile);
    auto newfund_and_previous = funding.FundAndSignMinerInfoTx (config, mtx, blockHeight);

    std::string const mtxhex {EncodeHexTx(CTransaction(mtx))};
    UniValue minerinfotx_args(UniValue::VARR);
    minerinfotx_args.push_back(mtxhex);
    minerinfotx_args.push_back(UniValue(false));
    minerinfotx_args.push_back(UniValue(true)); // do not check, we want to allow no fees
    TxId const txid =  mtx.GetId();

    auto initFunc = [&newfund_and_previous]() {
        g_MempoolDatarefTracker->funds_append(newfund_and_previous.first);
    };
    auto exitFunc = []() {
        g_MempoolDatarefTracker->funds_pop_back();
    };

    {
	MinerInfoClass_ScopeExit raii(initFunc, exitFunc);
        UniValue const r = CallRPC("sendrawtransaction", minerinfotx_args);
        LogPrint(BCLog::MINERID, "minerinfotx tracker, sent dataref txn %s to mempool at height %d. Spending %s, New funding outpoint: %s\n",
                 txid.ToString(), blockHeight, newfund_and_previous.second.ToString(), newfund_and_previous.first.ToString());

        if (r.exists("error")) {
            if (!r["error"].isNull()) {
                throw std::runtime_error(strprintf("Could not create minerinfo transaction. %s", r["error"]["message"].get_str()));
            }
        }

        // check that no new block has been added to the tip in the meantime.
        int32_t blockHeight2 = chainActive.Height() + 1;
        if (blockHeight != blockHeight2) {
            LogPrint(BCLog::MINERID, strprintf("A block was added to the tip while a dataref-tx %s was created. Currrent height: %ld\n", txid.ToString(), chainActive.Height()));
        }
	raii.good();
    }

    std::string txid_as_string = txid.ToString();
    LogPrint(BCLog::MINERID, "A dataref-txn %s has been created at height %d\n", txid_as_string, blockHeight);
    return txid_as_string;
}


std::string CreateReplaceMinerinfotx(const Config& config, const CScript& scriptPubKey, bool overridetx)
{
    // We need to lock because we need to ensure there is only
    // one such minerid info document transaction
    std::lock_guard lock{mut};

    auto blockHeight = chainActive.Height() + 1;
    auto prevBlockHash = chainActive.Tip()->GetBlockHash();

    auto GetOrRemoveCachedMinerInfoTx = [](int32_t blockHeight, uint256 const & prevBlockHash, bool overridetx, CScript const & scriptPubKey) -> CTransactionRef {
        try {
            if (currentMinerInfoTx && currentMinerInfoTx->height == blockHeight) {
                CTransactionRef tx = mempool.Get(currentMinerInfoTx->txid);
                if (!tx)
                    return nullptr;

                // if we do not override, we return what we have
                if (!overridetx)
                    return  tx;

                // if we do override with no change at all we are also done
                if(tx->vout[0].scriptPubKey == scriptPubKey)
                    return  tx;

                // If we get here, we override, hence we must remove the previously created tx
                TxId toRemove = tx->GetId();
                LogPrint(BCLog::MINERID, "minerinfotx tracker, scheduled removal of minerinfo txn %s because attempting to override\n", toRemove.ToString());
                tx.reset();
                CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::REMOVE_TXN) };
                mempool.RemoveTxAndDescendants(toRemove, changeSet);
                changeSet->apply();
                currentMinerInfoTx = std::nullopt;
                g_MempoolDatarefTracker->funds_pop_back();
                return nullptr;
            }
        } catch (std::exception const & e) {
            throw std::runtime_error(strprintf("rpc CreateReplaceMinerinfotx - minerinfo tx tracking error: %s", e.what()));
        } catch (...) {
            throw std::runtime_error("rpc CreateReplaceMinerinfotx - unknown minerinfo tx tracking error");
        }
        return nullptr;
    };
    
    // If such a transaction already exists in the mempool, then it is the one we need and return
    // unless we want to override
    CTransactionRef trackedTransaction = GetOrRemoveCachedMinerInfoTx (blockHeight, prevBlockHash, overridetx, scriptPubKey);
    if (trackedTransaction)
        return trackedTransaction->GetId().ToString();

    // check the height in the minerinfo document
    if (true)
    {
        auto ExtractMinerInfoDoc = [](CScript const & scriptPubKey) -> miner_info_doc {

            if (!IsMinerInfo(scriptPubKey))
                throw std::runtime_error ("Calling ParseMinerInfoScript on ill formed script.");
            const auto var_doc_sig = ParseMinerInfoScript(scriptPubKey);
            if(std::holds_alternative<miner_info_error>(var_doc_sig))
                throw std::runtime_error(strprintf(
                        "failed to extract miner info document from scriptPubKey: %s",
                        std::get<miner_info_error>(var_doc_sig)));

            const auto [sv, doc, sig] = std::get<mi_doc_sig>(var_doc_sig);
            return doc;
        };

        // Extract information from the Miner Info document which is embedded in the data part of the scriptPubKey
        miner_info_doc doc = ExtractMinerInfoDoc(scriptPubKey);

        if (doc.GetHeight() != blockHeight) {
            throw std::runtime_error("Block height must be the active chain height plus 1");
        }
    }

   // create and fund minerinfo txn
    CMutableTransaction mtx;
    mtx.vout.push_back(CTxOut{Amount{0}, scriptPubKey});

    auto funding = CreateDatarefFundingFromFile(config, fundingPath, fundingKeyFile, fundingSeedFile);
    auto newfund_and_previous = funding.FundAndSignMinerInfoTx (config, mtx, blockHeight);

    std::string const mtxhex {EncodeHexTx(CTransaction(mtx))};
    UniValue minerinfotx_args(UniValue::VARR);
    minerinfotx_args.push_back(mtxhex);
    minerinfotx_args.push_back(UniValue(false));
    minerinfotx_args.push_back(UniValue(true)); // do not check, we want to allow no fees
    TxId const txid =  mtx.GetId();

    currentMinerInfoTx = {newfund_and_previous.first.GetTxId(), blockHeight};
    auto initFunc = [&newfund_and_previous]() {
        g_MempoolDatarefTracker->funds_append(newfund_and_previous.first);
    };
    auto exitFunc = []() {
        g_MempoolDatarefTracker->funds_pop_back();
    };

    {
	MinerInfoClass_ScopeExit raii(initFunc, exitFunc);
        UniValue const r = CallRPC("sendrawtransaction", minerinfotx_args);
        LogPrint(BCLog::MINERID, "minerinfotx tracker, sent minerinfo txn %s to mempool at height %d. Spending %s, New funding outpoint: %s\n",
             txid.ToString(), blockHeight, newfund_and_previous.second.ToString(), newfund_and_previous.first.ToString());


        if (r.exists("error")) {
            if (!r["error"].isNull()) {
                throw std::runtime_error(strprintf("Could not create minerinfo transaction. %s", r["error"]["message"].get_str()));
            }
        }

        // check that no new block has been added to the tip in the meantime.
        int32_t blockHeight2 = chainActive.Height() + 1;
        if (blockHeight != blockHeight2) {
            LogPrint(BCLog::MINERID, strprintf("A block was added to the tip while a minerinfo-tx %s was created. Currrent height: %ld\n", txid.ToString(), chainActive.Height()));
        }
	raii.good();
    }

    std::string txid_as_string = txid.ToString();
    LogPrint(BCLog::MINERID, "A mineridinfo-txn %s has been created at height %d\n", txid_as_string, blockHeight);
    return txid_as_string;
}

static UniValue createminerinfotx(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.empty() || request.params.size() > 1) {
        throw std::runtime_error(
                "createminerinfotx \"scriptPubKey\"\n"
                "\nCreate a transaction with a miner info document and return its transaction id\n"
                "\nIf such a miner info document exists already, then return it's transaction id instead.\n"
                "\nArguments:\n"
                "1. \"scriptPubKey:\" (hex string mandatory) OP_FALSE OP_RETURN 0x601DFACE 0x00 minerinfo  \n"
                "where minerinfo contains the following json data in hex encoding"
                "{\n"
                "  \"MinerInfoDoc\":hex,      The minerid document in hex representation\n"
                "  \"MinerInfoDocSig\":hex    (hex string, required) The sequence\n"
                "}\n"
                "\nResult: a hex encoded transaction id\n"
                "\nExamples:\n" +
                HelpExampleCli("createminerinfotx", "\"006a04601dface01004dba027b22...\"") +
                HelpExampleRpc("createminerinfotx", "\"006a04601dface01004dba027b22...\""));
    }

    RPCTypeCheck(request.params,{UniValue::VSTR}, false);
    std::string const scriptPubKeyHex = request.params[0].get_str();
    std::vector<uint8_t> script = ParseHex(scriptPubKeyHex);
    const auto scriptPubKey = CScript {script.begin(), script.end()};

    bool overridetx = false;
    return CreateReplaceMinerinfotx(config, scriptPubKey, overridetx);
}

static UniValue replaceminerinfotx(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.empty() || request.params.size() > 1) {
        throw std::runtime_error(
                "replaceminerinfotx \"scriptPubKey\"\n"
                "\nCreate or replace a transaction with a miner info document and return it's transaction id\n"
                "\nArguments:\n"
                "1. \"scriptPubKey:\" (hex string mandatory) OP_FALSE OP_RETURN 0x601DFACE 0x00 minerinfo  \n"
                "where minerinfo contains the following json data in hex encoding"
                "{\n"
                "  \"MinerInfoDoc\":hex,      The minerid document in hex representation\n"
                "  \"MinerInfoDocSig\":hex    (hex string, required) The sequence\n"
                "}\n"
                "\nResult: a hex encoded transaction id\n"
                "\nExamples:\n" +
                HelpExampleCli("replaceminerinfotx", "\"006a04601dface01004dba027b22...\"") +
                HelpExampleRpc("replaceminerinfotx", "\"006a04601dface01004dba027b22...\""));
    }

    RPCTypeCheck(request.params,{UniValue::VSTR}, false);
    std::string const scriptPubKeyHex = request.params[0].get_str();
    std::vector<uint8_t> script = ParseHex(scriptPubKeyHex);
    const auto scriptPubKey = CScript {script.begin(), script.end()};

    bool overridetx = true;
    return CreateReplaceMinerinfotx(config, scriptPubKey, overridetx);
}

static UniValue createdatareftx(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
                "createdatareftx \"[scriptPubKey,...]\"\n"
                "\nCreate a transaction with dataref content\n"
                "\nArguments:\n"
                "1. \"scriptPubKey...:\" (array of hex strings)\n"
                "\nResult: a hex encoded transaction id\n"
                "\nExamples:\n" +
                HelpExampleCli("createdatareftx", R"([\"006a04601dface01004dba027b22...\", ...])") +
                HelpExampleRpc("createdatareftx", R"([\"006a04601dface01004dba027b22...\", ...])"));
    }

    std::vector<CScript> scriptPubKeys;
    RPCTypeCheck(request.params,{UniValue::VARR}, false);

    for (const auto& script: request.params[0].get_array().getValues()) {
        const std::string& scriptPubKeyHex = script.get_str();
        std::vector<uint8_t> script_binary = ParseHex(scriptPubKeyHex);
        const auto scriptPubKey = CScript {script_binary.begin(), script_binary.end()};
        scriptPubKeys.push_back(scriptPubKey);
    }

    return CreateDatarefTx(config, scriptPubKeys);
}


static UniValue getminerinfotxid(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || !request.params.empty()) {
        throw std::runtime_error(
                "getminerinfotxid  \n"
                "\nreturn the minerinfotx for the current block being built.\n"
                "\nResult: a hex encoded transaction id\n"
                "\nExamples:\n" +
                HelpExampleCli("getminerinfotxid","") +
                HelpExampleRpc("getminerinfotxid",""));
    }

    std::lock_guard lock{mut};
    auto blockHeight = chainActive.Height() + 1;

    if (currentMinerInfoTx) {
        const auto& c = currentMinerInfoTx.value();
        if(c.height == blockHeight) {
            CTransactionRef tx = mempool.Get(c.txid);
            if (tx)
                return {c.txid.ToString()};
        }
    }
    return {UniValue::VNULL};
}

static UniValue makeminerinfotxsigningkey(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || !request.params.empty()) {
        throw std::runtime_error(
                "makeminerinfotxsigningkey  \n"
                "\ncreates a private BIP32 Key and stores it in ./miner_id/Funding/.minerinfotxsigningkey.dat\n"
                "\nExamples:\n" +
                HelpExampleCli("makeminerinfotxsigningkey","") +
                HelpExampleRpc("makeminerinfotxsigningkey",""));
    }

    std::lock_guard lock{mut};

    // store the key
    CKey privKey;
    bool compressed = true;

    if (gArgs.GetBoolArg("-regtest", false)) {
        std::vector<uint8_t> vchKey =
                {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

        privKey.Set(vchKey.begin(), vchKey.end(), compressed);
    } else {
        privKey.MakeNewKey(compressed);
    }

    CExtKey masterKey {};
    masterKey.SetMaster(privKey.begin(), privKey.size());
    CBitcoinExtKey bip32key;
    bip32key.SetKey(masterKey);

    privKey = bip32key.GetKey().key;
    CPubKey pubKey = privKey.GetPubKey();

    UniValue uniBip32 (UniValue::VOBJ);
    uniBip32.pushKV("privateBIP32", bip32key.ToString());

    UniValue uniKey(UniValue::VOBJ);
    uniKey.pushKV("fundingKey", uniBip32);

    WriteUniValueToFile(fundingPath, fundingKeyFile, uniKey);

    // store the address
    CTxDestination destination = pubKey.GetID();

    UniValue uniBase58(UniValue::VOBJ);
    std::string base58 = EncodeDestination(destination, config);
    uniBase58.pushKV("addressBase58", base58);

    UniValue uniDestination(UniValue::VOBJ);
    uniDestination.pushKV("fundingDestination", uniBase58);

    WriteUniValueToFile(fundingPath, fundingSeedFile, uniDestination);

    return {};
}

static UniValue getminerinfotxfundingaddress(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || !request.params.empty()) {
        throw std::runtime_error(
                "getminerinfotxfundingaddress  \n"
                "Get the address that will fund the miner info transaction.\n"
                "\nExamples:\n" +
                HelpExampleCli("getminerinfotxfundingaddress","") +
                HelpExampleRpc("getminerinfotxfundingaddress",""));
    }

    std::lock_guard lock{mut};

    UniValue destination = ReadFileToUniValue (fundingPath, fundingSeedFile);
    RPCTypeCheck(destination,{UniValue::VOBJ}, false);
    RPCTypeCheckObj(
            destination,
            {
                    {"fundingDestination", UniValueType(UniValue::VOBJ )},
            }, false, false);
    RPCTypeCheckObj(
            destination["fundingDestination"],
            {
                    {"addressBase58", UniValueType(UniValue::VSTR )},
            }, false, false);

    return destination["fundingDestination"]["addressBase58"].get_str();
}

static UniValue setminerinfotxfundingoutpoint(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.size() != 2) {
        throw std::runtime_error(
                "setminerinfotxfundingoutpoint \"txid\" \"n\"\n"
                "\nConfigure the node to use the miner-info funding outpoint\n"
                "\nArguments:\n"
                "1. \"txid:\" (hex string mandatory) a transaction that can be spend using the \n"
                "key created by rpc function makeminerinfotxspendingkey\n"
                "2. \"n:\" (int) the output to spend \n"
                "\nExamples:\n" +
                HelpExampleCli("setminerinfotxfundingoutpoint", "\"txid\" n") +
                HelpExampleRpc("setminerinfotxfundingoutpoint", "\"txid\", n"));
    }

    std::lock_guard lock{mut};

    // Read rpc parameters
    RPCTypeCheck(request.params,{UniValue::VSTR, UniValue::VNUM}, false);
    auto txid = request.params[0].get_str();
    auto n = request.params[1].get_int();
    UniValue outPoint(UniValue::VOBJ);
    outPoint.pushKV("txid", txid);
    outPoint.pushKV("n", n);

    // Read funding configuration file and set or replace the funding output
    UniValue fundingSeed = ReadFileToUniValue (fundingPath, fundingSeedFile);

    UniValue result {UniValue::VOBJ};
    result.pushKV ("fundingDestination", fundingSeed["fundingDestination"]);
    result.pushKV ("firstFundingOutpoint", outPoint);
    WriteUniValueToFile(fundingPath, fundingSeedFile, result);
    return {};
}

UniValue rebuildminerids(const Config& config, const JSONRPCRequest& request)
{
    if(request.fHelp || request.params.size() > 1)
    {
        throw std::runtime_error(
            "rebuildminerids ( fullrebuild )\n"
            "\nForce a rebuild (in the background) of the miner ID database and synchronise it to the blockchain.\n"
            "\nArguments:\n"
            "1. fullrebuild (boolean, optional, default=false) True forces a full rebuild starting from the Genesis block, "
            "False does a much quicker rebuild only scanning as many blocks back as we think we need to determine "
            "the miners reputations.\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("rebuildminerids", "true") +
            HelpExampleRpc("rebuildminerids", "true"));
    }

    bool fullRebuild {false};
    if(request.params.size() == 1)
    {
        fullRebuild = request.params[0].get_bool();
    }

    // Check we have a miner ID database to rebuild
    if(g_minerIDs)
    {
        g_minerIDs->TriggerSync(true, fullRebuild);
    }
    else
    {
        throw std::runtime_error("Miner ID database unavailable");
    }

    return true;
}

UniValue revokeminerid(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 1) {
        throw std::runtime_error(
                "revokeminerid \"input\"\n"
                "\nRevoke minerId public key specified in the request and send out the P2P revokemid message to our peers.\n"
                "\nArguments:\n"
                "1. \"input\" (a json-object, mandatory) The payload which defines and certifies minerId to be revoked.\n"
                "  {\n"
                "    \"revocationKey\": xxxx,          (hex-string, mandatory) The current compressed revocationKey public key\n"
                "    \"minerId\": xxxx,                (hex-string, mandatory) The current compressed minerId public key\n"
                "    \"revocationMessage\": {          (object, mandatory)\n"
                "      \"compromised_minerId\": xxxx,  (hex-string, mandatory) The compromised minerId public key to be revoked\n"
                "      },\n"
                "    \"revocationMessageSig\": {       (object, mandatory)\n"
                "      \"sig1\": xxxx,                 (hex-string) The signature created by the revocationKey private key\n"
                "      \"sig2\": xxxx,                 (hex-string) The signature created by the current minerId private key\n"
                "      },\n"
                "  }\n"
                "\nResult:\n"
                "NullUniValue\n"
                "\nExamples:\n" +
                HelpExampleCli("revokeminerid", "\"input\"") +
                HelpExampleRpc("revokeminerid", "\"input\""));
    }
    RPCTypeCheck(request.params, {UniValue::VOBJ});
    UniValue input = request.params[0].get_obj();
    if (input.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter: An empty json object");
    }

    RPCTypeCheckObj(
        input,
        {
            {"revocationKey", UniValueType(UniValue::VSTR)},
            {"minerId", UniValueType(UniValue::VSTR)},
            {"revocationMessage", UniValueType(UniValue::VOBJ)},
            {"revocationMessageSig", UniValueType(UniValue::VOBJ)}
        },
        false, true);
    const CPubKey revocationKey { ParseHex(input["revocationKey"].get_str()) };
    if (!revocationKey.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid revocationKey key!");
    }
    const CPubKey minerId { ParseHex(input["minerId"].get_str()) };
    if (!minerId.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid minerId key!");
    }

    UniValue revMsg = input["revocationMessage"].get_obj();
    RPCTypeCheckObj(
        revMsg,
        {
            {"compromised_minerId", UniValueType(UniValue::VSTR)}
        },
        false, true);
    const CPubKey compromisedMinerId { ParseHex(revMsg["compromised_minerId"].get_str()) };
    if (!compromisedMinerId.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid compromised_minerId key!");
    }

    UniValue revMsgSig = input["revocationMessageSig"].get_obj();
    RPCTypeCheckObj(
        revMsgSig,
        {
            {"sig1", UniValueType(UniValue::VSTR)},
            {"sig2", UniValueType(UniValue::VSTR)}
        },
        false, true);

    uint8_t hash_sha256[CSHA256::OUTPUT_SIZE] {};
    CSHA256()
        .Write(reinterpret_cast<const uint8_t*>(compromisedMinerId.begin()), compromisedMinerId.size())
        .Finalize(hash_sha256);
    const uint256 hash { std::vector<uint8_t> {std::begin(hash_sha256), std::end(hash_sha256)} };
    const std::vector<uint8_t> sig1 { ParseHex(revMsgSig["sig1"].get_str()) };
    if (!revocationKey.Verify(hash, sig1)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sig1 signature!");
    }
    const std::vector<uint8_t> sig2 { ParseHex(revMsgSig["sig2"].get_str()) };
    if (!minerId.Verify(hash, sig2)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sig2 signature!");
    }

    const RevokeMid revokeMid {revocationKey, minerId, compromisedMinerId, sig1, sig2};
    // Pass to miner ID database for processing
    g_minerIDs->ProcessRevokemidMessage(revokeMid);
    // Relay to our peers
    g_connman->ForEachNode([&revokeMid](const CNodePtr& to) {
        const CNetMsgMaker msgMaker(to->GetSendVersion());
        g_connman->PushMessage(to, msgMaker.Make(NetMsgType::REVOKEMID, revokeMid));
    });
    return NullUniValue;
}

static UniValue getmineridinfo(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 1) {
        throw std::runtime_error(
                "getmineridinfo \"minerId\"\n"
                "\nReturn miner ID information.\n"
                "\nArguments:\n"
                "1. \"minerid:\" (hex string mandatory) The requested minerId public key to be checked in the Miner ID DB.\n"
                "\nResult:\n"
                "{                           (json object)\n"
                "    \"minerId\": xxxx,             (string) This miner ID\n"
                "    \"minerIdState\": xxxx,        (string) Whether this miner ID is CURRENT, ROTATED or REVOKED\n"
                "    \"prevMinerId\": xxxx,         (string) The previous miner ID seen for this miner\n"
                "    \"revocationKey\": xxxx,       (string) The current revocation key public key used by this miner\n"
                "    \"prevRevocationKey\": xxxx,   (string) The previous revocation key public key used by this miner\n"
                "}\n"
                "\nExamples:\n" +
                HelpExampleCli("getmineridinfo", "\"xxxx...\"") +
                HelpExampleRpc("getmineridinfo", "\"xxxx...\""));
    }
    if(!g_minerIDs) {
        throw std::runtime_error("Miner ID database unavailable");
    }
    // Read rpc parameters
    RPCTypeCheck(request.params,{UniValue::VSTR}, false);
    const CPubKey& reqMinerId { ParseHex(request.params[0].get_str()) };
    if (!reqMinerId.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid minerid key!");
    }

    const auto& coinbaseDocInfo { GetMinerCoinbaseDocInfo(*g_minerIDs, reqMinerId) };
    UniValue ret(UniValue::VOBJ);
    if (coinbaseDocInfo) {
        const std::pair<CoinbaseDocument, std::string>& obj { *coinbaseDocInfo };
        const CoinbaseDocument& coinbaseDoc { obj.first };
        const std::string& minerIdState { obj.second };
        ret.push_back(Pair("minerId", coinbaseDoc.GetMinerId()));
        ret.push_back(Pair("minerIdState", minerIdState));
        ret.push_back(Pair("prevMinerId", coinbaseDoc.GetPrevMinerId()));
        ret.push_back(Pair("revocationKey", HexStr(coinbaseDoc.GetRevocationKey())));
        ret.push_back(Pair("prevRevocationKey", HexStr(coinbaseDoc.GetPrevRevocationKey())));
    }
    return ret;
}

UniValue dumpminerids(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
    {
        throw std::runtime_error(
            "dumpminerids\n"

            "\nReturns details for all currently known miner IDs"
            "\nResult\n"
            "state {\n"
            "  \"synced\": xxxx,             (boolean) Whether the miner ID database has finished syncing with the blockchain\n"
            "  \"bestblock\": xxxx,          (boolean) The latest block scanned and parsed by the database\n"
            "}\n"
            "miners [\n"
            "  {\n"
            "    \"uuid\": xxxxxx,           (string) UUID for this miner\n"
            "    \"firstblock\": xxxx,       (string) Hash of first block seen for this miner\n"
            "    \"latestblock\": xxxx,      (string) Hash of most recent block seen for this miner\n"
            "    \"numrecentblocks\": xxxx,  (string) An indication of how many of the recent blocks were from this miner\n"
            "    \"reputationvoid\": xxxx,   (boolean) Whether this miner has voided their reputation with us\n"
            "    \"minerids\": [\n"
            "       \"minerid\": xxxx,       (string) This miner id\n"
            "       \"version\": xxxx,       (string) The version number of the miner ID spec this ID follows\n"
            "       \"state\": xxxx,         (string) Whether this miner id is CURRENT, ROTATED or REVOKED\n"
            "       \"creationblock\": xxxx, (string) Hash of block in which this miner id was created\n"
            "    ]\n"
            "  }\n"
            "[\n"
            "\nExamples:\n" +
            HelpExampleCli("dumpminerids", "") +
            HelpExampleRpc("dumpminerids", ""));
    }

    // Check we have a miner ID database to dump
    if(g_minerIDs)
    {
        return g_minerIDs->DumpJSON();
    }
    else
    {
        throw std::runtime_error("Miner ID database unavailable");
    }
}

UniValue datarefindexdump(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
    {
        throw std::runtime_error(
            "datarefindexdump\n"

            "\nReturns details for all currently stored dataRef transactions"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\": xxxx,        (string) ID of this dataRef transaction\n"
            "    \"blockid\": xxxx,     (string) Hash of the block this dataRef transaction was seen and referenced in\n"
            "  }\n"
            "[\n"
            "\nExamples:\n" +
            HelpExampleCli("datarefindexdump", "") +
            HelpExampleRpc("datarefindexdump", ""));
    }

    // Check we have the dataref index database to dump
    if(g_dataRefIndex)
    {
        auto data_access = g_dataRefIndex->CreateLockingAccess();
        return data_access.DumpDataRefTxnsJSON();
    }
    else
    {
        throw std::runtime_error("DataRef transaction database unavailable");
    }
}

UniValue datareftxndelete(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error(
            "datareftxndelete \"txid\"\n"

            "\nDelete the specified dataRef transaction from the dataRef index.\n"
            "\nArguments:\n"
            "1. \"txid\"   (string, required) The ID of the dataRef transaction to delete from the index.\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("datareftxndelete", "\"mytxid\"") +
            HelpExampleRpc("datareftxndelete", "\"mytxid\""));
    }

    // Check we have the dataref index database
    if(g_dataRefIndex)
    {
        const uint256 txid = ParseHashV(request.params[0], "txid");
        auto data_access = g_dataRefIndex->CreateLockingAccess();
        data_access.DeleteDatarefTxn(txid);
    }
    else
    {
        throw std::runtime_error("DataRef transaction database unavailable");
    }

    return NullUniValue;
}

static UniValue getdatareftxid(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || !request.params.empty()) {
        throw std::runtime_error(
                "getdatareftxid  \n"
                "\nreturn the datarefid for the current block being built.\n"
                "\nResult: a hex encoded transaction id\n"
                "\nExamples:\n" +
                HelpExampleCli("getdatareftxid","") +
                HelpExampleRpc("getdatareftxid",""));
    }

    std::lock_guard lock{mut};

    std::optional<COutPoint> p = g_MempoolDatarefTracker->funds_front();

    if (p) {
        if (!(currentMinerInfoTx && p->GetTxId() == currentMinerInfoTx->txid)) {
            CTransactionRef tx = mempool.Get(p->GetTxId());
            if (tx)
                return {p->GetTxId().ToString()};
        }
    }

    return {UniValue::VNULL};
}


} // namespace mining

// clang-format off
static const CRPCCommand commands[] = {
    //  category   name                     actor (function)       okSafeMode
    //  ---------- ------------------------ ---------------------- ----------
    {"minerid", "createminerinfotx",                  mining::createminerinfotx,                  true, {"minerinfo"}},
    {"minerid", "createdatareftx",                    mining::createdatareftx,                    true, {"minerinfo"}},
    {"minerid", "replaceminerinfotx",                 mining::replaceminerinfotx,                 true, {"minerinfo"}},
    {"minerid", "getminerinfotxid",                   mining::getminerinfotxid,                   true, {"minerinfo"}},
    {"minerid", "getdatareftxid",                     mining::getdatareftxid,                     true, {"minerinfo"}},
    {"minerid", "makeminerinfotxsigningkey",          mining::makeminerinfotxsigningkey,          true, {"minerinfo"}},
    {"minerid", "getminerinfotxfundingaddress",       mining::getminerinfotxfundingaddress,       true, {"minerinfo"}},
    {"minerid", "setminerinfotxfundingoutpoint",      mining::setminerinfotxfundingoutpoint,      true, {"minerinfo"}},
    {"minerid", "datarefindexdump",                   mining::datarefindexdump,                   true, {} },
    {"minerid", "datareftxndelete",                   mining::datareftxndelete,                   true, {"txid"} },
    {"minerid", "rebuildminerids",                    mining::rebuildminerids,                    true, {"fullrebuild"} },
    {"minerid", "revokeminerid",                      mining::revokeminerid,                      true, {"input"} },
    {"minerid", "getmineridinfo",                     mining::getmineridinfo,                     true, {"minerid"} },
    {"minerid", "dumpminerids",                       mining::dumpminerids,                       true, {} },
};
// clang-format on

void RegisterMinerIdRPCCommands(CRPCTable &t) {
    for (auto& c: commands)
        t.appendCommand(c.name, &c);
}
