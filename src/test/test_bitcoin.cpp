// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test_bitcoin.h"

#include "chainparams.h"
#include "config.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "crypto/sha256.h"
#include "fs.h"
#include "key.h"
#include "logging.h"
#include "miner_id/miner_info_tracker.h"
#include "mining/factory.h"
#include "mining/journal_builder.h"
#include "net/net_processing.h"
#include "pow.h"
#include "pubkey.h"
#include "random.h"
#include "rpc/mining.h"
#include "rpc/register.h"
#include "rpc/server.h"
#include "script/scriptcache.h"
#include "script/sigcache.h"
#include "taskcancellation.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"

#include "test/testutil.h"
#include "test/mempool_test_access.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <list>
#include <memory>
#include <thread>

using mining::CBlockTemplate;

constexpr auto env_var_name = "TEST_BITCOIN_RANDOM_SEED";

const uint256 insecure_rand_seed = []() {
    auto env = std::getenv(env_var_name);
    auto hash = env ? uint256S(env) : GetRandHash();
    if (env) {
        printf("Global random seed is set by environment: %s\n", hash.GetHex().c_str());
    } else {
        printf("To re-run tests using the same seed, set the following environment variable:\n export %s=%s\n", env_var_name, hash.GetHex().c_str());
    }
    return hash;
}();
FastRandomContext insecure_rand_ctx(insecure_rand_seed);

extern void noui_connect();

void ResetGlobalRandomContext() {
    RandomInit();
    insecure_rand_ctx = FastRandomContext{insecure_rand_seed};
}

BasicTestingSetup::BasicTestingSetup(const std::string& chainName) : testConfig(GlobalConfig::GetModifiableGlobalConfig()) {
    SHA256AutoDetect();
    SetupEnvironment();
    SetupNetworking();
    InitSignatureCache();
    InitScriptExecutionCache();
    ResetGlobalRandomContext();

    // Don't want to write to bitcoind.log file.
    GetLogger().fPrintToDebugLog = false;

    fCheckBlockIndex = true;
    SelectParams(chainName);
    noui_connect();
    testConfig.Reset(); // make sure that we start every test with a clean config
    testConfig.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());
    testConfig.SetBlockScriptValidatorsParams(
        DEFAULT_SCRIPT_CHECK_POOL_SIZE,
        DEFAULT_SCRIPTCHECK_THREADS,
        DEFAULT_TXNCHECK_THREADS,
        DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE);

    // Use a temporary datadir that we don't inadvertently create the default one.
    ClearDatadirCache();
    static FastRandomContext local_rand_ctx{GetRandHash()};
    pathTemp = GetTempPath() / strprintf("test_bitcoin_%lu_%i",
                                         (unsigned long)GetTime(),
                                         (int)(local_rand_ctx.randrange(100000)));
    fs::create_directories(pathTemp);
    gArgs.ForceSetArg("-datadir", pathTemp.string());

    mempool.SuspendSanityCheck();
    mempool.getNonFinalPool().loadConfig();
    CTxMemPoolTestAccess{mempool}.InitInMemoryMempoolTxDB();
    mempool.ResumeSanityCheck();
    if (!g_MempoolDatarefTracker)
        g_MempoolDatarefTracker = std::make_unique<mining::MempoolDatarefTracker>();
    if (!g_BlockDatarefTracker)
        g_BlockDatarefTracker = std::make_unique<mining::BlockDatarefTracker>();

}

BasicTestingSetup::~BasicTestingSetup() {
    fs::remove_all(pathTemp);
}

TestingSetup::TestingSetup(const std::string &chainName, mining::CMiningFactory::BlockAssemblerType assemblerType)
    : BasicTestingSetup(chainName) {
    
    testConfig.SetMiningCandidateBuilder(assemblerType);
    // Ideally we'd move all the RPC tests to the functional testing framework
    // instead of unit tests, but for now we need these here.
    RegisterAllRPCCommands(tableRPC);
    mempool.SetSanityCheck(1.0);
    InitFrozenTXO(DEFAULT_FROZEN_TXO_DB_CACHE);
    pblocktree = new CBlockTreeDB(1 << 20, true);
    pcoinsTip =
        std::make_unique<CoinsDB>(
            std::numeric_limits<size_t>::max(),
            1 << 23,
            CoinsDB::MaxFiles::Default(),
            true);
    if (!InitBlockIndex(testConfig)) {
        throw std::runtime_error("InitBlockIndex failed.");
    }
    {
        // dummyState is used to report errors, not block related invalidity - ignore it
        // (see description of ActivateBestChain)
        CValidationState dummyState;
        mining::CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(mining::JournalUpdateReason::INIT) };
        auto source = task::CCancellationSource::Make();
        if (!ActivateBestChain(source->GetToken(), testConfig, dummyState, changeSet)) {
            throw std::runtime_error("ActivateBestChain failed.");
        }
    }
    InitScriptCheckQueues(testConfig, threadGroup);

    // Deterministic randomness for tests.
    g_connman =
        std::make_unique<CConnman>(
          testConfig, 0x1337, 0x1337, std::chrono::milliseconds{0});
    connman = g_connman.get();
    RegisterNodeSignals(GetNodeSignals());

    mining::g_miningFactory = std::make_unique<mining::CMiningFactory>(testConfig);
}

TestingSetup::~TestingSetup() {
    mining::g_miningFactory.reset();
    threadGroup.interrupt_all();
    threadGroup.join_all();
    UnloadBlockIndex();
    pcoinsTip.reset();

    if (g_connman)
    {
        g_connman->Interrupt();
        // call Stop first as CConnman members are using g_connman global
        // variable and they must be shut down before the variable is reset to
        // nullptr
        g_connman->Stop();
        g_connman.reset();
        connman = nullptr;
    }

    ShutdownScriptCheckQueues();
    UnregisterNodeSignals(GetNodeSignals());
    delete pblocktree;
    pblocktree = nullptr;
    ShutdownFrozenTXO();
}

TestChain100Setup::TestChain100Setup()
    : TestingSetup(CBaseChainParams::REGTEST) {
    // Generate a 100-block chain:
    coinbaseKey.MakeNewKey(true);
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey())
                                     << OP_CHECKSIG;
    for (int i = 0; i < COINBASE_MATURITY; i++) {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        coinbaseTxns.push_back(*b.vtx[0]);
    }
}

//
// Create a new block with just given transactions, coinbase paying to
// scriptPubKey, and try to add it to the current chain.
//
CBlock TestChain100Setup::CreateAndProcessBlock(
    const std::vector<CMutableTransaction> &txns, const CScript &scriptPubKey) {
    const Config &config = GlobalConfig::GetConfig();
    CBlockIndex* pindexPrev {nullptr};
    std::unique_ptr<CBlockTemplate> pblocktemplate =
            mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev);
    CBlockRef blockRef = pblocktemplate->GetBlockRef();
    CBlock &block = *blockRef;

    // Replace mempool-selected txns with just coinbase plus passed-in txns:
    block.vtx.resize(1);
    for (const CMutableTransaction &tx : txns) {
        block.vtx.push_back(MakeTransactionRef(tx));
    }
    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    unsigned int extraNonce = 0;
    IncrementExtraNonce(&block, pindexPrev, extraNonce);

    while (!CheckProofOfWork(block.GetHash(), block.nBits, config)) {
        ++block.nNonce;
    }

    std::shared_ptr<const CBlock> shared_pblock =
        std::make_shared<const CBlock>(block);
    ProcessNewBlock(GlobalConfig::GetConfig(), shared_pblock, true, nullptr,
        CBlockSource::MakeLocal("test"));

    CBlock result = block;
    return result;
}

TestChain100Setup::~TestChain100Setup() {}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CMutableTransaction &tx,
                                               CTxMemPool *pool) {
    CTransaction txn(tx);
    return FromTx(txn, pool);
}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CTransaction &txn,
                                               CTxMemPool *pool) {
    return CTxMemPoolEntry(MakeTransactionRef(txn), nFee, nTime, 
                           nHeight, spendsCoinbase, lp);
}

namespace {
// A place to put misc. setup code eg "the travis workaround" that needs to run
// at program startup and exit
struct Init {
    Init();
    ~Init();

    std::list<std::function<void(void)>> cleanup;
};

Init init;

Init::Init() {
    if (getenv("TRAVIS_NOHANG_WORKAROUND")) {
        // This is a workaround for MinGW/Win32 builds on Travis sometimes
        // hanging due to no output received by Travis after a 10-minute
        // timeout.
        // The strategy here is to let the jobs finish however long they take
        // on Travis, by feeding Travis output.  We start a parallel thread
        // that just prints out '.' once per second.
        struct Private {
            Private() : stop(false) {}
            std::atomic_bool stop;
            std::thread thr;
            std::condition_variable cond;
            std::mutex mut;
        } *p = new Private;

        p->thr = std::thread([p] {
            // thread func.. print dots
            std::unique_lock<std::mutex> lock(p->mut);
            unsigned ctr = 0;
            while (!p->stop) {
                if (ctr) {
                    // skip first period to allow app to print first
                    std::cerr << "." << std::flush;
                }
                if (!(++ctr % 79)) {
                    // newline once in a while to keep travis happy
                    std::cerr << std::endl;
                }
                p->cond.wait_for(lock, std::chrono::milliseconds(1000));
            }
        });

        cleanup.emplace_back([p]() {
            // cleanup function to kill the thread and delete the struct
            p->mut.lock();
            p->stop = true;
            p->cond.notify_all();
            p->mut.unlock();
            if (p->thr.joinable()) {
                p->thr.join();
            }
            delete p;
        });
    }
}

Init::~Init() {
    for (auto &f : cleanup) {
        if (f) {
            f();
        }
    }
}
} // end anonymous namespace
