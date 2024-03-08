// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "init.h"
#include "addrman.h"
#include "amount.h"
#include "block_index_store.h"
#include "block_index_store_loader.h"
#include "chain.h"
#include "chainparams.h"
#include "compat/sanity.h"
#include "config.h"
#include "consensus/validation.h"
#include "consensus/consensus.h"
#include "double_spend/dsattempt_handler.h"
#include "fs.h"
#include "httprpc.h"
#include "httpserver.h"
#include "invalid_txn_publisher.h"
#include "key.h"
#include "miner_id/dataref_index.h"
#include "miner_id/miner_info_tracker.h"
#include "miner_id/miner_id_db.h"
#include "miner_id/miner_id_db_defaults.h"
#include "mining/journaling_block_assembler.h"
#include "net/net.h"
#include "net/net_processing.h"
#include "net/netbase.h"
#include "policy/policy.h"
#include "rpc/client_config.h"
#include "rpc/register.h"
#include "rpc/server.h"
#include "rpc/webhook_client.h"
#include "rpc/webhook_client_defaults.h"
#include "scheduler.h"
#include "script/scriptcache.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "taskcancellation.h"
#include "timedata.h"
#include "txdb.h"
#include "txmempool.h"
#include "txn_validation_config.h"
#include "txn_validator.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "validationinterface.h"
#include "vmtouch.h"
#include "merkletreestore.h"
#include "safe_mode.h"

#ifdef ENABLE_WALLET
#include "wallet/rpcdump.h"
#include "wallet/wallet.h"
#endif
#include "warnings.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <cinttypes>
#include <chrono>

#ifndef WIN32
#include <signal.h>
#endif

#include "compiler_warnings.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/bind/bind.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>


static const bool DEFAULT_PROXYRANDOMIZE = true;
static const bool DEFAULT_REST_ENABLE = false;
static const bool DEFAULT_DISABLE_SAFEMODE = false;
static const bool DEFAULT_STOPAFTERBLOCKIMPORT = false;

std::unique_ptr<CConnman> g_connman;
std::unique_ptr<PeerLogicValidation> peerLogic;

#if ENABLE_ZMQ
CCriticalSection cs_zmqNotificationInterface;
CZMQNotificationInterface *pzmqNotificationInterface = nullptr;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for accessing
// block files don't count towards the fd_set size limit anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

/** Used to pass flags to the Bind() function */
enum BindFlags {
    BF_NONE = 0,
    BF_EXPLICIT = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST = (1U << 2),
};


//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group created by
// AppInit() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM signal handler sets
// fRequestShutdown, which triggers the DetectShutdownThread(), which interrupts
// the main thread group. DetectShutdownThread() then exits, which causes
// AppInit() to continue (it .joins the shutdown thread). Shutdown() is then
// called to clean up database connections, and stop other threads that should
// only be stopped after the main network-processing threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2 before
// adding any threads to the threadGroup, so .join_all() returns immediately and
// the parent exits from main().
//

std::shared_ptr<task::CCancellationSource> shutdownSource(task::CCancellationSource::Make());
std::atomic<bool> fDumpMempoolLater(false);

void StartShutdown() {
    shutdownSource->Cancel();
}
task::CCancellationToken GetShutdownToken()
{
    return shutdownSource->GetToken();
}

void Interrupt(boost::thread_group &threadGroup) {
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    if (g_connman) g_connman->Interrupt();
    threadGroup.interrupt_all();
}

void Shutdown() {
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown) return;


    // Remove all datarefs and minerinfo txns from the mempool
    if (g_MempoolDatarefTracker) {
        std::vector<COutPoint> funds = g_MempoolDatarefTracker->funds();
        std::vector<TxId> datarefs;
        std::transform(funds.cbegin(), funds.cend(), std::back_inserter(datarefs), [](const COutPoint& p) {return p.GetTxId();});
        if (!datarefs.empty())
            mempool.RemoveTxnsAndDescendants(datarefs, nullptr);
    }

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed
    /// part of the way, for example if the data directory was found to be
    /// locked. Be sure that anything that writes files or flushes caches only
    /// does this if the respective module was initialized.
    RenameThread("shutoff");
    mempool.AddTransactionsUpdated(1);

    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
#ifdef ENABLE_WALLET
    for (CWalletRef pwallet : vpwallets) {
        pwallet->Flush(false);
    }
#endif
    MapPort(false);
    if(peerLogic) {
        peerLogic->UnregisterValidationInterface();
    }

    rpc::client::g_pWebhookClient.reset();
    mining::g_miningFactory.reset();

    if (g_connman) {
        // call Stop first as CConnman members are using g_connman global
        // variable and they must be shut down before the variable is reset to
        // nullptr
        g_connman->Stop();
        g_connman.reset();
    }
    peerLogic.reset();

    // must be called after g_connman shutdown as conman threads could still be
    // using it before that
    ShutdownScriptCheckQueues();

    UnregisterNodeSignals(GetNodeSignals());
    if (fDumpMempoolLater &&
        gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        mempool.DumpMempool();
    }

    {
        LOCK(cs_main);
        if (pcoinsTip != nullptr) {
            FlushStateToDisk();
        }
        pcoinsTip.reset();
        delete pblocktree;
        pblocktree = nullptr;
    }

    // Flush/destroy miner ID database
    g_minerIDs.reset();
    // Destroy dataRef index
    g_dataRefIndex.reset();

#ifdef ENABLE_WALLET
    for (CWalletRef pwallet : vpwallets) {
        pwallet->Flush(true);
    }
#endif

    pMerkleTreeFactory.reset();

#if ENABLE_ZMQ
    {
        LOCK(cs_zmqNotificationInterface);
        if (pzmqNotificationInterface) {
            pzmqNotificationInterface->UnregisterValidationInterface();
            delete pzmqNotificationInterface;
            pzmqNotificationInterface = nullptr;
        }
    }
#endif

#ifndef WIN32
    try {
        fs::remove(GetPidFile());
    } catch (const fs::filesystem_error &e) {
        LogPrintf("%s: Unable to remove pidfile: %s\n", __func__, e.what());
    }
#endif
    UnregisterAllValidationInterfaces();
#ifdef ENABLE_WALLET
    for (CWalletRef pwallet : vpwallets) {
        delete pwallet;
    }
    vpwallets.clear();
#endif
    ShutdownFrozenTXO();
    BlockIndexStoreLoader(mapBlockIndex).ForceClear();

    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int) {
    StartShutdown();
}

void HandleSIGHUP(int) {
    GetLogger().fReopenDebugLog = true;
}

static bool Bind(CConnman &connman, const CService &addr, unsigned int flags) {
    if (!(flags & BF_EXPLICIT) && IsLimited(addr)) return false;
    std::string strError;
    if (!connman.BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
        if (flags & BF_REPORT_ERROR) return InitError(strError);
        return false;
    }
    return true;
}
void OnRPCStarted() {
    uiInterface.NotifyBlockTip.connect(&RPCNotifyBlockChange);
}

void OnRPCStopped() {
    uiInterface.NotifyBlockTip.disconnect(&RPCNotifyBlockChange);
    RPCNotifyBlockChange(false, nullptr);
    cvBlockChange.notify_all();
    LogPrint(BCLog::RPC, "RPC stopped.\n");
}

void OnRPCPreCommand(const CRPCCommand &cmd) {
    // Observe safe mode.
    std::string strWarning = GetWarnings("rpc");
    if (strWarning != "" &&
        !gArgs.GetBoolArg("-disablesafemode", DEFAULT_DISABLE_SAFEMODE) &&
        !cmd.okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE,
                           std::string("Safe mode: ") + strWarning);
}

std::string HelpMessage(HelpMessageMode mode, const Config& config) {
    const auto defaultBaseParams =
        CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams =
        CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto defaultChainParams = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnetChainParams =
        CreateChainParams(CBaseChainParams::TESTNET);
    const bool showDebug = gArgs.GetBoolArg("-help-debug", false);

    // When adding new options to the categories, please keep and ensure
    // alphabetical ordering. Do not translate _(...) -help-debug options, Many
    // technical terms, and only a very small audience, so is unnecessary stress
    // to translators.
    std::string strUsage = HelpMessageGroup(_("Options:"));
    strUsage += HelpMessageOpt("-?", _("Print this help message and exit"));
    strUsage += HelpMessageOpt("-version", _("Print version and exit"));
    strUsage += HelpMessageOpt(
        "-alertnotify=<cmd>",
        _("Execute command when a relevant alert is received or we see a "
          "really long fork (%s in cmd is replaced by message)"));
    strUsage += HelpMessageOpt("-blocknotify=<cmd>",
                               _("Execute command when the best block changes "
                                 "(%s in cmd is replaced by block hash)"));
    if (showDebug)
        strUsage += HelpMessageOpt(
            "-blocksonly",
            strprintf(
                _("Whether to operate in a blocks only mode (default: %d)"),
                DEFAULT_BLOCKSONLY));
    strUsage += HelpMessageOpt(
        "-assumevalid=<hex>",
        strprintf(
            _("If this block is in the chain assume that it and its ancestors "
              "are valid and potentially skip their script verification (0 to "
              "verify all, default: %s, testnet: %s)"),
            defaultChainParams->GetConsensus().defaultAssumeValid.GetHex(),
            testnetChainParams->GetConsensus().defaultAssumeValid.GetHex()));
    strUsage += HelpMessageOpt(
        "-conf=<file>", strprintf(_("Specify configuration file (default: %s)"),
                                  BITCOIN_CONF_FILENAME));
    if (mode == HMM_BITCOIND) {
#if HAVE_DECL_DAEMON
        strUsage += HelpMessageOpt(
            "-daemon",
            _("Run in the background as a daemon and accept commands"));
#endif
    }
    strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-dbbatchsize",
            strprintf(
                "Maximum database write batch size in bytes (default: %u). The value may be given in bytes or with unit (B, kB, MB, GB).",
                nDefaultDbBatchSize));
    }
    strUsage += HelpMessageOpt(
        "-dbcache=<n>",
        strprintf(
            _("Set database cache size in megabytes (%d to %d, default: %d). The value may be given in megabytes or with unit (B, KiB, MiB, GiB)."),
            nMinDbCache, nMaxDbCache, nDefaultDbCache));

    strUsage += HelpMessageOpt(
        "-frozentxodbcache=<n>",
        strprintf(
            _("Set cache size for database holding a list of frozen transaction outputs in bytes (default: %u)"),
            DEFAULT_FROZEN_TXO_DB_CACHE));

    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-feefilter", strprintf("Tell other nodes to filter invs to us by "
                                    "our mempool min fee (default: %d)",
                                    DEFAULT_FEEFILTER));
    }

    strUsage += HelpMessageOpt(
        "-genesisactivationheight",
        strprintf("Set block height at which genesis should be activated. "
                  "(default: %u).",
                  defaultChainParams->GetConsensus().genesisHeight));

    strUsage += HelpMessageOpt(
        "-loadblock=<file>",
        _("Imports blocks from external blk000??.dat file on startup"));

    strUsage += HelpMessageOpt("-maxmempool=<n>",
                   strprintf(_("Keep the resident size of the transaction memory pool below <n> megabytes "
                               "(default: %u%s,  must be at least %d). "
                               "The value may be given in megabytes or with unit (B, kB, MB, GB)."),
                             DEFAULT_MAX_MEMPOOL_SIZE,
                             showDebug ? ", 0 to turn off mempool memory sharing with dbcache" : "",
                             std::ceil(DEFAULT_MAX_MEMPOOL_SIZE*0.3)));
    if (showDebug) 
    {
        strUsage += HelpMessageOpt("-maxmempoolsizedisk=<n>",
                                   strprintf(_("Experimental: Additional amount of mempool transactions to keep stored on disk "
                                               "below <n> megabytes (default: -maxmempool x %u). Actual disk usage will "
                                               "be larger due to leveldb compaction strategy. "
                                               "The value may be given in megabytes or with unit (B, kB, MB, GB)."),
                                             DEFAULT_MAX_MEMPOOL_SIZE_DISK_FACTOR));
    }
    strUsage += HelpMessageOpt("-mempoolmaxpercentcpfp=<n>",
                               strprintf(_("Percentage of total mempool size (ram+disk) to allow for "
                                           "low paying transactions (0..100) (default: %u)"),
                                         DEFAULT_MEMPOOL_MAX_PERCENT_CPFP));
    strUsage +=
        HelpMessageOpt("-mempoolexpiry=<n>",
                       strprintf(_("Do not keep transactions in the mempool "
                                   "longer than <n> hours (default: %u)"),
                                 DEFAULT_MEMPOOL_EXPIRY));
    strUsage += HelpMessageOpt("-maxmempoolnonfinal=<n>",
                               strprintf(_("Keep the non-final transaction memory pool "
                                           "below <n> megabytes (default: %u). The value may be given in megabytes or with unit (B, KiB, MiB, GiB)."),
                                         DEFAULT_MAX_NONFINAL_MEMPOOL_SIZE));
    strUsage +=
        HelpMessageOpt("-mempoolexpirynonfinal=<n>",
                       strprintf(_("Do not keep transactions in the non-final mempool "
                                   "longer than <n> hours (default: %u)"),
                                 DEFAULT_NONFINAL_MEMPOOL_EXPIRY));
    strUsage +=
        HelpMessageOpt("-mempoolnonfinalmaxreplacementrate=<n>",
                       strprintf(_("The maximum rate at which a transaction in the non-final mempool can be replaced by "
                                   "another updated transaction, expressed as transactions per hour. (default: %u/hour)"),
                                 DEFAULT_NONFINAL_MAX_REPLACEMENT_RATE));
    if (showDebug) {
        strUsage +=
            HelpMessageOpt("-mempoolnonfinalmaxreplacementrateperiod=<n>",
                           strprintf(_("The period of time (in minutes) over which the maximum rate for non-final transactions "
                                       "is measured (see -mempoolnonfinalmaxreplacementrate above). (default: %u)"),
                                     DEFAULT_NONFINAL_MAX_REPLACEMENT_RATE_PERIOD));
    }
    if (showDebug) {
        strUsage += HelpMessageOpt("-checknonfinalfreq=<n>",
                       strprintf(_("Run checks on non-final transactions every <n> "
                                   "milli-seconds (default: %u)"),
                                 CTimeLockedMempool::DEFAULT_NONFINAL_CHECKS_FREQ));
    }
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-minimumchainwork=<hex>",
            strprintf(
                "Minimum work assumed to exist on a valid chain in hex "
                "(default: %s, testnet: %s)",
                defaultChainParams->GetConsensus().nMinimumChainWork.GetHex(),
                testnetChainParams->GetConsensus().nMinimumChainWork.GetHex()));
    }
    strUsage +=
        HelpMessageOpt("-persistmempool",
                       strprintf(_("Whether to save the mempool on shutdown "
                                   "and load on restart (default: %u)"),
                                 DEFAULT_PERSIST_MEMPOOL));
    strUsage += HelpMessageOpt(
        "-threadsperblock=<n>",
        strprintf(_("Set the number of script verification threads used when "
                    "validating single block (0 to %d, 0 = auto, default: %d)"),
                  MAX_TXNSCRIPTCHECK_THREADS,
                  DEFAULT_SCRIPTCHECK_THREADS));
    strUsage += HelpMessageOpt(
        "-txnthreadsperblock=<n>",
        strprintf(_("Set the number of transaction verification threads used when "
                    "validating single block (0 to %d, 0 = auto, default: %d)"),
                  MAX_TXNSCRIPTCHECK_THREADS,
                  DEFAULT_TXNCHECK_THREADS));
    strUsage +=
        HelpMessageOpt(
            "-scriptvalidatormaxbatchsize=<n>",
            strprintf(
                _("Set size of script verification batch per thread (1 to %d, "
                  "default: %d)"),
                std::numeric_limits<uint8_t>::max(),
                DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE));

    strUsage +=
        HelpMessageOpt(
        "-maxparallelblocks=<n>",
        strprintf(_("Set the number of block that can be validated in parallel"
                    " across all nodes. If additional block arrive, validation"
                    " of an old block is terminated. (1 to 100, default: %d)"),
                  DEFAULT_SCRIPT_CHECK_POOL_SIZE));
    strUsage +=
        HelpMessageOpt(
            "-maxparallelblocksperpeer=<n>",
            strprintf(
                _("Set the number of blocks that can be validated in parallel "
                  "from a single peer. If peers sends another block, the validation"
                  " of it is delayed. (1 to maxparallelblocks, default: %d)"),
                DEFAULT_NODE_ASYNC_TASKS_LIMIT));

#ifndef WIN32
    strUsage += HelpMessageOpt(
        "-pid=<file>",
        strprintf(_("Specify pid file (default: %s)"), BITCOIN_PID_FILENAME));
#endif

    strUsage += HelpMessageOpt(
        "-preload=<n>",
            _("If n is set to 1, blockchain state will be preloaded into memory. If n is 0, no preload will happen. "
              "Other values for n are not allowed. The default value is 0."
              " This option is not supported on Windows operating systems.")
            );

    strUsage += HelpMessageOpt(
        "-prune=<n>",
        strprintf(
            _("Reduce storage requirements by enabling pruning (deleting) of "
              "old blocks. This allows the pruneblockchain RPC to be called to "
              "delete specific blocks, and enables automatic pruning of old "
              "blocks if a target size in MiB is provided. This mode is "
              "incompatible with -txindex and -rescan. "
              "Warning: Reverting this setting requires re-downloading the "
              "entire blockchain. "
              "(default: 0 = disable pruning blocks, 1 = allow manual pruning "
              "via RPC, >%u = automatically prune block files to stay under "
              "the specified target size in MiB, but still keep the last %u blocks "
              "to speed up a potential reorg even if this results in the pruning "
              "target being exceeded)"
              "Note: Currently achievable prune target is ~100GB (mainnet). "
              "Setting the target size too low will not affect pruning function, "
              "but will not guarantee block files size staying under the threshold at all times. "),
            MIN_DISK_SPACE_FOR_BLOCK_FILES / ONE_MEBIBYTE, config.GetMinBlocksToKeep()));

    if(showDebug) {
        strUsage += HelpMessageOpt(
            "-pruneminblockstokeep=<n>",
            strprintf(
                _("Set the minimum number of most recent blocks to keep when pruning. "
                  "WARNING: Changing this value could cause unexpected problems with reorgs, "
                  "safe-mode activation and other functions; use at your own risk. "
                  "It should only be used for a limited time to help a node with very limited "
                  "disk space make progress downloading the blockchain "
                  "(default: %d, minimum value: %d)."), DEFAULT_MIN_BLOCKS_TO_KEEP, MIN_MIN_BLOCKS_TO_KEEP)
        );
    }

    strUsage += HelpMessageOpt(
        "-reindex-chainstate",
        _("Rebuild chain state from the currently indexed blocks"));
    strUsage +=
        HelpMessageOpt("-reindex", _("Rebuild chain state and block index from "
                                     "the blk*.dat files on disk"));
    strUsage +=
        HelpMessageOpt("-rejectmempoolrequest", strprintf(_("Reject every mempool request from "
                                     "non-whitelisted peers (default: %d)."), DEFAULT_REJECTMEMPOOLREQUEST));
#ifndef WIN32
    strUsage += HelpMessageOpt(
        "-sysperms",
        _("Create new files with system default permissions, instead of umask "
          "077 (only effective with disabled wallet functionality)"));
#endif
    strUsage += HelpMessageOpt(
        "-txindex", strprintf(_("Maintain a full transaction index, used by "
                                "the getrawtransaction rpc call (default: %d)"),
                              DEFAULT_TXINDEX));
    strUsage += HelpMessageOpt(
        "-maxmerkletreediskspace", strprintf(_("Maximum disk size in bytes that "
        "can be taken by stored merkle trees. This size should not be less than default size "
        "(default: %uMB for a maximum 4GB block size). The value may be given in bytes or with unit (B, kiB, MiB, GiB)."),
        CalculateMinDiskSpaceForMerkleFiles(4 * ONE_GIGABYTE)/ONE_MEGABYTE));
    strUsage += HelpMessageOpt(
        "-preferredmerkletreefilesize", strprintf(_("Preferred size of a single datafile containing "
        "merkle trees. When size is reached, new datafile is created. If preferred size is less than "
        "size of a single merkle tree, it will still be stored, meaning datafile size can be larger than "
        "preferred size. (default: %uMB for a maximum 4GB block size). The value may be given in bytes or with unit (B, kiB, MiB, GiB)."),
        CalculatePreferredMerkleTreeSize(4 * ONE_GIGABYTE)/ONE_MEGABYTE));
    strUsage += HelpMessageOpt(
        "-maxmerkletreememcachesize", strprintf(_("Maximum merkle trees memory cache size in bytes. For "
        "faster responses, requested merkle trees are stored into a memory cache. "
        "(default: %uMB for a maximum 4GB block size). The value may be given in bytes or with unit (B, kiB, MiB, GiB)."),
        CalculatePreferredMerkleTreeSize(4 * ONE_GIGABYTE)/ONE_MEGABYTE));
    strUsage += HelpMessageGroup(_("Connection options:"));
    strUsage += HelpMessageOpt(
        "-addnode=<ip>",
        _("Add a node to connect to and attempt to keep the connection open"));
    strUsage += HelpMessageOpt(
        "-banscore=<n>",
        strprintf(
            _("Threshold for disconnecting misbehaving peers (default: %u)"),
            DEFAULT_BANSCORE_THRESHOLD));
    strUsage += HelpMessageOpt(
        "-bantime=<n>", strprintf(_("Number of seconds to keep misbehaving "
                                    "peers from reconnecting (default: %u)"),
                                  DEFAULT_MISBEHAVING_BANTIME));
    strUsage += HelpMessageOpt("-bind=<addr>",
                               _("Bind to given address and always listen on "
                                 "it. Use [host]:port notation for IPv6"));

    /** Block download */
    strUsage += HelpMessageOpt("-blockstallingmindownloadspeed=<n>",
        strprintf(_("Minimum average download speed (Kbytes/s) we will allow a stalling "
                    "peer to fall to during IBD. A value of 0 means stall detection is "
                    "disabled (default: %uKb/s)"), DEFAULT_MIN_BLOCK_STALLING_RATE));
    if (showDebug) {
        strUsage += HelpMessageOpt("-blockstallingtimeout=<n>",
            strprintf(_("Number of seconds to wait before considering a peer stalling "
                        "during IBD (default: %u)"), DEFAULT_BLOCK_STALLING_TIMEOUT));
        strUsage += HelpMessageOpt("-blockdownloadwindow=<n>",
            strprintf(_("Size of block download window before considering we may be stalling "
                        "during IBD (default: %u)"), DEFAULT_BLOCK_DOWNLOAD_WINDOW));
        strUsage += HelpMessageOpt("-blockdownloadlowerwindow=<n>",
            strprintf(_("A further lower limit on the download window (above) to help the node hit the pruning target (if enabled). "
            "If pruning is NOT enabled then this will default to the same as the blockdownloadwindow. An operator may choose to "
            "reduce this value even if pruning is not enabled which will result in the node using less disk space during IBD but "
            "at the possible cost of a slower IBD time. Conversely, an operator of a pruned node may choose to increase this value "
            "to reduce the time it takes to perform IBD but at the cost of possibly exceeding the pruning target at times. "
            "(default if pruning enabled: %u, default if pruning not enabled: %u)"),
                DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW, DEFAULT_BLOCK_DOWNLOAD_WINDOW));
        strUsage += HelpMessageOpt("-blockdownloadslowfetchtimeout=<n>",
            strprintf(_("Number of seconds to wait for a block to be received before triggering "
                        "a slow fetch timeout (default: %u)"), DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT));
        strUsage += HelpMessageOpt("-blockdownloadmaxparallelfetch=<n>",
            strprintf(_("Maximum number of parallel requests to different peers we will issue for "
                        "a block that has exceeded the slow fetch detection timeout (default: %u)"),
                        DEFAULT_MAX_BLOCK_PARALLEL_FETCH));
        strUsage += HelpMessageOpt("-blockdownloadtimeoutbasepercent=<n>",
            strprintf(_("Block download timeout, expressed as percentage of the block interval which is %d minutes by default."
                        " (default: %u%%)"),
                        defaultChainParams->GetConsensus().nPowTargetSpacing / 60,
                        DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE));
        strUsage += HelpMessageOpt("-blockdownloadtimeoutbaseibdpercent=<n>",
            strprintf(_("Block download timeout during the initial block download, expressed as percentage of the block interval which is %d minutes by default."
                        " (default: %u%%)"),
                        defaultChainParams->GetConsensus().nPowTargetSpacing / 60,
                        DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD));
        strUsage += HelpMessageOpt("-blockdownloadtimeoutperpeerpercent=<n>",
            strprintf(_("Additional block download time per parallel downloading peer, expressed as percentage of the block interval which is %d minutes by default."
                        " (default: %u%%)"),
                        defaultChainParams->GetConsensus().nPowTargetSpacing / 60,
                        DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER));
    }

    strUsage += HelpMessageOpt(
        "-broadcastdelay=<n>",
        strprintf(
            _("Set inventory broadcast delay duration in millisecond(min: %d, max: %d)"),
            0,MAX_INV_BROADCAST_DELAY));
    strUsage +=
        HelpMessageOpt("-connect=<ip>",
                       _("Connect only to the specified node(s); -noconnect or "
                         "-connect=0 alone to disable automatic connections"));
    strUsage += HelpMessageOpt("-discover",
                               _("Discover own IP addresses (default: 1 when "
                                 "listening and no -externalip or -proxy)"));
    strUsage += HelpMessageOpt(
        "-dns",
        _("Allow DNS lookups for -addnode, -seednode and -connect") + " " +
            strprintf(_("(default: %d)"), DEFAULT_NAME_LOOKUP));
    strUsage += HelpMessageOpt(
        "-dnsseed", _("Query for peer addresses via DNS lookup, if low on "
                      "addresses (default: 1 unless -connect/-noconnect)"));
    strUsage += HelpMessageOpt("-externalip=<ip>",
                               _("Specify your own public address"));
    strUsage += HelpMessageOpt(
        "-forcednsseed",
        strprintf(
            _("Always query for peer addresses via DNS lookup (default: %d)"),
            DEFAULT_FORCEDNSSEED));
    strUsage +=
        HelpMessageOpt("-listen", _("Accept connections from outside (default: "
                                    "1 if no -proxy or -connect/-noconnect)"));
    strUsage += HelpMessageOpt(
        "-maxaddnodeconnections=<n>",
        strprintf(_("Maximum number of additional outgoing connections to maintain that have been added "
                    "via addnode (default: %u)"), DEFAULT_MAX_ADDNODE_CONNECTIONS));
    strUsage +=
        HelpMessageOpt("-maxblocktxnpercent=<n>",
        strprintf(_("Maximum perentage of txns from a block we will respond to a getblocktxn request "
                    "with a blocktxn response. Larger than this we will just respond with the entire block "
                    "(default: %u)"),
            DEFAULT_BLOCK_TXN_MAX_PERCENT));
    strUsage += HelpMessageOpt(
        "-maxoutboundconnections=<n>",
        strprintf(_("Maintain at most <n> outbound connections to peers (default: %u)"),
                  DEFAULT_MAX_OUTBOUND_CONNECTIONS));
    strUsage += HelpMessageOpt(
        "-maxconnectionsfromaddr=<n>",
        strprintf(_("Maximum number of inbound connections from a single address "
                    "(not applicable to whitelisted peers) 0 = unrestricted (default: %d)"),
                  DEFAULT_MAX_CONNECTIONS_FROM_ADDR));
    strUsage += HelpMessageOpt(
        "-maxconnections=<n>",
        strprintf(_("Maintain at most <n> connections to peers (default: %d)"),
                  DEFAULT_MAX_PEER_CONNECTIONS));
    strUsage +=
        HelpMessageOpt("-maxreceivebuffer=<n>",
                       strprintf(_("Maximum per-connection receive buffer "
                                   "in kilobytes (default: %u). The value may be given in kilobytes or with unit (B, kB, MB, GB)."),
                                 DEFAULT_MAXRECEIVEBUFFER));
    strUsage += HelpMessageOpt(
        "-maxsendbuffer=<n>", strprintf(_("Maximum per-connection send buffer "
                                          "in kilobytes (default: %u). The value may be given in kilobytes or with unit (B, kB, MB, GB)."),
                                        DEFAULT_MAXSENDBUFFER));
    strUsage += HelpMessageOpt("-maxsendbuffermult=<n>",
        strprintf(_("Temporary multiplier applied to the -maxsendbuffer size to "
                    "allow connections to unblock themselves in the unlikely "
                    "situation where they have become paused for both sending and "
                    "receiving (default: %d)"), DEFAULT_MAXSENDBUFFER_MULTIPLIER));
    strUsage += HelpMessageOpt(
        "-factormaxsendqueuesbytes=<n>",
        strprintf(_("Factor that will be multiplied with excessiveBlockSize"
            " to limit the maximum bytes in all sending queues. If this"
            " size is exceeded, no response to block related P2P messages is sent."
            " (default factor: %u)"),
            DEFAULT_FACTOR_MAX_SEND_QUEUES_BYTES));
    strUsage += HelpMessageOpt(
        "-maxtimeadjustment",
        strprintf(_("Maximum allowed median peer time offset adjustment. Local "
                    "perspective of time may be influenced by peers forward or "
                    "backward by this amount. (default: %u seconds)"),
                  DEFAULT_MAX_TIME_ADJUSTMENT));

    /** Multi-streaming */
    strUsage += HelpMessageOpt("-multistreams",
        _("Enable the use of multiple streams to our peers") + " " +
            strprintf(_("(default: %d)"), DEFAULT_STREAMS_ENABLED));
    strUsage += HelpMessageOpt("-multistreampolicies",
        _("List of stream policies to use with our peers in order of preference") + " " +
            strprintf(_("(available policies: %s, default: %s)"),
        StreamPolicyFactory{}.GetAllPolicyNamesStr(), DEFAULT_STREAM_POLICY_LIST));

    strUsage += HelpMessageOpt(
        "-onlynet=<net>",
        _("Only connect to nodes in network <net> (ipv4 or ipv6)"));
    strUsage +=
        HelpMessageOpt("-permitbaremultisig",
                       strprintf(_("Relay non-P2SH multisig (default: %d)"),
                                 DEFAULT_PERMIT_BAREMULTISIG));
    if(showDebug) {
        strUsage += HelpMessageOpt("-p2ptimeout=<n>",
            strprintf(_("Number of seconds before timing out some operations "
                "within the P2P layer. Affected operations include pings and "
                "send/receive inactivity (default: %u seconds)"), DEFAULT_P2P_TIMEOUT_INTERVAL));
        strUsage += HelpMessageOpt("-p2phandshaketimeout=<n>",
            strprintf(_("Number of seconds to wait for a P2P connection to fully "
                "establish before timing out and dropping it (default: %u seconds)"),
                DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL));
    }
    strUsage += HelpMessageOpt(
        "-peerbloomfilters",
        strprintf(_("Support filtering of blocks and transaction with bloom "
                    "filters (default: %d)"),
                  DEFAULT_PEERBLOOMFILTERS));
    strUsage += HelpMessageOpt(
        "-port=<port>",
        strprintf(
            _("Listen for connections on <port> (default: %u or testnet: %u)"),
            defaultChainParams->GetDefaultPort(),
            testnetChainParams->GetDefaultPort()));
    strUsage +=
        HelpMessageOpt("-proxy=<ip:port>", _("Connect through SOCKS5 proxy"));
    strUsage += HelpMessageOpt(
        "-proxyrandomize",
        strprintf(_("Randomize credentials for every proxy connection. (default: %d)"),
                  DEFAULT_PROXYRANDOMIZE));
    strUsage += HelpMessageOpt(
        "-seednode=<ip>",
        _("Connect to a node to retrieve peer addresses, and disconnect"));
    strUsage += HelpMessageOpt(
        "-timeout=<n>", strprintf(_("Specify connection timeout in "
                                    "milliseconds (minimum: 1, default: %d)"),
                                  DEFAULT_CONNECT_TIMEOUT));
#ifdef USE_UPNP
#if USE_UPNP
    strUsage +=
        HelpMessageOpt("-upnp", _("Use UPnP to map the listening port "
                                  "(default: 1 when listening and no -proxy)"));
#else
    strUsage += HelpMessageOpt(
        "-upnp",
        strprintf(_("Use UPnP to map the listening port (default: %u)"), 0));
#endif
#endif
    strUsage +=
        HelpMessageOpt("-whitebind=<addr>",
                       _("Bind to given address and whitelist peers connecting "
                         "to it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt(
        "-whitelist=<IP address or network>",
        _("Whitelist peers connecting from the given IP address (e.g. 1.2.3.4) "
          "or CIDR notated network (e.g. 1.2.3.0/24). Can be specified "
          "multiple times.") +
            " " + _("Whitelisted peers cannot be DoS banned and their "
                    "transactions are always relayed, even if they are already "
                    "in the mempool, useful e.g. for a gateway"));
    strUsage += HelpMessageOpt(
        "-whitelistrelay",
        strprintf(_("Accept relayed transactions received from whitelisted "
                    "peers even when not relaying transactions (default: %d)"),
                  DEFAULT_WHITELISTRELAY));
    strUsage += HelpMessageOpt(
        "-whitelistforcerelay",
        strprintf(_("Force relay of transactions from whitelisted peers even "
                    "if they violate local relay policy (default: %d)"),
                  DEFAULT_WHITELISTFORCERELAY));
    strUsage += HelpMessageOpt(
        "-maxuploadtarget=<n>",
        strprintf(_("Tries to keep outbound traffic under the given target (in "
                    "MiB per 24h), 0 = no limit (default: %d). The value may be given in megabytes or with unit (KiB, MiB, GiB)."),
                  DEFAULT_MAX_UPLOAD_TARGET));
    strUsage += HelpMessageOpt(
        "-maxpendingresponses_getheaders=<n>",
        strprintf(_("Maximum allowed number of pending responses in the sending queue for received GETHEADERS P2P requests before "
                    "the connection is closed. Not applicable to whitelisted peers. 0 = no limit (default: %d). Main purpose of "
                    "this setting is to limit memory usage. The specified value should be small (e.g. ~50) since in practice connected "
                    "peers do not need to send many GETHEADERS requests in parallel."),
                  DEFAULT_MAXPENDINGRESPONSES_GETHEADERS));
    strUsage += HelpMessageOpt(
        "-maxpendingresponses_gethdrsen=<n>",
        strprintf(_("Maximum allowed number of pending responses in the sending queue for received GETHDRSEN P2P requests before "
                    "the connection is closed. Not applicable to whitelisted peers. 0 = no limit (default: %d). Main purpose of "
                    "this setting is to limit memory usage. The specified value should be small (e.g. ~10) since in practice connected "
                    "peers do not need to send many GETHDRSEN requests in parallel."),
                  DEFAULT_MAXPENDINGRESPONSES_GETHDRSEN));

#ifdef ENABLE_WALLET
    strUsage += CWallet::GetWalletHelpString(showDebug);
#endif

#if ENABLE_ZMQ
    strUsage += HelpMessageGroup(_("ZeroMQ notification options:"));
    strUsage += HelpMessageOpt("-zmqpubhashblock=<address>",
                               _("Enable publish hash block in <address>. "
                               "For more information see doc/zmq.md."));
    strUsage +=
        HelpMessageOpt("-zmqpubhashtx=<address>",
                       _("Enable publish hash transaction in <address>. "
                       "For more information see doc/zmq.md."));
    strUsage += HelpMessageOpt("-zmqpubrawblock=<address>",
                               _("Enable publish raw block in <address>. "
                                 "For more information see doc/zmq.md."));
    strUsage +=
        HelpMessageOpt("-zmqpubrawtx=<address>",
                       _("Enable publish raw transaction in <address>. "
                       "For more information see doc/zmq.md."));
    strUsage +=
        HelpMessageOpt("-zmqpubinvalidtx=<address>",
                       _("Enable publish invalid transaction in <address>. -invalidtxsink=ZMQ should be specified. "
                       "For more information see doc/zmq.md."));
    strUsage += HelpMessageOpt("-zmqpubremovedfrommempool=<address>",
                               _("Enable publish removal of transaction (txid and the reason in json format) in <address>. "
                               "For more information see doc/zmq.md."));
    strUsage += HelpMessageOpt("-zmqpubremovedfrommempoolblock=<address>",
                               _("Enable publish removal of transaction (txid and the reason in json format) in <address>. "
                               "For more information see doc/zmq.md."));
    strUsage += HelpMessageOpt("-zmqpubhashtx2=<address>",
                       _("Enable publish hash transaction in <address>. "
                       "For more information see doc/zmq.md."));
    strUsage += HelpMessageOpt("-zmqpubrawtx2=<address>",
                       _("Enable publish raw transaction in <address>. "
                       "For more information see doc/zmq.md."));
    strUsage += HelpMessageOpt("-zmqpubhashblock2=<address>",
                               _("Enable publish hash block in <address>. "
                               "For more information see doc/zmq.md."));
    strUsage += HelpMessageOpt("-zmqpubrawblock2=<address>",
                               _("Enable publish raw block in <address>. "
                               "For more information see doc/zmq.md."));
#endif

    strUsage += HelpMessageGroup(_("Debugging/Testing options:"));
    strUsage += HelpMessageOpt("-uacomment=<cmt>",
                               _("Append comment to the user agent string"));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-checkblocks=<n>",
            strprintf(
                _("How many blocks to check at startup (default: %u, 0 = all)"),
                DEFAULT_CHECKBLOCKS));
        strUsage +=
            HelpMessageOpt("-checklevel=<n>",
                           strprintf(_("How thorough the block verification of "
                                       "-checkblocks is (0-4, default: %u)"),
                                     DEFAULT_CHECKLEVEL));
        strUsage += HelpMessageOpt(
            "-checkblockindex",
            strprintf("Do a full consistency check for mapBlockIndex, "
                      "setBlockIndexCandidates, chainActive and "
                      "mapBlocksUnlinked occasionally. Also sets -checkmempool "
                      "(default: %u)",
                      defaultChainParams->DefaultConsistencyChecks()));
        strUsage += HelpMessageOpt(
            "-checkmempool=<n>",
            strprintf("Run checks every <n> transactions (default: %u)",
                      defaultChainParams->DefaultConsistencyChecks()));
        strUsage += HelpMessageOpt(
            "-checkpoints", strprintf("Only accept block chain matching "
                                      "built-in checkpoints (default: %d)",
                                      DEFAULT_CHECKPOINTS_ENABLED));

        strUsage +=
            HelpMessageOpt("-dropmessagestest=<n>",
                           "Randomly drop 1 of every <n> network messages");
        strUsage +=
            HelpMessageOpt("-fuzzmessagestest=<n>",
                           "Randomly fuzz 1 of every <n> network messages");
        strUsage += HelpMessageOpt(
            "-stopafterblockimport",
            strprintf(
                "Stop running after importing blocks from disk (default: %d)",
                DEFAULT_STOPAFTERBLOCKIMPORT));
        strUsage += HelpMessageOpt(
            "-stopatheight", strprintf("Stop running after reaching the given "
                                       "height in the main chain (default: %u)",
                                       DEFAULT_STOPATHEIGHT));
        strUsage += HelpMessageOpt(
            "-streamsendratelimit=<n>",
            strprintf(_("Specify stream sending bandwidth upper rate limit in bytes/sec. "
                "A negative value means no limit. (default: %d)"),
                Stream::DEFAULT_SEND_RATE_LIMIT));
        strUsage += HelpMessageOpt(
            "-limitancestorcount=<n>",
            strprintf("Do not accept transactions if maximum height of in-mempool "
                      "ancestor chain is <n> or more (default: %lu)",
                      DEFAULT_ANCESTOR_LIMIT));
        strUsage += HelpMessageOpt(
            "-limitcpfpgroupmemberscount=<n>",
            strprintf("Do not accept transactions if number of in-mempool transactions "
                      "which we are not willing to mine due to a low fee is <n> or more (default: %lu)",
                      DEFAULT_SECONDARY_MEMPOOL_ANCESTOR_LIMIT));
    }
    strUsage += HelpMessageOpt(
        "-debug=<category>",
        strprintf(_("Output debugging information (default: %u, supplying "
                    "<category> is optional)"),
                  0) +
            ". " + _("If <category> is not supplied or if <category> = 1, "
                     "output all debugging information.") +
            _("<category> can be:") + " " + ListLogCategories() + ".");
    strUsage += HelpMessageOpt(
        "-debugexclude=<category>",
        strprintf(_("Exclude debugging information for a category. Can be used "
                    "in conjunction with -debug=1 to output debug logs for all "
                    "categories except one or more specified categories.")));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-nodebug", "Turn off debugging messages, same as -debug=0");
    }
    strUsage += HelpMessageOpt(
        "-help-debug",
        _("Show all debugging options (usage: --help -help-debug)"));
    strUsage += HelpMessageOpt(
        "-debugp2pthreadstalls",
        _("Log P2P requests that stall request processing loop for longer than "
          "specified milliseconds (default: disabled)"));
    strUsage += HelpMessageOpt(
        "-logips",
        strprintf(_("Include IP addresses in debug output (default: %d)"),
                  DEFAULT_LOGIPS));
    strUsage += HelpMessageOpt(
        "-logtimestamps",
        strprintf(_("Prepend debug output with timestamp (default: %d)"),
                  DEFAULT_LOGTIMESTAMPS));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-logtimemicros",
            strprintf(
                "Add microsecond precision to debug timestamps (default: %d)",
                DEFAULT_LOGTIMEMICROS));
        strUsage += HelpMessageOpt(
            "-mocktime=<n>",
            "Replace actual time with <n> seconds since epoch (default: 0)");
        strUsage += HelpMessageOpt(
            "-blocksizeactivationtime=<n>",
            "Change time that specifies when new defaults for -blockmaxsize are used");
        strUsage += HelpMessageOpt(
            "-maxsigcachesize=<n>",
            strprintf("Limit size of signature cache to <n> MiB (default: %u). The value may be given in megabytes or with unit (B, KiB, MiB, GiB).",
                      DEFAULT_MAX_SIG_CACHE_SIZE));
        strUsage += HelpMessageOpt(
            "-maxinvalidsigcachesize=<n>",
            strprintf("Limit size of invalid signature cache to <n> MiB (default: %u). The value may be given in megabytes or with unit (B, KiB, MiB, GiB).",
                      DEFAULT_INVALID_MAX_SIG_CACHE_SIZE));
        strUsage += HelpMessageOpt(
            "-maxscriptcachesize=<n>",
            strprintf("Limit size of script cache to <n> MiB (default: %u). The value may be given in megabytes or with unit (B, KiB, MiB, GiB).",
                      DEFAULT_MAX_SCRIPT_CACHE_SIZE));
        strUsage += HelpMessageOpt(
            "-maxtipage=<n>",
            strprintf("Maximum tip age in seconds to consider node in initial "
                      "block download (default: %u)",
                      DEFAULT_MAX_TIP_AGE));
    }
    strUsage += HelpMessageOpt(
        "-maxtxfee=<amt>",
        strprintf(_("Maximum total fees (in %s) to use in a single wallet "
                    "transaction or raw transaction; setting this too low may "
                    "abort large transactions (default: %s)"),
                  CURRENCY_UNIT, FormatMoney(DEFAULT_TRANSACTION_MAXFEE)));
    strUsage += HelpMessageOpt(
        "-printtoconsole",
        _("Send trace/debug info to console instead of bitcoind.log file"));
    strUsage += HelpMessageOpt("-shrinkdebugfile",
                               _("Shrink bitcoind.log file on client startup "
                                 "(default: 1 when no -debug)"));

    AppendParamsHelpMessages(strUsage, showDebug);

    strUsage += HelpMessageGroup(_("Node relay options:"));
    strUsage +=
        HelpMessageOpt("-excessiveblocksize=<n>",
                       strprintf(_("Set the maximum block size in bytes we will accept "
                                   "from any source. This is the effective block size "
                                   "hard limit and it is a required parameter (0 = unlimited). "
                                   "The value may be given in bytes or with unit (B, kB, MB, GB).")));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-acceptnonstdtxn",
            strprintf(
                "Relay and mine \"non-standard\" transactions (%sdefault: %u)",
                "testnet/regtest only; ",
                defaultChainParams->RequireStandard()));
        strUsage += HelpMessageOpt(
                "-mindebugrejectionfee",
                strprintf(
                        "For testing on testnet/regtest only;"));
        strUsage += HelpMessageOpt(
            "-acceptnonstdoutputs",
            strprintf(
                "Relay and mine transactions that create or consume non standard"
                " outputs after Genesis is activated. (default: %u)",
                config.GetAcceptNonStandardOutput(true)));

    }
    strUsage += HelpMessageOpt(
        "-datacarrier",
        strprintf(_("Relay and mine data carrier transactions (default: %d)"),
                  DEFAULT_ACCEPT_DATACARRIER));
    strUsage += HelpMessageOpt(
        "-datacarriersize",
        strprintf(_("Maximum size of data in data carrier transactions we "
                    "relay and mine (default: %u). The value may be given in bytes or with unit (B, kB, MB, GB)."),
                  DEFAULT_DATA_CARRIER_SIZE));
    strUsage += HelpMessageOpt(
        "-maxstackmemoryusageconsensus",
        strprintf(_("Set maximum stack memory usage in bytes used for script verification "
                    "we're willing to accept from any source (0 = unlimited) "
                    "after Genesis is activated (consensus level). This is a required parameter. "
                    "The value may be given in bytes or with unit (B, kB, MB, GB).")));
    strUsage += HelpMessageOpt(
        "-maxstackmemoryusagepolicy",
        strprintf(_("Set maximum stack memory usage used for script verification "
                    "we're willing to relay/mine in a single transaction "
                    "(default: %u MB, 0 = unlimited) "
                    "after Genesis is activated (policy level). The value may be given in bytes or with unit (B, kB, MB, GB). "
                    "Must be less or equal to -maxstackmemoryusageconsensus."),
                  DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS/ONE_MEGABYTE));
    strUsage +=
        HelpMessageOpt("-maxopsperscriptpolicy=<n>",
            strprintf(_("Set maximum number of non-push operations "
                        "we're willing to relay/mine per script (default: unlimited, 0 = unlimited), after Genesis is activated")));
    strUsage += HelpMessageOpt(
        "-maxtxsigopscountspolicy=<n>",
        strprintf("Set maximum allowed number of signature operations we're willing to relay/mine in a single transaction (default: unlimited, 0 = unlimited) after Genesis is activated."));


    strUsage += HelpMessageOpt(
        "-maxstdtxvalidationduration=<n>",
        strprintf(
            _("Set the single standard transaction validation duration threshold in"
              " milliseconds after which the standard transaction validation will"
              " terminate with error and the transaction is not accepted to"
              " mempool (min 1ms, default: %dms)"),
            DEFAULT_MAX_STD_TXN_VALIDATION_DURATION.count()));

    strUsage += HelpMessageOpt(
        "-maxnonstdtxvalidationduration=<n>",
        strprintf(
            _("Set the single non-standard transaction validation duration threshold in"
              " milliseconds after which the non-standard transaction validation will"
              " terminate with error and the transaction is not accepted to"
              " mempool (min 10ms, default: %dms)"),
            DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION.count()));

    strUsage += HelpMessageOpt(
        "-maxtxchainvalidationbudget=<n>",
        strprintf(
            _("Set the upper limit of unused validation time to add to the next transaction validated in the chain "
              "(min 0ms, default: %dms)"),
            DEFAULT_MAX_TXN_CHAIN_VALIDATION_BUDGET.count()));

    strUsage +=
        HelpMessageOpt("-validationclockcpu",
                       strprintf(_("Use CPU time instead of wall clock time for validation duration measurement (default: %d)"
#ifndef BOOST_CHRONO_HAS_THREAD_CLOCK
                                   " WARNING: this platform does not have CPU clock."
#endif
                                   ),
                                 DEFAULT_VALIDATION_CLOCK_CPU));

    strUsage +=
        HelpMessageOpt("-maxtxsizepolicy=<n>",
            strprintf(_("Set maximum transaction size in bytes we relay and mine (default: %u MB, min: %u B, 0 = unlimited) after Genesis is activated. "
                        "The value may be given in bytes or with unit (B, kB, MB, GB)."),
                DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS/ONE_MEGABYTE, MAX_TX_SIZE_POLICY_BEFORE_GENESIS));

    strUsage +=
            HelpMessageOpt("-minconsolidationfactor=<n>",
                           strprintf(_("Set minimum ratio between sum of utxo scriptPubKey sizes spent in a consolidation transaction, to the corresponding sum of output scriptPubKey sizes. "
					   "The ratio between number of consolidation transaction inputs to the number of outputs also needs to be greater or equal to the minimum consolidation factor (default: %u). "
				       "A value of 0 disables free consolidation transactions"),
                                     DEFAULT_MIN_CONSOLIDATION_FACTOR));
    strUsage +=
            HelpMessageOpt("-maxconsolidationinputscriptsize=<n>",
                           strprintf(_("This number is the maximum length for a scriptSig input in a consolidation txn (default: %u). The value may be given in bytes or with unit (B, kB, MB, GB)."),
                                     DEFAULT_MAX_CONSOLIDATION_INPUT_SCRIPT_SIZE));

    strUsage +=
            HelpMessageOpt("-minconfconsolidationinput=<n>",
                           strprintf(_("Minimum number of confirmations of inputs spent by consolidation transactions (default: %u). "),
                                     DEFAULT_MIN_CONF_CONSOLIDATION_INPUT));

    strUsage +=
            HelpMessageOpt("-minconsolidationinputmaturity=<n>",
                           strprintf(_("(DEPRECATED: This option will be removed, use -minconfconsolidationinput instead) Minimum number of confirmations of inputs spent by consolidation transactions (default: %u). "),
                                     DEFAULT_MIN_CONF_CONSOLIDATION_INPUT));

    strUsage +=
            HelpMessageOpt("-acceptnonstdconsolidationinput=<n>",
                           strprintf(_("Accept consolidation transactions spending non standard inputs (default: %u). "),
                                     DEFAULT_ACCEPT_NON_STD_CONSOLIDATION_INPUT));

    strUsage += HelpMessageOpt(
        "-maxscriptsizepolicy",
        strprintf("Set maximum script size in bytes we're willing to relay/mine per script after Genesis is activated. "
            "(default: %u, 0 = unlimited). The value may be given in bytes or with unit (B, kB, MB, GB).",
            DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS));

    strUsage += HelpMessageOpt(
        "-maxscriptnumlengthpolicy=<n>",
        strprintf("Set maximum allowed number length we're willing to relay/mine in scripts (default: %d, 0 = unlimited) after Genesis is activated. "
            "The value may be given in bytes or with unit (B, kB, MB, GB).",
            DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS));

    strUsage += HelpMessageOpt(
        "-softconsensusfreezeduration",
        strprintf("Set for how many blocks a block that contains transaction spending "
                  "consensus frozen TXO will remain frozen before it auto unfreezes "
                  "due to the amount of child blocks that were mined after it "
                  "(default: %u; note: 0 - soft consensus freeze duration is "
                  "disabled and block is frozen indefinitely).",
                  DEFAULT_SOFT_CONSENSUS_FREEZE_DURATION));

    strUsage += HelpMessageOpt(
        "-enableassumewhitelistedblockdepth=<n>",
        strprintf("Assume confiscation transaction to be whitelisted if it is in block that is at least as deep under tip as specified by option 'assumewhitelistedblockdepth'. (default: %d)",
            DEFAULT_ENABLE_ASSUME_WHITELISTED_BLOCK_DEPTH));

    strUsage += HelpMessageOpt(
        "-assumewhitelistedblockdepth=<n>",
        strprintf("Set minimal depth of block under tip at which confiscation transaction is assumed to be whitelisted. (default: %d)",
            DEFAULT_ASSUME_WHITELISTED_BLOCK_DEPTH));

    strUsage += HelpMessageGroup(_("Block creation options:"));
    strUsage += HelpMessageOpt(
        "-blockmaxsize=<n>",
        strprintf(_("Set maximum block size in bytes we will mine. "
                    "Size of the mined block will never exceed the maximum block size we will accept (-excessiveblocksize). "
                    "The value may be given in bytes or with unit (B, kB, MB, GB). "
                    "If not specified, the following defaults are used: "
                    "Mainnet: %d MB before %s and %d MB after, "
                    "Testnet: %d MB before %s and %d MB after."),
                    defaultChainParams->GetDefaultBlockSizeParams().maxGeneratedBlockSizeBefore / ONE_MEGABYTE,
                    DateTimeStrFormat("%Y-%m-%d %H:%M:%S", defaultChainParams->GetDefaultBlockSizeParams().blockSizeActivationTime),
                    defaultChainParams->GetDefaultBlockSizeParams().maxGeneratedBlockSizeAfter/ONE_MEGABYTE,
                    testnetChainParams->GetDefaultBlockSizeParams().maxGeneratedBlockSizeBefore / ONE_MEGABYTE,
                    DateTimeStrFormat("%Y-%m-%d %H:%M:%S", testnetChainParams->GetDefaultBlockSizeParams().blockSizeActivationTime),
                    testnetChainParams->GetDefaultBlockSizeParams().maxGeneratedBlockSizeAfter / ONE_MEGABYTE
                    ));
    strUsage += HelpMessageOpt(
        "-minminingtxfee=<amt>",
        strprintf(_("Set lowest fee rate (in %s/kB) for transactions to be "
                    "included in block creation. This is a mandatory setting"),
                  CURRENCY_UNIT));

    strUsage +=
        HelpMessageOpt("-detectselfishmining=<n>",
                       strprintf(_("Detect selfish mining (default: %u). "),
                                 DEFAULT_DETECT_SELFISH_MINING));
    strUsage +=
        HelpMessageOpt("-selfishtxpercentthreshold=<n>",
        strprintf(_("Set percentage threshold of number of txs in mempool "
                    "that are not included in received block for "
                    "the block to be classified as selfishly mined (default: %u). "),
                        DEFAULT_SELFISH_TX_THRESHOLD_IN_PERCENT));
    strUsage += HelpMessageOpt("-minblockmempooltimedifferenceselfish=<n>",
        strprintf(_("Set lowest time difference in sec between the last block and last mempool "
                    "transaction for the block to be classified as selfishly mined (default: %ds)"),
                    DEFAULT_MIN_BLOCK_MEMPOOL_TIME_DIFFERENCE_SELFISH));

    strUsage += HelpMessageOpt(
        "-invalidateblock=<hash>",
        strprintf(_("Permanently marks an existing block as invalid as if it violated "
                    "a consensus rule (same as InvalidateBlock RPC function). "
                    "If specified block header was not received yet, the header will be "
                    "ignored when it is received from a peer. "
                    "This option can be specified multiple times.")));

    strUsage += HelpMessageOpt(
        "-banclientua=<ua>",
        strprintf(_("Ban clients whose User Agent contains specified string (case insensitive). "
                    "This option can be specified multiple times.")));

    strUsage += HelpMessageOpt(
        "-allowclientua=<ua>",
        strprintf(_("Allow clients whose User Agent equals specified string (case insensitive). "
                    "This option can be specified multiple times and has precedence over '-banclientua'.")));

    if (showDebug) {
        strUsage +=
            HelpMessageOpt("-blockversion=<n>",
                           "Override block version to test forking scenarios");
        strUsage +=
            HelpMessageOpt("-blockcandidatevaliditytest",
                           strprintf(_("Perform validity test on block candidates. Defaults: "
                           "Mainnet: %d, Testnet: %d"), defaultChainParams->TestBlockCandidateValidity(), testnetChainParams->TestBlockCandidateValidity()));
        strUsage +=
            HelpMessageOpt("-disablebip30checks",
                           "Disable BIP30 checks when connecting a block. "
                           "This flag can not be set on the mainnet.");
    }

    /** Block assembler */
    strUsage += HelpMessageOpt(
        "-blockassembler=<type>",
        strprintf(_("Set the type of block assembler to use for mining. Supported options are "
                    "JOURNALING. (default: %s)"),
                  enum_cast<std::string>(mining::DEFAULT_BLOCK_ASSEMBLER_TYPE).c_str()));
    strUsage += HelpMessageOpt(
        "-jbamaxtxnbatch=<max batch size>",
        strprintf(_("Set the maximum number of transactions processed in a batch by the journaling block assembler "
                "(default: %d)"), mining::JournalingBlockAssembler::DEFAULT_MAX_SLOT_TRANSACTIONS)
    );
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-jbafillafternewblock",
            strprintf(_("After a new block has been found it can take a short while for the journaling block assembler "
                        "to catch up and return a new candidate containing every transaction in the mempool. "
                        "If this flag is 1, calling getminingcandidate will wait until the JBA has caught up "
                        "and always return a candidate with every available transaction. If it is 0, calls to "
                        "getminingcandidate will always return straight away but may occasionally only contain a "
                        "subset of the available transactions from the mempool (default: %d)"),
                mining::JournalingBlockAssembler::DEFAULT_NEW_BLOCK_FILL)
        );
        strUsage += HelpMessageOpt(
            "-jbarunfrequency",
            strprintf(_("How frequently (in milliseconds) does the jounaling block assembler background thread "
                        "run to sweep up newly seen transactions and add them to the latest block template "
                        "(default: %dms)"),
                mining::JournalingBlockAssembler::DEFAULT_RUN_FREQUENCY_MILLIS)
        );
    }
    strUsage += HelpMessageOpt(
        "-jbathrottlethreshold",
        strprintf(_("To prevent the appearance of selfish mining when a block template becomes full, "
                    "the journaling block assembler will start to throttle back the rate at which it "
                    "adds new transactions from the journal to the next block template when the block "
                    "template reaches this percent full (default: %d%%)"),
            mining::JournalingBlockAssembler::DEFAULT_THROTTLE_THRESHOLD)
    );

    strUsage += HelpMessageGroup(_("RPC client/server options:"));
    strUsage += HelpMessageOpt("-server",
                               _("Accept command line and JSON-RPC commands"));
    strUsage += HelpMessageOpt(
        "-rest", strprintf(_("Accept public REST requests (default: %d)"),
                           DEFAULT_REST_ENABLE));
    strUsage += HelpMessageOpt(
        "-rpcbind=<addr>",
        _("Bind to given address to listen for JSON-RPC connections. Use "
          "[host]:port notation for IPv6. This option can be specified "
          "multiple times (default: bind to all interfaces)"));
    strUsage +=
        HelpMessageOpt("-rpccookiefile=<loc>",
                       _("Location of the auth cookie (default: data dir)"));
    strUsage += HelpMessageOpt("-rpcuser=<user>",
                               _("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>",
                               _("Password for JSON-RPC connections"));
    strUsage += HelpMessageOpt(
        "-rpcauth=<userpw>",
        _("Username and hashed password for JSON-RPC connections. The field "
          "<userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical "
          "python script is included in share/rpcuser. The client then "
          "connects normally using the "
          "rpcuser=<USERNAME>/rpcpassword=<PASSWORD> pair of arguments. This "
          "option can be specified multiple times"));
    strUsage += HelpMessageOpt(
        "-rpcport=<port>",
        strprintf(_("Listen for JSON-RPC connections on <port> (default: %u or "
                    "testnet: %u)"),
                  defaultBaseParams->RPCPort(), testnetBaseParams->RPCPort()));
    strUsage += HelpMessageOpt(
        "-rpcallowip=<ip>",
        _("Allow JSON-RPC connections from specified source. Valid for <ip> "
          "are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. "
          "1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This "
          "option can be specified multiple times"));
    strUsage += HelpMessageOpt(
        "-magicbytes=<hexcode>",
        _("Allow users to split the test net by changing the magicbytes. "
          "This option only work on a network different than mainnet. "
          "default : 0f0f0f0f"));
    strUsage += HelpMessageOpt(
        "-rpcthreads=<n>",
        strprintf(
            _("Set the number of threads to service RPC calls (default: %d)"),
            DEFAULT_HTTP_THREADS));
    strUsage += HelpMessageOpt(
        "-rpccorsdomain=value",
        "Domain from which to accept cross origin requests (browser enforced)");
    strUsage += HelpMessageOpt("-rpcwebhookclientnumthreads=<n>",
        strprintf(_("Number of threads available for submitting HTTP requests to webhook endpoints. (default: %u, maximum: %u)"),
            rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS, rpc::client::WebhookClientDefaults::MAX_NUM_THREADS));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-rpcworkqueue=<n>", strprintf("Set the depth of the work queue to "
                                           "service RPC calls (default: %d)",
                                           DEFAULT_HTTP_WORKQUEUE));
        strUsage += HelpMessageOpt(
            "-rpcservertimeout=<n>",
            strprintf("Timeout during HTTP requests (default: %d)",
                      DEFAULT_HTTP_SERVER_TIMEOUT));
    }
     strUsage += HelpMessageOpt(
        "-invalidcsinterval=<n>",
         strprintf("Set the time limit on the reception of invalid message checksums from a single node in milliseconds (default: %dms)",
            DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS)) ;

         strUsage += HelpMessageOpt(
        "-invalidcsfreq=<n>",
         strprintf("Set the limit on the number of invalid checksums received over a given time period from a single node  (default: %d)",
            DEFAULT_INVALID_CHECKSUM_FREQUENCY)) ;

    /** COrphanTxns */
    strUsage += HelpMessageGroup(_("Orphan txns config :"));
    strUsage += HelpMessageOpt(
        "-blockreconstructionextratxn=<n>",
        strprintf(_("Extra transactions to keep in memory for compact block "
                    "reconstructions (default: %u)"),
            COrphanTxns::DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN));
    strUsage += HelpMessageOpt(
        "-maxorphantxsize=<n>",
        strprintf(_("Keep at most <n> MB of unconnectable "
                    "transactions in memory (default: %u MB). The value may be given in megabytes or with unit (B, kB, MB, GB)."),
          COrphanTxns::DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE/ONE_MEGABYTE));
    strUsage += HelpMessageOpt(
        "-maxorphansinbatchpercent=<n>",
        strprintf(_("Maximal number of orphans scheduled for re-validation as percentage of max batch size. "
                    "(1 to 100, default:%lu)"),
            COrphanTxns::DEFAULT_MAX_PERCENTAGE_OF_ORPHANS_IN_BATCH));
    strUsage += HelpMessageOpt(
        "-maxinputspertransactionoutoffirstlayerorphan=<n>",
        strprintf(_("Maximal number of inputs of a non-first-layer transaction that can be scheduled for re-validation. "
                    "(default:%lu)"),
            COrphanTxns::DEFAULT_MAX_INPUTS_OUTPUTS_PER_TRANSACTION));

    /** TxnValidator */
    strUsage += HelpMessageGroup(_("TxnValidator options:"));
    strUsage += HelpMessageOpt(
        "-blockvalidationtxbatchsize=<n>",
        strprintf(_("Set the minimum batch size for groups of txns to be validated in parallel during block validation "
                    "(default: %d)"),
            DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE));
    strUsage += HelpMessageOpt(
        "-numstdtxvalidationthreads=<n>",
        strprintf(_("Set the number of 'High' priority threads used to validate standard txns (dynamically calculated default: %d)"),
            GetNumHighPriorityValidationThrs())) ;
    strUsage += HelpMessageOpt(
        "-numnonstdtxvalidationthreads=<n>",
        strprintf(_("Set the number of 'Low' priority threads used to validate non-standard txns (dynamically calculated default: %d)"),
            GetNumLowPriorityValidationThrs())) ;
    strUsage += HelpMessageOpt(
        "-maxstdtxnsperthreadratio=<n>",
        strprintf(_("Set the max ratio for a number of standard txns per 'High' priority thread (default: %d)"),
            DEFAULT_MAX_STD_TXNS_PER_THREAD_RATIO)) ;
    strUsage += HelpMessageOpt(
        "-maxnonstdtxnsperthreadratio=<n>",
        strprintf(_("Set the max ratio for a number of non-standard txns per 'Low' priority thread (default: %d)"),
            DEFAULT_MAX_NON_STD_TXNS_PER_THREAD_RATIO)) ;
    strUsage += HelpMessageOpt(
        "-txnvalidationasynchrunfreq=<n>",
        strprintf("Set run frequency in asynchronous mode (default: %dms)",
            CTxnValidator::DEFAULT_ASYNCH_RUN_FREQUENCY_MILLIS)) ;
    // The message below assumes that default strategy is TOPO_SORT, therefore we assert here.
    static_assert(DEFAULT_PTV_TASK_SCHEDULE_STRATEGY == PTVTaskScheduleStrategy::TOPO_SORT);
    strUsage += HelpMessageOpt(
            "-txnvalidationschedulestrategy=<strategy>",
            "Set task scheduling strategy to use in parallel transaction validation."
                      "Available strategies: CHAIN_DETECTOR (legacy), TOPO_SORT (default)");
    strUsage += HelpMessageOpt(
        "-maxtxnvalidatorasynctasksrunduration=<n>",
        strprintf("Set the maximum validation duration for async tasks in a single run (default: %dms)",
            CTxnValidator::DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION.count())) ;
    strUsage += HelpMessageOpt(
        "-maxcoinsviewcachesize=<n>",
        _("Set the maximum cumulative size of accepted transaction inputs inside coins cache (default: unlimited -> 0). "
            "The value may be given in bytes or with unit (B, kB, MB, GB)."));
    strUsage += HelpMessageOpt(
        "-maxcoinsprovidercachesize=<n>",
        strprintf(_("Set soft maximum limit of cached coin tip buffer size (default: %d GB, minimum: %d MB). "
            "The value may be given in bytes or with unit (B, kB, MB, GB)."),
            DEFAULT_COINS_PROVIDER_CACHE_SIZE / ONE_GIGABYTE,
            MIN_COINS_PROVIDER_CACHE_SIZE / ONE_MEGABYTE));
    strUsage += HelpMessageOpt(
        "-maxcoinsdbfiles=<n>",
        strprintf(_("Set maximum number of files used by coins leveldb (default: %d). "),
                  CoinsDB::MaxFiles::Default().maxFiles));
    strUsage += HelpMessageOpt(
        "-txnvalidationqueuesmaxmemory=<n>",
        strprintf("Set the maximum memory usage for the transaction queues in MB (default: %d). The value may be given in megabytes or with unit (B, kB, MB, GB).",
            CTxnValidator::DEFAULT_MAX_MEMORY_TRANSACTION_QUEUES)) ;

    strUsage += HelpMessageOpt(
        "-maxpubkeyspermultisigpolicy=<n>",
        strprintf("Set maximum allowed number of public keys we're willing to relay/mine in a single CHECK_MULTISIG(VERIFY) operation (default: unlimited, 0 = unlimited), after Genesis is activated"));

    strUsage += HelpMessageOpt(
        "-maxgenesisgracefulperiod=<n>",
        strprintf(_("Set maximum allowed number of blocks for Genesis graceful period (default: %d) where nodes will not be banned "
                    "for violating Genesis rules in case the calling node is not yet on Genesis height and vice versa. "
                    "Seting 0 will disable Genesis graceful period. Genesis graceful period range :"
                    "(GENESIS_ACTIVATION_HEIGHT - n |...| GENESIS_ACTIVATION_HEIGHT |...| GENESIS_ACTIVATION_HEIGHT + n)"),
            DEFAULT_GENESIS_GRACEFULL_ACTIVATION_PERIOD));

    strUsage += HelpMessageGroup(_("Invalid transactions sink options:"));
    std::string availableSinks = StringJoin(", ", config.GetAvailableInvalidTxSinks());
    strUsage += HelpMessageOpt(
        "-invalidtxsink=<sink>",
        strprintf(_("Set destination for dumping invalid transactions. Specify separately for every sink you want to include. Available sinks:%s, (no sink by default)"),
            availableSinks));

    strUsage += HelpMessageOpt(
        "-invalidtxfilemaxdiskusage=<n>",
        strprintf(_("Set maximal disk usage for dumping invalid transactions when using FILE for the sink."
            " In megabytes. (default: %dMB)"
            " The value may be given in megabytes or with unit (B, kB, MB, GB)."),
            CInvalidTxnPublisher::DEFAULT_FILE_SINK_DISK_USAGE / ONE_MEGABYTE));

    // The message below assumes that default policy is IGNORE_NEW
    static_assert(CInvalidTxnPublisher::DEFAULT_FILE_SINK_EVICTION_POLICY == InvalidTxEvictionPolicy::IGNORE_NEW);
    strUsage += HelpMessageOpt(
        "-invalidtxfileevictionpolicy=<policy>",
        strprintf(_("Set policy which is applied when disk usage limits are reached when using FILE for the sink. IGNORE_NEW or DELETE_OLD (default: IGNORE_NEW)")));

#if ENABLE_ZMQ
    strUsage += HelpMessageOpt(
        "-invalidtxzmqmaxmessagesize=<n>",
        strprintf(_("Set maximal message size for publishing invalid transactions using ZMQ, in megabytes. (default: %dMB)"
            " The value may be given in megabytes or with unit (B, kB, MB, GB)."),
            CInvalidTxnPublisher::DEFAULT_ZMQ_SINK_MAX_MESSAGE_SIZE / ONE_MEGABYTE));
#endif

      strUsage += HelpMessageOpt(
        "-maxprotocolrecvpayloadlength=<n>",
        strprintf("Set maximum protocol recv payload length you are willing to accept in bytes (default %d). Value should be bigger than legacy protocol payload length: %d B "
                  "and smaller than: %d B.", DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH, LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH, ONE_GIGABYTE));

      strUsage += HelpMessageOpt(
        "-recvinvqueuefactor=<n>",
        strprintf("Set maximum number of full size inventory messages that we can store for each peer (default %d). Inventory message size can be set with -maxprotocolrecvpayloadlength. "
          "Value should be an integer between %d and %d )", DEFAULT_RECV_INV_QUEUE_FACTOR, MIN_RECV_INV_QUEUE_FACTOR, MAX_RECV_INV_QUEUE_FACTOR)); 

    /** Double-Spend detection/reporting */
    strUsage += HelpMessageGroup(_("Double-Spend detection options:"));
    strUsage += HelpMessageOpt(
        "-dsnotifylevel",
        strprintf(_("Set how this node should handle double-spend notification sending. The options are: 0 Send no notifications, "
                    "1 Send notifications only for standard transactions, 2 Send notifications for all transactions. (default: %d)"),
            static_cast<int>(DSAttemptHandler::DEFAULT_NOTIFY_LEVEL)));
    strUsage += HelpMessageOpt(
        "-dsendpointfasttimeout=<n>",
        strprintf(_("Timeout in seconds for high priority communications with a double-spend reporting endpoint (default: %u)"),
            rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT));
    strUsage += HelpMessageOpt(
        "-dsendpointslowtimeout=<n>",
        strprintf(_("Timeout in seconds for low priority communications with a double-spend reporting endpoint (default: %u)"),
            rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT));
    strUsage += HelpMessageOpt(
        "-dsendpointslowrateperhour=<n>",
        strprintf(_("The allowable number of timeouts per hour on a rolling basis to a double-spend reporting endpoint before "
                    "we temporarily assume that endpoint is consistently slow and direct all communications for it to the "
                    "slow / low priority queue. Must be between 1 and 60 (default: %u)"),
            DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-dsendpointport=<n>",
            strprintf(_("Port to connect to double-spend reporting endpoint on (default: %u)"),
                rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT));
        strUsage += HelpMessageOpt(
            "-dsendpointblacklistsize=<n>",
            strprintf(_("Limits the maximum number of entries stored in the bad double-spend reporting endpoint server blacklist (default: %u)"),
                DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE));
    }
    strUsage += HelpMessageOpt(
        "-dsendpointskiplist=<list of ips>",
        "A comma separated list of IP addresses for double-spend endpoints we should skip sending notifications to. This can be useful if (for example) "
        "we are running a mAPI node locally which will already be receiving double-spend notification via ZMQ, then we don't need to also send such "
        "notifications via HTTP.");
    strUsage += HelpMessageOpt(
        "-dsendpointmaxcount=<n>",
        strprintf(_("Maximum number of endpoint IPs we will consider notifying per transaction (default: %u)"),
            DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT));
    strUsage += HelpMessageOpt(
        "-dsattempttxnremember=<n>",
        strprintf(_("Limits the maximum number of previous double-spend transactions the node remembers. Setting this high uses more memory and is slower, "
                    "setting it low increases the chances we may unnecessarily process and re-report duplicate double-spent transactions (default: %u)"),
            DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT));
    strUsage += HelpMessageOpt(
        "-dsattemptnumfastthreads=<n>",
        strprintf(_("Number of threads available for processing high priority double-spend notifications. Note that each additional thread also "
            "requires a small amount of disk space for serialising transactions to. (default: %u, maximum: %u)"),
            DSAttemptHandler::DEFAULT_NUM_FAST_THREADS, DSAttemptHandler::MAX_NUM_THREADS));
    strUsage += HelpMessageOpt(
        "-dsattemptnumslowthreads=<n>",
        strprintf(_("Number of threads available for processing low priority double-spend notifications. Note that each additional thread also "
            "requires a small amount of disk space for serialising transactions to. (default: %u, maximum: %u)"),
            DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS, DSAttemptHandler::MAX_NUM_THREADS));
    strUsage += HelpMessageOpt(
        "-dsattemptqueuemaxmemory=<n>",
        strprintf(_("Maximum memory usage for the queue of detected double-spend transactions (default: %uMB). "
                    "The value may be given in megabytes or with unit (B, kB, MB, GB)."),
            DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY));

    strUsage += HelpMessageOpt(
        "-dsdetectedwebhookurl=<url>",
        "URL of a webhook to notify on receipt of a double-spend detected P2P message from another node. For example: "
        "http://127.0.0.1/dsdetected/webhook");
    strUsage += HelpMessageOpt(
        "-dsdetectedwebhookmaxtxnsize=<n>",
        strprintf(_("Maximum size of transaction to forward to the double-spend detected webhook. For double-spent transactions "
                    "above this size only the transaction ID will be reported to the webhook (default: %uMB). "
                    "The value may be given in megabytes or with unit (B, kB, MB, GB)."),
            DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE));

    /** MinerID */
    strUsage += HelpMessageGroup(_("Miner ID database / authenticated connection options:"));
    if(showDebug) {
        strUsage += HelpMessageOpt(
            "-minerid",
            strprintf(_("Enable the building and use of the miner ID database (default: %d)"),
                MinerIdDatabaseDefaults::DEFAULT_MINER_ID_ENABLED));
    }
    strUsage += HelpMessageOpt(
        "-mineridcachesize=<n>",
        strprintf(_("Cache size to use for the miner ID database (default: %uMB, maximum: %uMB). "
            "The value may be given in bytes or with unit (B, kB, MB, GB)."),
            MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE / ONE_MEBIBYTE, MinerIdDatabaseDefaults::MAX_CACHE_SIZE / ONE_MEBIBYTE));
    if(showDebug) {
        strUsage += HelpMessageOpt(
            "-mineridnumtokeep=<n>",
            strprintf(_("Maximum number of old (rotated, expired) miner IDs we will keep in the database (default: %u)"),
                MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP));
    }
    strUsage += HelpMessageOpt(
        "-mineridreputation_m=<n>",
        strprintf(_("Miners who identify themselves using miner ID can accumulate certain priviledges over time by gaining "
            "a good reputation. A good reputation is gained by having mined M of the last N blocks on the current chain. "
            "This parameter sets the M value for that test. (default: %u, maximum %u)"),
            MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M, MinerIdDatabaseDefaults::MAX_MINER_REPUTATION_M));
    strUsage += HelpMessageOpt(
        "-mineridreputation_n=<n>",
        strprintf(_("Miners who identify themselves using miner ID can accumulate certain priviledges over time by gaining "
            "a good reputation. A good reputation is gained by having mined M of the last N blocks on the current chain. "
            "This parameter sets the N value for that test. (default: %u, maximum %u)"),
            MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N, MinerIdDatabaseDefaults::MAX_MINER_REPUTATION_N));
    strUsage += HelpMessageOpt(
        "-mineridreputation_mscale=<n>",
        strprintf(_("Miners who lose their good reputation can in some circumstances recover that reputation, "
            "but at the cost of a temporarily increased M of N block target. This parameter determines how "
            "much to scale the base M value in such cases. (default: %f)"),
            MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR));
    strUsage += HelpMessageOpt("-mineridgeneratorurl=<url>",
        "URL for communicating with the miner ID generator. Required to setup authenticated connections. "
        "For example: http://127.0.0.1:9002");
    strUsage += HelpMessageOpt("-mineridgeneratoralias=<string>",
        "Alias used to identify our current miner ID in the generator. Required to setup authenticated connections.");

    /** Safe mode */
    strUsage += HelpMessageGroup(_("Safe-mode activation options:"));

    strUsage += HelpMessageOpt(
        "-disablesafemode", strprintf("Disable safemode, override a real "
                                        "safe mode event (default: %d)",
                                        DEFAULT_DISABLE_SAFEMODE));
    if (showDebug) {
        strUsage += HelpMessageOpt(
            "-testsafemode",
            strprintf("Force safe mode (default: %d)", DEFAULT_TESTSAFEMODE));
    }
    strUsage += HelpMessageOpt("-safemodewebhookurl=<url>",
        "URL of a webhook to notify if the node enters safe mode. For example: http://127.0.0.1/mywebhook");
    strUsage += HelpMessageOpt("-safemodeminblockdifference=<n>",
        strprintf("Minimum number of blocks that fork should be ahead (if positive) or behind (if negative) of active tip to enter safe mode "
            "(default: %d)", SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE));
    strUsage += HelpMessageOpt("-safemodemaxforkdistance=<n>",
        strprintf("Maximum distance of forks last common block from current active tip to enter safe mode "
            "(default: %d)", SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE));
    strUsage += HelpMessageOpt("-safemodeminforklength=<n>",
        strprintf("Minimum length of valid fork to enter safe mode "
            "(default: %d)", SAFE_MODE_DEFAULT_MIN_FORK_LENGTH));

    return strUsage;
}

std::string LicenseInfo() {
    const std::string URL_SOURCE_CODE =
        "<https://github.com/bitcoin-sv/bitcoin-sv>";
    const std::string URL_WEBSITE = "<https://bitcoinsv.io>";

    return CopyrightHolders(
               strprintf(_("Copyright (C) %i-%i"), 2009, COPYRIGHT_YEAR) +
               " ") +
           "\n" + "\n" +
           strprintf(_("Please contribute if you find %s useful. "
                       "Visit %s for further information about the software."),
                     PACKAGE_NAME, URL_WEBSITE) +
           "\n" + strprintf(_("The source code is available from %s."),
                            URL_SOURCE_CODE) +
           "\n" + "\n" + _("This is experimental software.") + "\n" +
           strprintf(_("Distributed under the Open BSV software license, see the "
                       "accompanying file %s"),
                     "LICENSE") +
           "\n" + "\n" +
           strprintf(_("This product includes software developed by the "
                       "OpenSSL Project for use in the OpenSSL Toolkit %s and "
                       "cryptographic software written by Eric Young and UPnP "
                       "software written by Thomas Bernard."),
                     "<https://www.openssl.org>") +
           "\n";
}

static void BlockNotifyCallback(bool initialSync,
                                const CBlockIndex *pBlockIndex) {
    if (initialSync || !pBlockIndex) return;

    std::string strCmd = gArgs.GetArg("-blocknotify", "");

    boost::replace_all(strCmd, "%s", pBlockIndex->GetBlockHash().GetHex());
    boost::thread t(runCommand, strCmd); // thread runs free
}

static bool fHaveGenesis = false;
static boost::mutex cs_GenesisWait;
static CConditionVariable condvar_GenesisWait;

static void BlockNotifyGenesisWait(bool, const CBlockIndex *pBlockIndex) {
    if (pBlockIndex != nullptr) {
        {
            boost::unique_lock<boost::mutex> lock_GenesisWait(cs_GenesisWait);
            fHaveGenesis = true;
        }
        condvar_GenesisWait.notify_all();
    }
}

struct CImportingNow {
    CImportingNow(const CImportingNow&) = delete;
    CImportingNow& operator=(const CImportingNow&) = delete;
    CImportingNow(CImportingNow&&) = delete;
    CImportingNow& operator=(CImportingNow&&) = delete;
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};

// If we're using -prune with -reindex, then delete block files that will be
// ignored by the reindex.  Since reindexing works by starting at block file 0
// and looping until a blockfile is missing, do the same here to delete any
// later block files after a gap. Also delete all rev files since they'll be
// rewritten by the reindex anyway. This ensures that vinfoBlockFile is in sync
// with what's actually on disk by the time we start downloading, so that
// pruning works correctly.
void CleanupBlockRevFiles() {
    std::map<std::string, fs::path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for "
              "-reindex with -prune\n");
    fs::path blocksdir = GetDataDir() / "blocks";
    for (fs::directory_iterator it(blocksdir); it != fs::directory_iterator();
         it++) {
        if (is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8, 4) == ".dat") {
            if (it->path().filename().string().substr(0, 3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3, 5)] =
                    it->path();
            else if (it->path().filename().string().substr(0, 3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by keeping
    // a separate counter. Once we hit a gap (or if 0 doesn't exist) start
    // removing block files.
    int nContigCounter = 0;
    for (const auto& item : mapBlockFiles) {
        if (atoi(item.first) == nContigCounter) {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}

/* shutdownToken must be passed by value to prevent access violation because
 * "import_files" thread can have longer life span than shutdownToken presented with a reference.
 */
void ThreadImport(const Config &config, std::vector<fs::path> vImportFiles, const task::CCancellationToken shutdownToken) {
    RenameThread("loadblk");

    {
        CImportingNow imp;

        // -reindex
        if (fReindex) {
            ReindexAllBlockFiles(config, pblocktree, fReindex);
        }

        // hardcoded $DATADIR/bootstrap.dat
        fs::path pathBootstrap = GetDataDir() / "bootstrap.dat";
        if (fs::exists(pathBootstrap)) {
            UniqueCFile file{ fsbridge::fopen(pathBootstrap, "rb") };
            if (file) {
                fs::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
                LogPrintf("Importing bootstrap.dat...\n");
                LoadExternalBlockFile(config, std::move(file));
                RenameOver(pathBootstrap, pathBootstrapOld);
            } else {
                LogPrintf("Warning: Could not open bootstrap file %s\n",
                          pathBootstrap.string());
            }
        }

        // -loadblock=
        for (const fs::path &path : vImportFiles) {
            UniqueCFile file{ fsbridge::fopen(path, "rb") };
            if (file) {
                LogPrintf("Importing blocks file %s...\n", path.string());
                LoadExternalBlockFile(config, std::move(file));
            } else {
                LogPrintf("Warning: Could not open blocks file %s\n",
                          path.string());
            }
        }

        // scan for better chains in the block chain database, that are not yet
        // connected in the active best chain

        // dummyState is used to report errors, not block related invalidity
        // (see description of ActivateBestChain)
        CValidationState dummyState;
        mining::CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(mining::JournalUpdateReason::INIT) };
        auto source = task::CCancellationSource::Make();
        if (!ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), shutdownToken), config, dummyState, changeSet)) {
            LogPrintf("Failed to connect best block\n");
            StartShutdown();
        }

        if (gArgs.GetBoolArg("-stopafterblockimport",
                             DEFAULT_STOPAFTERBLOCKIMPORT)) {
            LogPrintf("Stopping after block import\n");
            StartShutdown();
        }
    } // End scope of CImportingNow

    {
        LOCK(cs_main);
        CheckSafeModeParameters(config, nullptr);
    }

    if (gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        mempool.LoadMempool(config, shutdownToken);
        mempool.ResumeSanityCheck();
        fDumpMempoolLater = !shutdownToken.IsCanceled();
    }
}

/** Sanity checks
 *  Ensure that Bitcoin is running in a usable environment with all
 *  necessary library support.
 */
bool InitSanityCheck(void) {
    if (!ECC_InitSanityCheck()) {
        InitError(
            "Elliptic curve cryptography sanity check failure. Aborting.");
        return false;
    }

    if (!glibc_sanity_test() || !glibcxx_sanity_test()) {
        return false;
    }

    if (!Random_SanityCheck()) {
        InitError("OS cryptographic RNG sanity check failure. Aborting.");
        return false;
    }

    return true;
}

static bool AppInitServers(Config &config, boost::thread_group &threadGroup) {
    RPCServer::OnStarted(&OnRPCStarted);
    RPCServer::OnStopped(&OnRPCStopped);
    RPCServer::OnPreCommand(&OnRPCPreCommand);
    if (!InitHTTPServer(config)) return false;
    if (!StartRPC()) return false;
    if (!StartHTTPRPC()) return false;
    if (gArgs.GetBoolArg("-rest", DEFAULT_REST_ENABLE) && !StartREST())
        return false;
    if (!StartHTTPServer()) return false;
    return true;
}

// Parameter interaction based on rules
void InitParameterInteraction() {
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified.
    if (gArgs.IsArgSet("-bind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf(
                "%s: parameter interaction: -bind set -> setting -listen=1\n",
                __func__);
    }
    if (gArgs.IsArgSet("-whitebind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting "
                      "-listen=1\n",
                      __func__);
    }

    if (gArgs.IsArgSet("-connect")) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen
        // by default.
        if (gArgs.SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting "
                      "-dnsseed=0\n",
                      __func__);
        if (gArgs.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting "
                      "-listen=0\n",
                      __func__);
    }

    if (gArgs.IsArgSet("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy
        // server is specified.
        if (gArgs.SoftSetBoolArg("-listen", false))
            LogPrintf(
                "%s: parameter interaction: -proxy set -> setting -listen=0\n",
                __func__);
        // to protect privacy, do not use UPNP when a proxy is set. The user may
        // still specify -listen=1 to listen locally, so don't rely on this
        // happening through -listen below.
        if (gArgs.SoftSetBoolArg("-upnp", false))
            LogPrintf(
                "%s: parameter interaction: -proxy set -> setting -upnp=0\n",
                __func__);
        // to protect privacy, do not discover addresses by default
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting "
                      "-discover=0\n",
                      __func__);
    }

    if (!gArgs.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not map ports or try to retrieve public IP when not listening
        // (pointless)
        if (gArgs.SoftSetBoolArg("-upnp", false))
            LogPrintf(
                "%s: parameter interaction: -listen=0 -> setting -upnp=0\n",
                __func__);
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf(
                "%s: parameter interaction: -listen=0 -> setting -discover=0\n",
                __func__);
    }

    if (gArgs.IsArgSet("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting "
                      "-discover=0\n",
                      __func__);
    }

    // disable whitelistrelay in blocksonly mode
    if (gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY)) {
        if (gArgs.SoftSetBoolArg("-whitelistrelay", false))
            LogPrintf("%s: parameter interaction: -blocksonly=1 -> setting "
                      "-whitelistrelay=0\n",
                      __func__);
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from
    // them in the first place.
    if (gArgs.GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)) {
        if (gArgs.SoftSetBoolArg("-whitelistrelay", true))
            LogPrintf("%s: parameter interaction: -whitelistforcerelay=1 -> "
                      "setting -whitelistrelay=1\n",
                      __func__);
    }
}

static std::string ResolveErrMsg(const char *const optname,
                                 const std::string &strBind) {
    return strprintf(_("Cannot resolve -%s address: '%s'"), optname, strBind);
}

void InitLogging() {
    BCLog::Logger &logger = GetLogger();
    logger.fPrintToConsole = gArgs.GetBoolArg("-printtoconsole", false);
    logger.fLogTimestamps =
        gArgs.GetBoolArg("-logtimestamps", DEFAULT_LOGTIMESTAMPS);
    logger.fLogTimeMicros =
        gArgs.GetBoolArg("-logtimemicros", DEFAULT_LOGTIMEMICROS);

    fLogIPs = gArgs.GetBoolArg("-logips", DEFAULT_LOGIPS);

    LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrintf("%s version %s\n", CLIENT_NAME, FormatFullVersion());
}

namespace { // Variables internal to initialization process only

ServiceFlags nRelevantServices = NODE_NETWORK;
int nMaxConnections;
int nMaxConnectionsFromAddr;
int nMaxOutboundConnections;
int nFD;
ServiceFlags nLocalServices = NODE_NETWORK;
} // namespace

[[noreturn]] static void new_handler_terminate() {
    // Rather than throwing std::bad-alloc if allocation fails, terminate
    // immediately to (try to) avoid chain corruption. Since LogPrintf may
    // itself allocate memory, set the handler directly to terminate first.
    std::set_new_handler(std::terminate);
    LogPrintf("Error: Out of memory. Terminating.\n");

    // The log was successful, terminate now.
    std::terminate();
}

bool AppInitBasicSetup() {
// Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr,
                                             OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
// Enable Data Execution Prevention (DEP)
// Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
// A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
// We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >=
// 0x0601 (Windows 7), which is not correct. Can be removed, when GCCs winbase.h
// is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL(WINAPI * PSETPROCDEPPOL)(DWORD);
    GCC_WARNINGS_PUSH
    GCC_WARNINGS_IGNORE(-Wcast-function-type)
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(
        GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    GCC_WARNINGS_POP
    if (setProcDEPPol != nullptr) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif

    if (!SetupNetworking()) return InitError("Initializing networking failed");

#ifndef WIN32
    if (!gArgs.GetBoolArg("-sysperms", false)) {
        umask(077);
    }

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // Reopen bitcoind.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, nullptr);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client
    // closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#endif

    std::set_new_handler(new_handler_terminate);

    return true;
}

bool AppInitParameterInteraction(ConfigInit &config) {
    const CChainParams &chainparams = config.GetChainParams();
    // Step 2: parameter interactions

    // also see: InitParameterInteraction()

    // if using block pruning, then disallow txindex
    if (gArgs.GetArg("-prune", 0)) {
        if (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX))
            return InitError(_("Prune mode is incompatible with -txindex."));
    }

    // Make sure enough file descriptors are available
    const int nBind = std::max(
        (gArgs.IsArgSet("-bind") ? gArgs.GetArgs("-bind").size() : 0) +
            (gArgs.IsArgSet("-whitebind") ? gArgs.GetArgs("-whitebind").size()
                                          : 0),
        size_t(1));

    const int nUserMaxConnections =
        static_cast<int>(gArgs.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS));
    nMaxConnections = std::max(nUserMaxConnections, 0);

    const int nUserMaxOutboundConnections = gArgs.GetArg(
        "-maxoutboundconnections", DEFAULT_MAX_OUTBOUND_CONNECTIONS);

    // Trim requested connection counts, to fit into system limitations
    if(std::string err; !config.SetMaxAddNodeConnections(gArgs.GetArg("-maxaddnodeconnections", DEFAULT_MAX_ADDNODE_CONNECTIONS), &err)) {
        return InitError(err);
    }
    const uint16_t maxAddNodeConnections { config.GetMaxAddNodeConnections() };
    nMaxConnections =
        std::max(std::min(nMaxConnections,
                          (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS - maxAddNodeConnections)),
                 0);
    nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS + maxAddNodeConnections);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    nMaxConnections =
        std::max(std::min(nFD - MIN_CORE_FILEDESCRIPTORS - maxAddNodeConnections, nMaxConnections), 0);
    if (nMaxConnections < nUserMaxConnections)
        InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, "
                                "because of system limitations."),
                              nUserMaxConnections, nMaxConnections));

    nMaxOutboundConnections = std::clamp(nUserMaxOutboundConnections, 0, nMaxConnections);
    if(nMaxOutboundConnections < nUserMaxOutboundConnections) 
        InitWarning(strprintf("Reducing -maxoutboundconnections from %d to %d, "
                              "because of system limitations",
                              nUserMaxOutboundConnections, nMaxOutboundConnections));

    nMaxConnectionsFromAddr = static_cast<int>(gArgs.GetArg("-maxconnectionsfromaddr", DEFAULT_MAX_CONNECTIONS_FROM_ADDR));
    nMaxConnectionsFromAddr = std::clamp(nMaxConnectionsFromAddr, 0, INT32_MAX);
    if (nMaxConnectionsFromAddr == 0) {
        nMaxConnectionsFromAddr = INT32_MAX;
    }

    // Step 3: parameter-to-internal-flags
    if (gArgs.IsArgSet("-debug")) {
        // Special-case: if -debug=0/-nodebug is set, turn off debugging
        // messages
        const std::vector<std::string> &categories = gArgs.GetArgs("-debug");
        if (find(categories.begin(), categories.end(), std::string("0")) ==
            categories.end()) {
            for (const auto &cat : categories) {
                BCLog::LogFlags flag;
                if (!GetLogCategory(flag, cat)) {
                    InitWarning(
                        strprintf(_("Unsupported logging category %s=%s."),
                                  "-debug", cat));
                }
                GetLogger().EnableCategory(flag);
            }
        }
    }

    // Now remove the logging categories which were explicitly excluded
    if (gArgs.IsArgSet("-debugexclude")) {
        for (const std::string &cat : gArgs.GetArgs("-debugexclude")) {
            BCLog::LogFlags flag;
            if (!GetLogCategory(flag, cat)) {
                InitWarning(strprintf(_("Unsupported logging category %s=%s."),
                                      "-debugexclude", cat));
            }
            GetLogger().DisableCategory(flag);
        }
    }

    // Check for -debugnet
    if (gArgs.GetBoolArg("-debugnet", false))
        InitWarning(
            _("Unsupported argument -debugnet ignored, use -debug=net."));
    // Check for -socks - as this is a privacy risk to continue, exit here
    if (gArgs.IsArgSet("-socks"))
        return InitError(
            _("Unsupported argument -socks found. Setting SOCKS version isn't "
              "possible anymore, only SOCKS5 proxies are supported."));

    if (gArgs.GetBoolArg("-benchmark", false))
        InitWarning(
            _("Unsupported argument -benchmark ignored, use -debug=bench."));

    if (gArgs.GetBoolArg("-whitelistalwaysrelay", false))
        InitWarning(_("Unsupported argument -whitelistalwaysrelay ignored, use "
                      "-whitelistrelay and/or -whitelistforcerelay."));

    if (gArgs.IsArgSet("-blockminsize"))
        InitWarning("Unsupported argument -blockminsize ignored.");

    // Checkmempool and checkblockindex default to true in regtest mode
    int ratio = std::min<int>(
        std::max<int>(
            gArgs.GetArg("-checkmempool",
                         chainparams.DefaultConsistencyChecks() ? 1 : 0),
            0),
        1000000);
    if (ratio != 0) {
        mempool.SetSanityCheck(1.0 / ratio);
    }
    fCheckBlockIndex = gArgs.GetBoolArg("-checkblockindex",
                                        chainparams.DefaultConsistencyChecks());
    fCheckpointsEnabled =
        gArgs.GetBoolArg("-checkpoints", DEFAULT_CHECKPOINTS_ENABLED);

    hashAssumeValid = uint256S(
        gArgs.GetArg("-assumevalid",
                     chainparams.GetConsensus().defaultAssumeValid.GetHex()));
    if (!hashAssumeValid.IsNull())
        LogPrintf("Assuming ancestors of block %s have valid signatures.\n",
                  hashAssumeValid.GetHex());
    else
        LogPrintf("Validating signatures for all blocks.\n");

    if (gArgs.IsArgSet("-minimumchainwork")) {
        const std::string minChainWorkStr =
            gArgs.GetArg("-minimumchainwork", "");
        if (!IsHexNumber(minChainWorkStr)) {
            return InitError(strprintf(
                "Invalid non-hex (%s) minimum chain work value specified",
                minChainWorkStr));
        }
        nMinimumChainWork = UintToArith256(uint256S(minChainWorkStr));
    } else {
        nMinimumChainWork =
            UintToArith256(chainparams.GetConsensus().nMinimumChainWork);
    }
    LogPrintf("Setting nMinimumChainWork=%s\n", nMinimumChainWork.GetHex());
    if (nMinimumChainWork <
        UintToArith256(chainparams.GetConsensus().nMinimumChainWork)) {
        LogPrintf("Warning: nMinimumChainWork set below default value of %s\n",
                  chainparams.GetConsensus().nMinimumChainWork.GetHex());
    }

    // mempool limits
    if (std::string err; !config.SetMaxMempool(
        gArgs.GetArgAsBytes("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE, ONE_MEGABYTE), &err))
    {
        return InitError(err);
    }
    const auto defaultMaxMempoolSizeDisk = int64_t(
        std::ceil(1.0 * config.GetMaxMempool() * DEFAULT_MAX_MEMPOOL_SIZE_DISK_FACTOR
                  / ONE_MEGABYTE));
    if (std::string err; !config.SetMaxMempoolSizeDisk(
        gArgs.GetArgAsBytes("-maxmempoolsizedisk", defaultMaxMempoolSizeDisk, ONE_MEGABYTE), &err))
    {
        return InitError(err);
    }
    if (std::string err; !config.SetMempoolMaxPercentCPFP(
        gArgs.GetArg("-mempoolmaxpercentcpfp", DEFAULT_MEMPOOL_MAX_PERCENT_CPFP), &err))
    {
        return InitError(err);
    }

    // script validation settings
    if(std::string error; !config.SetBlockScriptValidatorsParams(
        gArgs.GetArg("-maxparallelblocks", DEFAULT_SCRIPT_CHECK_POOL_SIZE),
        gArgs.GetArg("-threadsperblock", DEFAULT_SCRIPTCHECK_THREADS),
        gArgs.GetArg("-txnthreadsperblock", DEFAULT_TXNCHECK_THREADS),
        gArgs.GetArg("-scriptvalidatormaxbatchsize", DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE),
        &error))
    {
        return InitError(error);
    }

    if(std::string error; !config.SetMaxConcurrentAsyncTasksPerNode(
        gArgs.GetArg("-maxparallelblocksperpeer", DEFAULT_NODE_ASYNC_TASKS_LIMIT),
        &error))
    {
        return InitError("-maxparallelblocksperpeer: " + error);
    }

    // Configure memory pool expiry
    if (std::string err; !config.SetMemPoolExpiry(
        gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * SECONDS_IN_ONE_HOUR, &err))
    {
        return InitError(err);
    }
    // Configure max orphant Tx size
    if (std::string err; !config.SetMaxOrphanTxSize(
        gArgs.GetArgAsBytes("-maxorphantxsize",
            COrphanTxns::DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE / ONE_MEGABYTE, ONE_MEGABYTE), &err))
    {
        return InitError(err);
    }
    
    if (std::string err; !config.SetMaxOrphansInBatchPercentage(
        gArgs.GetArg("-maxorphansinbatchpercent",
            COrphanTxns::DEFAULT_MAX_PERCENTAGE_OF_ORPHANS_IN_BATCH), &err))
    {
        return InitError(err);
    }

    if (std::string err; !config.SetMaxInputsForSecondLayerOrphan(
        gArgs.GetArgAsBytes("-maxinputspertransactionoutoffirstlayerorphan",
            COrphanTxns::DEFAULT_MAX_INPUTS_OUTPUTS_PER_TRANSACTION), &err))
    {
        return InitError(err);
    }

    // Configure height to stop running
    if (std::string err; !config.SetStopAtHeight(
        gArgs.GetArg("-stopatheight", DEFAULT_STOPATHEIGHT), &err))
    {
        return InitError(err);
    }

    // Configure promiscuous memory pool flags
    if (gArgs.IsArgSet("-promiscuousmempoolflags"))
    {
        if (std::string err; !config.SetPromiscuousMempoolFlags(
            gArgs.GetArg("-promiscuousmempoolflags", 0), &err))
        {
            return InitError(err);
        }
    }

    // Configure preferred size of blockfile.
    config.SetPreferredBlockFileSize(
        gArgs.GetArgAsBytes("-preferredblockfilesize",
            DEFAULT_PREFERRED_BLOCKFILE_SIZE));

    // Configure excessive block size.
    if(gArgs.IsArgSet("-excessiveblocksize")) {
        const uint64_t nProposedExcessiveBlockSize =
            gArgs.GetArgAsBytes("-excessiveblocksize", 0);
        if (std::string err; !config.SetMaxBlockSize(nProposedExcessiveBlockSize, &err)) {
            return InitError(err);
        }
    }

    if(gArgs.IsArgSet("-factormaxsendqueuesbytes")) {
        const uint64_t factorMaxSendQueuesBytes = gArgs.GetArg("-factormaxsendqueuesbytes", DEFAULT_FACTOR_MAX_SEND_QUEUES_BYTES);
        config.SetFactorMaxSendQueuesBytes(factorMaxSendQueuesBytes);
    }

    // Configure max generated block size.
    if(gArgs.IsArgSet("-blockmaxsize")) {
        const uint64_t nProposedMaxGeneratedBlockSize =
            gArgs.GetArgAsBytes("-blockmaxsize", 0 /* not used*/);
        if (std::string err; !config.SetMaxGeneratedBlockSize(nProposedMaxGeneratedBlockSize, &err)) {
            return InitError(err);
        }
    }

    // Configure block size related activation time
    if(gArgs.IsArgSet("-blocksizeactivationtime")) {
        const int64_t nProposedActivationTime =
            gArgs.GetArg("-blocksizeactivationtime", 0);
        if (std::string err; !config.SetBlockSizeActivationTime(nProposedActivationTime)){
            return InitError(err);
        }
    }

    // Configure whether to run extra block candidate validity checks
    config.SetTestBlockCandidateValidity(
        gArgs.GetBoolArg("-blockcandidatevaliditytest", chainparams.TestBlockCandidateValidity()));

    // Configure whether to performe BIP30 checks
    if(gArgs.IsArgSet("-disablebip30checks"))
    {
        bool doDisable = gArgs.GetBoolArg("-disablebip30checks", chainparams.DisableBIP30Checks());
        if (std::string err; !config.SetDisableBIP30Checks(doDisable)){
            return InitError(err);
        }
    }

    // Configure mining block assembler
    if(gArgs.IsArgSet("-blockassembler")) {
        std::string assemblerStr { boost::to_upper_copy<std::string>(gArgs.GetArg("-blockassembler", "")) };
        mining::CMiningFactory::BlockAssemblerType assembler { enum_cast<mining::CMiningFactory::BlockAssemblerType>(assemblerStr) };
        if(assembler == mining::CMiningFactory::BlockAssemblerType::UNKNOWN)
            assembler = mining::DEFAULT_BLOCK_ASSEMBLER_TYPE;
        config.SetMiningCandidateBuilder(assembler);
    }

    // Configure data carrier size.
    if(gArgs.IsArgSet("-datacarriersize")) {
        config.SetDataCarrierSize(gArgs.GetArgAsBytes("-datacarriersize", DEFAULT_DATA_CARRIER_SIZE));
    }

    // Configure ancestor limit count.
    if(gArgs.IsArgSet("-limitancestorcount")) {
        int64_t limitancestorcount = gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        if(std::string err; !config.SetLimitAncestorCount(limitancestorcount, &err))
        {
            return InitError(err);
        }
    }
    
    // Configure ancestor limit count.
    if(gArgs.IsArgSet("-limitcpfpgroupmemberscount")) {
        int64_t limitcpfpgroupmemberscount = gArgs.GetArgAsBytes("-limitcpfpgroupmemberscount", DEFAULT_SECONDARY_MEMPOOL_ANCESTOR_LIMIT);
        if(std::string err; !config.SetLimitSecondaryMempoolAncestorCount(limitcpfpgroupmemberscount, &err)){
            return InitError(err);
        }
        config.SetLimitSecondaryMempoolAncestorCount(gArgs.GetArg("-limitcpfpgroupmemberscount", DEFAULT_SECONDARY_MEMPOOL_ANCESTOR_LIMIT));
    }

    // configure max transaction size policy
    if (gArgs.IsArgSet("-maxtxsizepolicy"))
    {
        int64_t maxTxSizePolicy = gArgs.GetArgAsBytes("-maxtxsizepolicy", DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS);
        if (std::string err; !config.SetMaxTxSizePolicy(maxTxSizePolicy, &err)) {
            return InitError(err);
        }
    }

    // configure min ratio between tx input to tx output size to be considered free consolidation tx.
    if (gArgs.IsArgSet("-minconsolidationfactor"))
    {
        int64_t minConsolidationFactor = gArgs.GetArg("-minconsolidationfactor", DEFAULT_MIN_CONSOLIDATION_FACTOR);
        if (std::string err; !config.SetMinConsolidationFactor(minConsolidationFactor, &err)) {
            return InitError(err);
        }
    }

    // configure maxiumum scriptSig input size not considered spam in a consolidation transaction
    if (gArgs.IsArgSet("-maxconsolidationinputscriptsize"))
    {
        int64_t maxConsolidationInputScriptSize = gArgs.GetArgAsBytes("-maxconsolidationinputscriptsize", DEFAULT_MAX_CONSOLIDATION_INPUT_SCRIPT_SIZE);
        if (std::string err; !config.SetMaxConsolidationInputScriptSize(maxConsolidationInputScriptSize, &err)) {
            return InitError(err);
        }
    }

    // configure minimum number of confirmations needed by transactions spent in a consolidatin transaction
    if (gArgs.IsArgSet("-minconfconsolidationinput") && gArgs.IsArgSet("-minconsolidationinputmaturity")) {
        return InitError(
            _("Cannot use both -minconfconsolidationinput and -minconsolidationinputmaturity (deprecated) at the same time"));
    }
    if (gArgs.IsArgSet("-minconfconsolidationinput")) {
        int64_t param = gArgs.GetArg("-minconfconsolidationinput", DEFAULT_MIN_CONF_CONSOLIDATION_INPUT);
        if (std::string err; !config.SetMinConfConsolidationInput(param, &err)) {
            return InitError(err);
        }
    }
    if (gArgs.IsArgSet("-minconsolidationinputmaturity")) {
        int64_t param = gArgs.GetArg("-minconsolidationinputmaturity", DEFAULT_MIN_CONF_CONSOLIDATION_INPUT);
        if (std::string err; !config.SetMinConfConsolidationInput(param, &err)) {
            return InitError(err);
        }
        LogPrintf("Option -minconsolidationinputmaturity is deprecated, use -minconfconsolidationinput instead.\n");
    }

    // configure if non standard inputs for consolidation transactions are allowed
    if (gArgs.IsArgSet("-acceptnonstdconsolidationinput"))
    {
        bool param = gArgs.GetBoolArg("-acceptnonstdconsolidationinput", DEFAULT_ACCEPT_NON_STD_CONSOLIDATION_INPUT);
        if (std::string err; !config.SetAcceptNonStdConsolidationInput(param, &err)) {
            return InitError(err);
        }
    }
    // Configure genesis activation height.
    int32_t genesisActivationHeight = static_cast<int32_t>(gArgs.GetArg("-genesisactivationheight", chainparams.GetConsensus().genesisHeight));
    if (std::string err; !config.SetGenesisActivationHeight(genesisActivationHeight, &err)) {
        return InitError(err);
    }

    if (std::string err; !config.SetMaxStackMemoryUsage(
        gArgs.GetArgAsBytes("-maxstackmemoryusageconsensus", 0),
        gArgs.GetArgAsBytes("-maxstackmemoryusagepolicy", DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS),
        &err))
    {
        return InitError(err);
    }

    //Configure max script size after genesis
    if (gArgs.IsArgSet("-maxscriptsizepolicy")) {
        int64_t maxScriptSize = gArgs.GetArgAsBytes("-maxscriptsizepolicy", DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS);
        if (std::string err; !config.SetMaxScriptSizePolicy(maxScriptSize, &err)) {
            return InitError(err);
        }
    }

    // Txn sinks
    if (gArgs.IsArgSet("-invalidtxsink"))
    {
        for (const std::string &sink : gArgs.GetArgs("-invalidtxsink"))
        {
            if (std::string err; !config.AddInvalidTxSink(sink, &err))
            {
                return InitError(err);
            }
        }
    }

    if(std::string err; !config.SetBlockValidationTxBatchSize(gArgs.GetArg("-blockvalidationtxbatchsize", DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE), &err)) {
        return InitError(err);
    }

    // Safe mode activation
    if(gArgs.IsArgSet("-safemodewebhookurl")) {
        if(std::string err; !config.SetSafeModeWebhookURL(gArgs.GetArg("-safemodewebhookurl", ""), &err)) {
            return InitError(err);
        }
    }
    if(std::string err; !config.SetSafeModeMinForkHeightDifference(gArgs.GetArg("-safemodeminblockdifference", SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetSafeModeMaxForkDistance(gArgs.GetArg("-safemodemaxforkdistance", SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetSafeModeMinForkLength(gArgs.GetArg("-safemodeminforklength", SAFE_MODE_DEFAULT_MIN_FORK_LENGTH), &err)) {
        return InitError(err);
    }

    // Block download
    if(std::string err; !config.SetBlockStallingMinDownloadSpeed(gArgs.GetArg("-blockstallingmindownloadspeed", DEFAULT_MIN_BLOCK_STALLING_RATE), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockStallingTimeout(gArgs.GetArg("-blockstallingtimeout", DEFAULT_BLOCK_STALLING_TIMEOUT), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockDownloadWindow(gArgs.GetArg("-blockdownloadwindow", DEFAULT_BLOCK_DOWNLOAD_WINDOW), &err)) {
        return InitError(err);
    }
    int64_t defaultBlockDownloadLowerWindow { gArgs.GetArg("-prune", 0)? DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW : config.GetBlockDownloadWindow() };
    if(std::string err; !config.SetBlockDownloadLowerWindow(gArgs.GetArg("-blockdownloadlowerwindow", defaultBlockDownloadLowerWindow), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockDownloadSlowFetchTimeout(gArgs.GetArg("-blockdownloadslowfetchtimeout", DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockDownloadMaxParallelFetch(gArgs.GetArg("-blockdownloadmaxparallelfetch", DEFAULT_MAX_BLOCK_PARALLEL_FETCH), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockDownloadTimeoutBase(gArgs.GetArg("-blockdownloadtimeoutbasepercent", DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockDownloadTimeoutBaseIBD(gArgs.GetArg("-blockdownloadtimeoutbaseibdpercent", DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockDownloadTimeoutPerPeer(gArgs.GetArg("-blockdownloadtimeoutperpeerpercent", DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER), &err)) {
        return InitError(err);
    }

    // P2P parameters
    if(std::string err; !config.SetP2PHandshakeTimeout(gArgs.GetArg("-p2phandshaketimeout", DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetStreamSendRateLimit(gArgs.GetArg("-streamsendratelimit", Stream::DEFAULT_SEND_RATE_LIMIT), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBanScoreThreshold(gArgs.GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetBlockTxnMaxPercent(gArgs.GetArg("-maxblocktxnpercent", DEFAULT_BLOCK_TXN_MAX_PERCENT), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetMultistreamsEnabled(gArgs.GetBoolArg("-multistreams", DEFAULT_STREAMS_ENABLED), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetWhitelistRelay(gArgs.GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetWhitelistForceRelay(gArgs.GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetRejectMempoolRequest(gArgs.GetBoolArg("-rejectmempoolrequest", DEFAULT_REJECTMEMPOOLREQUEST), &err)) {
        return InitError(err);
    }
    if(gArgs.IsArgSet("-dropmessagestest")) {
        if(std::string err; !config.SetDropMessageTest(gArgs.GetArg("-dropmessagestest", 0), &err)) {
            return InitError(err);
        }
    }
    if(std::string err; !config.SetInvalidChecksumInterval(gArgs.GetArg("-invalidcsinterval", DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetInvalidChecksumFreq(gArgs.GetArg("-invalidcsfreq", DEFAULT_INVALID_CHECKSUM_FREQUENCY), &err)) {
        return InitError(err);
    }
    if(std::string err; !config.SetFeeFilter(gArgs.GetBoolArg("-feefilter", DEFAULT_FEEFILTER), &err)) {
        return InitError(err);
    }

    // RPC parameters
    if(std::string err; !config.SetWebhookClientNumThreads(gArgs.GetArg("-rpcwebhookclientnumthreads", rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS), &err)) {
        return InitError(err);
    }

#if ENABLE_ZMQ
    bool zmqSinkSpecified = (config.GetInvalidTxSinks().count("ZMQ") != 0);
    bool zmqIpDefined = gArgs.IsArgSet("-zmqpubinvalidtx");

    if (zmqSinkSpecified && !zmqIpDefined)
    {
        return InitError("The 'zmqpubinvalidtx' parameter should be specified when 'invalidtxsink' is set to ZMQ.");
    }
    if (!zmqSinkSpecified && zmqIpDefined)
    {
        return InitError("The 'invalidtxsink' parameter should be set to ZMQ when 'zmqpubinvalidtx' is defined.");
    }
#endif

    if (gArgs.IsArgSet("-invalidtxfilemaxdiskusage"))
    {
        auto maxInvalidTxDumpFileSize =
            gArgs.GetArgAsBytes(
                "-invalidtxfilemaxdiskusage",
                CInvalidTxnPublisher::DEFAULT_FILE_SINK_DISK_USAGE,
                ONE_MEGABYTE);
        if (std::string err; !config.SetInvalidTxFileSinkMaxDiskUsage(maxInvalidTxDumpFileSize, &err))
        {
            return InitError(err);
        }
    }

    if (gArgs.IsArgSet("-invalidtxfileevictionpolicy"))
    {
        assert(CInvalidTxnPublisher::DEFAULT_FILE_SINK_EVICTION_POLICY == InvalidTxEvictionPolicy::IGNORE_NEW);
        auto evictionPolicy = gArgs.GetArg("-invalidtxfileevictionpolicy", "IGNORE_NEW");
        if (std::string err; !config.SetInvalidTxFileSinkEvictionPolicy(evictionPolicy, &err))
        {
            return InitError(err);
        }
    }

    config.SetEnableAssumeWhitelistedBlockDepth(gArgs.GetBoolArg("-enableassumewhitelistedblockdepth", DEFAULT_ENABLE_ASSUME_WHITELISTED_BLOCK_DEPTH));
    if(std::string err; !config.SetAssumeWhitelistedBlockDepth(gArgs.GetArg("-assumewhitelistedblockdepth", DEFAULT_ASSUME_WHITELISTED_BLOCK_DEPTH), &err)) {
        return InitError(err);
    }

#if ENABLE_ZMQ
    if (gArgs.IsArgSet("-invalidtxzmqmaxmessagesize"))
    {
        auto zmqMessageSize =
            gArgs.GetArgAsBytes(
                "-invalidtxzmqmaxmessagesize",
                CInvalidTxnPublisher::DEFAULT_ZMQ_SINK_MAX_MESSAGE_SIZE,
                ONE_MEGABYTE);
        if (std::string err; !config.SetInvalidTxZMQMaxMessageSize(zmqMessageSize, &err))
        {
            return InitError(err);
        }
    }
#endif

    // block pruning; get the amount of disk space (in MiB) to allot for block &
    // undo files
    int64_t nPruneArg = gArgs.GetArg("-prune", 0);
    if (nPruneArg < 0) {
        return InitError(
            _("Prune cannot be configured with a negative value."));
    }
    nPruneTarget = (uint64_t)nPruneArg * ONE_MEBIBYTE;
    if (nPruneArg == 1) { // manual pruning: -prune=1
        LogPrintf("Block pruning enabled.  Use RPC call "
                  "pruneblockchain(height) to manually prune block and undo "
                  "files.\n");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget) {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES) {
            return InitError(
                strprintf(_("Prune configured below the minimum of %d MiB.  "
                            "Please use a higher number."),
                          MIN_DISK_SPACE_FOR_BLOCK_FILES / ONE_MEBIBYTE));
        }
        LogPrintf("Prune configured to target %uMiB on disk for block and undo "
                  "files.\n",
                  nPruneTarget / ONE_MEBIBYTE);
        fPruneMode = true;
    }

    if(std::string err; !config.SetMinBlocksToKeep(gArgs.GetArg("-pruneminblockstokeep", DEFAULT_MIN_BLOCKS_TO_KEEP), &err)) {
        return InitError(err);
    }

    if(std::string err; !config.SetMaxStdTxnValidationDuration(
        gArgs.GetArg(
            "-maxstdtxvalidationduration",
            DEFAULT_MAX_STD_TXN_VALIDATION_DURATION.count()),
        &err))
    {
        return InitError(err);
    }

    if(std::string err; !config.SetMaxNonStdTxnValidationDuration(
        gArgs.GetArg(
            "-maxnonstdtxvalidationduration",
            DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION.count()),
        &err))
    {
        return InitError(err);
    }

    if(std::string err; !config.SetMaxTxnValidatorAsyncTasksRunDuration(
        gArgs.GetArg(
            "-maxtxnvalidatorasynctasksrunduration",
            CTxnValidator::DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION.count()),
        &err))
    {
        return InitError(err);
    }

    if(std::string err; !config.SetMaxTxnChainValidationBudget(
        gArgs.GetArg(
            "-maxtxchainvalidationbudget",
            DEFAULT_MAX_TXN_CHAIN_VALIDATION_BUDGET.count()),
        &err))
    {
        return InitError(err);
    }

    config.SetValidationClockCPU(gArgs.GetBoolArg("-validationclockcpu", DEFAULT_VALIDATION_CLOCK_CPU));
#ifndef BOOST_CHRONO_HAS_THREAD_CLOCK
    if (config.GetValidationClockCPU()) {
        return InitError(
            strprintf("validationclockcpu enabled on a platform with no CPU clock. Start with -validationclockcpu=0 -maxstdtxvalidationduration=10"));
    }
#endif

    if(std::string err; !config.CheckTxValidationDurations(err))
    {
        return InitError(err);
    }

    if (gArgs.IsArgSet("-txnvalidationschedulestrategy"))
    {
        static_assert(DEFAULT_PTV_TASK_SCHEDULE_STRATEGY == PTVTaskScheduleStrategy::TOPO_SORT);
        auto strategy = gArgs.GetArg("-txnvalidationschedulestrategy", "TOPO_SORT");
        if (std::string err; !config.SetPTVTaskScheduleStrategy(strategy, &err))
        {
            return InitError(err);
        }
    }

    if(std::string err; !config.SetMaxCoinsViewCacheSize(
        gArgs.GetArgAsBytes("-maxcoinsviewcachesize", 0), &err))
    {
        return InitError(err);
    }

    if(std::string err; !config.SetMaxCoinsProviderCacheSize(
        gArgs.GetArgAsBytes("-maxcoinsprovidercachesize", DEFAULT_COINS_PROVIDER_CACHE_SIZE),
        &err))
    {
        return InitError(err);
    }

    if(std::string err; !config.SetMaxCoinsDbOpenFiles(
        gArgs.GetArg("-maxcoinsdbfiles", CoinsDB::MaxFiles::Default().maxFiles), &err))
    {
        return InitError(err);
    }

    // Double-Spend processing parameters
    if(std::string err; !config.SetDoubleSpendNotificationLevel(
        gArgs.GetArg("-dsnotifylevel", static_cast<int>(DSAttemptHandler::DEFAULT_NOTIFY_LEVEL)), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendEndpointFastTimeout(
        gArgs.GetArg("-dsendpointfasttimeout", rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendEndpointSlowTimeout(
        gArgs.GetArg("-dsendpointslowtimeout", rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendEndpointSlowRatePerHour(
        gArgs.GetArg("-dsendpointslowrateperhour", DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendEndpointPort(
        gArgs.GetArg("-dsendpointport", rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendEndpointBlacklistSize(
        gArgs.GetArg("-dsendpointblacklistsize", DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendEndpointSkipList(gArgs.GetArg("-dsendpointskiplist", ""), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendEndpointMaxCount(
        gArgs.GetArg("-dsendpointmaxcount", DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendTxnRemember(
        gArgs.GetArg("-dsattempttxnremember", DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendNumFastThreads(
        gArgs.GetArg("-dsattemptnumfastthreads", DSAttemptHandler::DEFAULT_NUM_FAST_THREADS), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendNumSlowThreads(
        gArgs.GetArg("-dsattemptnumslowthreads", DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetDoubleSpendQueueMaxMemory(
        gArgs.GetArgAsBytes("-dsattemptqueuemaxmemory", DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY, ONE_MEBIBYTE), &err))
    {
        return InitError(err);
    }
    if(gArgs.IsArgSet("-dsdetectedwebhookurl"))
    {
        if(std::string err; !config.SetDoubleSpendDetectedWebhookURL(gArgs.GetArg("-dsdetectedwebhookurl", ""), &err))
        {
            return InitError(err);
        }
    }
    if(std::string err; !config.SetDoubleSpendDetectedWebhookMaxTxnSize(
        gArgs.GetArgAsBytes("-dsdetectedwebhookmaxtxnsize", DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE, ONE_MEBIBYTE), &err))
    {
        return InitError(err);
    }

    // MinerID parameters
    if(std::string err; !config.SetMinerIdEnabled(
        gArgs.GetBoolArg("-minerid", MinerIdDatabaseDefaults::DEFAULT_MINER_ID_ENABLED), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetMinerIdCacheSize(
        gArgs.GetArgAsBytes("-mineridcachesize", MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetMinerIdsNumToKeep(
        gArgs.GetArg("-mineridnumtokeep", MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetMinerIdReputationM(
        gArgs.GetArg("-mineridreputation_m", MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetMinerIdReputationN(
        gArgs.GetArg("-mineridreputation_n", MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N), &err))
    {
        return InitError(err);
    }
    if(std::string err; !config.SetMinerIdReputationMScale(
        gArgs.GetDoubleArg("-mineridreputation_mscale", MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR), &err))
    {
        return InitError(err);
    }
    if(gArgs.IsArgSet("-mineridgeneratorurl"))
    {
        if(std::string err; !config.SetMinerIdGeneratorURL(gArgs.GetArg("-mineridgeneratorurl", ""), &err))
        {
            return InitError(err);
        }
    }
    if(std::string err; !config.SetMinerIdGeneratorAlias(gArgs.GetArg("-mineridgeneratoralias", ""), &err))
    {
        return InitError(err);
    }

    RegisterAllRPCCommands(tableRPC);
#ifdef ENABLE_WALLET
    RegisterWalletRPCCommands(tableRPC);
    RegisterDumpRPCCommands(tableRPC);
#endif

    nConnectTimeout = gArgs.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0) nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    // Option -minrelaytxfee has been removed
    if (gArgs.IsArgSet("-minrelaytxfee")) {
        LogPrintf("Warning: configuration parameter -minrelaytxfee was removed\n");
    }
    // TODO: remove relayfee settings
    config.SetMinFeePerKB(CFeeRate(Amount{0}));

    if (gArgs.IsArgSet("-dustlimitfactor")) {
        LogPrintf("Warning: configuration parameter -dustlimitfactor was removed\n");
    }
    // TODO: remove dust settings
    config.SetDustLimitFactor(DEFAULT_DUST_LIMIT_FACTOR);


    // Deprecated. The -blockmintxfee is now zero.
    if (gArgs.IsArgSet("-blockmintxfee")) {
        LogPrintf("Warning: Optional parameter -blockmintxfee was replaced with mandatory -minminingtxfee\n");
    }

    // Sanity check argument for min fee for including tx in block
    // TODO: Harmonize which arguments need sanity checking and where that
    // happens.
    if (gArgs.IsArgSet("-minminingtxfee")) {
        Amount n(0);
        if (!ParseMoney(gArgs.GetArg("-minminingtxfee", ""), n)) {
            return InitError(AmountErrMsg("minminingtxfee",
                                          gArgs.GetArg("-minminingtxfee", "")));
        }
        mempool.SetBlockMinTxFee(CFeeRate(n));
    }

    if(gArgs.IsArgSet("-rollingminfeeratehalflife"))
    {
        const auto halflife = gArgs.GetArg(
            "-rollingminfeeratehalflife", CTxMemPool::MAX_ROLLING_FEE_HALFLIFE);

        if(!mempool.SetRollingMinFee(halflife))
            LogPrintf("Warning: configuration parameter -rollingminfeeratehalflife out-of-range %i - %i\n",
                      CTxMemPool::MIN_ROLLING_FEE_HALFLIFE,
                      CTxMemPool::MAX_ROLLING_FEE_HALFLIFE);
    }

    if (gArgs.IsArgSet("-mindebugrejectionfee")) {
        if (chainparams.NetworkIDString() != "main") {
            Amount n(0);
            if (!ParseMoney(gArgs.GetArg("-mindebugrejectionfee", ""), n)) {
                return InitError(AmountErrMsg("mindebugrejectionfee",
                                              gArgs.GetArg("--mindebugrejectionfee", "")));
            }
            mempool.SetMinDebugRejectionFee(CFeeRate(n));
        } else {
            // Only for testing in non-mainnet. The -mindebugrejectionfee is now zero.
            return InitError("configuration parameter -mindebugrejectionfee is only for testing");
        }
    }


    // Feerate used to define dust.  Shouldn't be changed lightly as old
    // implementations may inadvertently create non-standard transactions.
    if (gArgs.IsArgSet("-dustrelayfee")) {
        LogPrintf("Warning: configuration parameter -dustrelayfee was removed\n");
    }
    // TODO: remove dust settings
    config.SetDustRelayFee(DUST_RELAY_TX_FEE);

    fRequireStandard =
        !gArgs.GetBoolArg("-acceptnonstdtxn", !chainparams.RequireStandard());
    if (chainparams.RequireStandard() && !fRequireStandard)
        return InitError(
            strprintf("acceptnonstdtxn is not currently supported for %s chain",
                      chainparams.NetworkIDString()));

    config.SetAcceptNonStandardOutput(
        gArgs.GetBoolArg("-acceptnonstdoutputs", config.GetAcceptNonStandardOutput(true)));


    // Enable selfish mining detection
    config.SetDetectSelfishMining(gArgs.GetBoolArg("-detectselfishmining", DEFAULT_DETECT_SELFISH_MINING));
    
    // Min time difference in sec between the last block and last mempool 
    // transaction for the block to be classified as selfishly mined
    if (gArgs.IsArgSet("-minblockmempooltimedifferenceselfish")) {
        int64_t minBlockMempoolTimeDiff = gArgs.GetArg("-minblockmempooltimedifferenceselfish", DEFAULT_MIN_BLOCK_MEMPOOL_TIME_DIFFERENCE_SELFISH);
        if (std::string err;
            !config.SetMinBlockMempoolTimeDifferenceSelfish(minBlockMempoolTimeDiff, &err)) {
            return InitError(err);
        }
    }

    // Set percentage threshold of number of txs in mempool 
    // that are not included in received block for the block to be classified as selfishly mined
    if (gArgs.IsArgSet("-selfishtxpercentthreshold"))
    {
        int64_t selfishTxPercentThreshold = gArgs.GetArg("-selfishtxpercentthreshold", DEFAULT_SELFISH_TX_THRESHOLD_IN_PERCENT);
        if (std::string err; !config.SetSelfishTxThreshold(selfishTxPercentThreshold, &err))
        {
            return InitError(err);
        }
    }

#ifdef ENABLE_WALLET
    if (!CWallet::ParameterInteraction()) return false;
#endif

    fIsBareMultisigStd =
        gArgs.GetBoolArg("-permitbaremultisig", DEFAULT_PERMIT_BAREMULTISIG);
    config.SetDataCarrier(gArgs.GetBoolArg("-datacarrier", DEFAULT_ACCEPT_DATACARRIER));

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(gArgs.GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (gArgs.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        nLocalServices = ServiceFlags(nLocalServices | NODE_BLOOM);

    // Signal Bitcoin Cash support.
    // TODO: remove some time after the hardfork when no longer needed
    // to differentiate the network nodes.
    nLocalServices = ServiceFlags(nLocalServices | NODE_BITCOIN_CASH);

    nMaxTipAge = gArgs.GetArg("-maxtipage", DEFAULT_MAX_TIP_AGE);

    // Configure the maximum number of sigops we're willing to relay/mine in a single tx
    if (gArgs.IsArgSet("-maxtxsigopscountspolicy"))
    {
        const int64_t value = gArgs.GetArg("-maxtxsigopscountspolicy", DEFAULT_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS);
        if (std::string err; !config.SetMaxTxSigOpsCountPolicy(value, &err))
        {
            return InitError(err);
        }
    }

    // Configure max number of public keys per MULTISIG operation
    if (gArgs.IsArgSet("-maxpubkeyspermultisigpolicy"))
    {
        const int64_t value = gArgs.GetArg("-maxpubkeyspermultisigpolicy", DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS);

        std::string err;
        if (!config.SetMaxPubKeysPerMultiSigPolicy(value, &err))
        {
            return InitError(err);
        }
    }

    // Configure maximum length of numbers in scripts
    if (gArgs.IsArgSet("-maxscriptnumlengthpolicy"))
    {
        const int64_t value = gArgs.GetArgAsBytes("-maxscriptnumlengthpolicy", DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS);
        if (std::string err; !config.SetMaxScriptNumLengthPolicy(value, &err))
        {
            return InitError(err);
        }
    }

    // Configure max number of blocks in which Genesis graceful period is active
    if (gArgs.IsArgSet("-maxgenesisgracefulperiod"))
    {
        const int64_t value = gArgs.GetArg("-maxgenesisgracefulperiod", DEFAULT_GENESIS_GRACEFULL_ACTIVATION_PERIOD);

        std::string err;
        if (!config.SetGenesisGracefulPeriod(value, &err))
        {
            return InitError(err);
        }
    }

    if (gArgs.IsArgSet("-invalidateblock"))
    {
        std::set<uint256> invalidBlocks;
        for(const auto& invalidBlockHashStr : gArgs.GetArgs("-invalidateblock"))
        {
            uint256 hash = uint256S(invalidBlockHashStr);
            invalidBlocks.insert(hash);
        }
        config.SetInvalidBlocks(invalidBlocks);
    }

    if (gArgs.IsArgSet("-banclientua"))
    {
        std::set<std::string> invalidUAClients;
        for (auto& invalidClient : gArgs.GetArgs("-banclientua"))
        {
            invalidUAClients.insert(std::move(invalidClient));
        }
        config.SetBanClientUA(std::move(invalidUAClients));
    }

    if (gArgs.IsArgSet("-allowclientua"))
    {
        std::set<std::string> validUAClients;
        for (auto& validClient : gArgs.GetArgs("-allowclientua"))
        {
            validUAClients.insert(std::move(validClient));
        }
        config.SetAllowClientUA(std::move(validUAClients));
    }

    {
        int64_t maxBlockEstimate = std::min(config.GetMaxBlockSize(), config.GetMaxMempool());
        std::string err;

        // Configure preferred size of a single Merkle Tree data file.
        int64_t merkleTreeFileSizeArg = gArgs.GetArgAsBytes("-preferredmerkletreefilesize", CalculatePreferredMerkleTreeSize(maxBlockEstimate));
        if (!config.SetPreferredMerkleTreeFileSize(merkleTreeFileSizeArg, &err))
            return InitError(err);

        // Configure size of Merkle Trees memory cache.
        int64_t maxMerkleTreeMemCacheSizeArg = gArgs.GetArgAsBytes("-maxmerkletreememcachesize", CalculatePreferredMerkleTreeSize(maxBlockEstimate));
        if (!config.SetMaxMerkleTreeMemoryCacheSize(maxMerkleTreeMemCacheSizeArg, &err))
            return InitError(err);

        // Configure maximum disk space that can be taken by Merkle Tree data files.
        int64_t maxMerkleTreeDiskspaceArg = gArgs.GetArgAsBytes("-maxmerkletreediskspace", CalculateMinDiskSpaceForMerkleFiles(maxBlockEstimate));
        if (maxMerkleTreeDiskspaceArg < merkleTreeFileSizeArg || maxMerkleTreeDiskspaceArg < maxMerkleTreeMemCacheSizeArg)
        {
            err = "-maxmerkletreediskspace cannot be less than -maxmerkletreememcachesize or -preferredmerkletreefilesize";
            return InitError(err);
        }

        if (!config.SetMaxMerkleTreeDiskSpace(maxMerkleTreeDiskspaceArg, &err))
            return InitError(err);
    }

    const uint64_t value = gArgs.GetArg("-maxprotocolrecvpayloadlength", DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH);
    if (std::string err; !config.SetMaxProtocolRecvPayloadLength(value, &err))
    {
        return InitError(err);
    }
    mapAlreadyAskedFor = std::make_unique<limitedmap<uint256, int64_t>>(CInv::estimateMaxInvElements(config.GetMaxProtocolSendPayloadLength()));


    const uint64_t recvInvQueueFactorArg = gArgs.GetArg("-recvinvqueuefactor", DEFAULT_RECV_INV_QUEUE_FACTOR);
    if (std::string err; !config.SetRecvInvQueueFactor(recvInvQueueFactorArg, &err))
    {
        return InitError(err);
    }

    if (std::string err;
        !config.SetSoftConsensusFreezeDuration(
            gArgs.GetArg(
                "-softconsensusfreezeduration",
                DEFAULT_SOFT_CONSENSUS_FREEZE_DURATION),
            &err))
    {
        return InitError(err);
    }

    return true;
}

static bool LockDataDirectory(bool probeOnly) {
    std::string strDataDir = GetDataDir().string();

    // Make sure only a single Bitcoin process is using the data directory.
    fs::path pathLockFile = GetDataDir() / ".lock";
    // empty lock file; created if it doesn't exist.
    FILE *file = fsbridge::fopen(pathLockFile, "a");
    if (file) fclose(file);

    try {
        static boost::interprocess::file_lock lock(
            pathLockFile.string().c_str());
        if (!lock.try_lock()) {
            return InitError(
                strprintf(_("Cannot obtain a lock on data directory %s. %s is "
                            "probably already running."),
                          strDataDir, _(PACKAGE_NAME)));
        }
        if (probeOnly) {
            lock.unlock();
        }
    } catch (const boost::interprocess::interprocess_exception &e) {
        return InitError(strprintf(_("Cannot obtain a lock on data directory "
                                     "%s. %s is probably already running.") +
                                       " %s.",
                                   strDataDir, _(PACKAGE_NAME), e.what()));
    }
    return true;
}

bool AppInitSanityChecks() {
    // Step 4: sanity checks

    // Initialize elliptic curve code
    std::string sha256_algo = SHA256AutoDetect();
    LogPrintf("Using the '%s' SHA256 implementation\n", sha256_algo);
    RandomInit();

    // Sanity check
    if (!InitSanityCheck()) {
        return InitError(strprintf(
            _("Initialization sanity check failed. %s is shutting down."),
            _(PACKAGE_NAME)));
    }

    // Probe the data directory lock to give an early error message, if possible
    return LockDataDirectory(true);
}

void preloadChainStateThreadFunction()
{
#ifndef WIN32
    auto path = boost::filesystem::canonical(GetDataDir() / "chainstate").string();
    LogPrintf("Preload started\n");
    try {
        auto start = std::chrono::system_clock::now();
        VMTouch vm;

        vm.vmtouch_touch(path);

        auto end = std::chrono::system_clock::now();

        int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
        auto& warnings = vm.get_warnings();
        if (!warnings.empty())
        {
            LogPrintf("Warnings occured during chainstate preload\n:");
            for(auto& warning : warnings)
            {
                LogPrintf("Preload warning:  %s \n", warning.c_str());
            }
        }
        LogPrintf("Preload finished in %" PRId64 " [ms]. Preloaded %" PRId64 " MB of data (%d %% were already present in memory)\n",
            elapsed, vm.total_pages*vm.pagesize/ONE_MEGABYTE, (int) vm.getPagesInCorePercent());

        // verify that pages were not evicted
        VMTouch vm2;
        int stillLoadedPercent = (int) vm2.vmtouch_check(path);

        if (stillLoadedPercent < 90) {
            LogPrintf("WARNING: Only %d %% of data still present in memory after preloading. Increase amount of free RAM to get the benefits of preloading\n", stillLoadedPercent);
        }

    }   catch(const std::runtime_error& ex) {
        LogPrintf("Error while preloading chain state: %s\n", ex.what());
    }

#else
    LogPrintf("Preload is not supported on this platform!\n");
    return;
#endif
}

void preloadChainState(boost::thread_group &threadGroup)
{
    int64_t preload;
    preload = gArgs.GetArg("-preload", 0);
    if (preload == 0)
    {
        LogPrintf("Chainstate will NOT be preloaded\n");
        return;
    }

    if (preload == 1 )// preload with vmtouch
    {
        threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "preload", preloadChainStateThreadFunction));
    }
    else
    {
        LogPrintf("Unknown value of -preload. No preloading will be done\n");
    }
}

static size_t GetMaxNumberOfMerkleTreeThreads()
{
    // Use 1/4 of all threads for Merkle tree calculations
    size_t numberOfMerkleTreeCalculationThreads = static_cast<size_t>(std::thread::hardware_concurrency() * 0.25);
    if (!numberOfMerkleTreeCalculationThreads)
    {
        numberOfMerkleTreeCalculationThreads = 1;
    }
    return numberOfMerkleTreeCalculationThreads;
}

bool AppInitMain(ConfigInit &config, boost::thread_group &threadGroup,
                 CScheduler &scheduler, const task::CCancellationToken& shutdownToken) {
    const CChainParams &chainparams = config.GetChainParams();
    // Step 4a: application initialization

    // After daemonization get the data directory lock again and hold on to it
    // until exit. This creates a slight window for a race condition to happen,
    // however this condition is harmless: it will at most make us exit without
    // printing a message to console.
    if (!LockDataDirectory(false)) {
        // Detailed error printed inside LockDataDirectory
        return false;
    }

#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif

    BCLog::Logger &logger = GetLogger();

    bool default_shrinkdebugfile = logger.DefaultShrinkDebugFile();
    if (gArgs.GetBoolArg("-shrinkdebugfile", default_shrinkdebugfile)) {
        // Do this first since it both loads a bunch of bitcoind.log into memory,
        // and because this needs to happen before any other bitcoind.log printing.
        logger.ShrinkDebugFile();
    }

    if (logger.fPrintToDebugLog) {
        if (logger.OpenDebugLog()) {
            return InitError(strprintf(_("Unable to open log file.")));
        }
    }

    if (!logger.fLogTimestamps) {
        LogPrintf("Startup time: %s\n",
                  DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    }
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", GetDataDir().string());
    LogPrintf(
        "Using config file %s\n",
        GetConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME)).string());
    LogPrintf("Using at most %i automatic connections (%i file descriptors "
              "available)\n",
              nMaxConnections, nFD);

    InitSignatureCache();
    InitScriptExecutionCache();

    g_MempoolDatarefTracker = std::make_unique<mining::MempoolDatarefTracker>();
    g_BlockDatarefTracker = mining::make_from_dir();

    LogPrintf("Using %u threads for block transaction verification\n", config.GetPerBlockTxnValidatorThreadsCount());
    LogPrintf("Using %u threads for script verification\n", config.GetPerBlockScriptValidatorThreadsCount());
    InitScriptCheckQueues(config, threadGroup);

    // Late configuration for globaly constructed objects
    mempool.SuspendSanityCheck();
    mempool.getNonFinalPool().loadConfig();
    mempool.InitMempoolTxDB();
    if (!gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        mempool.ResumeSanityCheck();
    }

    // Start the lightweight task scheduler thread
    scheduler.startServiceThread(threadGroup);

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (gArgs.GetBoolArg("-server", false)) {
        uiInterface.InitMessage.connect(SetRPCWarmupStatus);
        if (!AppInitServers(config, threadGroup)) {
            return InitError(
                _("Unable to start HTTP server. See debug log for details."));
        }
    }

    if (gArgs.IsArgSet("-maxopsperscriptpolicy"))
    {
        const int64_t value = gArgs.GetArg("-maxopsperscriptpolicy", 0);

        std::string err;
        if (!config.SetMaxOpsPerScriptPolicy(value, &err))
        {
            return InitError(err);
        }
    }

    int64_t nStart=0;

// Step 5: verify wallet database integrity
#ifdef ENABLE_WALLET
    if (!CWallet::Verify(chainparams)) {
        return false;
    }
#endif
    // Step 6: network initialization

    // Note that we absolutely cannot open any actual connections
    // until the very end ("start node") as the UTXO/block state
    // is not yet setup and may end up being set up twice if we
    // need to reindex later.

    assert(!g_connman);
    {
        int64_t duration = gArgs.GetArg("-debugp2pthreadstalls", 0);
        g_connman =
            std::make_unique<CConnman>(
                config, GetRand(std::numeric_limits<uint64_t>::max()),
                GetRand(std::numeric_limits<uint64_t>::max()),
                std::chrono::milliseconds{duration > 0 ? duration : 0});
    }
    CConnman &connman = *g_connman;

    peerLogic.reset(new PeerLogicValidation(&connman));
    if (gArgs.IsArgSet("-broadcastdelay")) {
        const int64_t nDelayMillisecs = gArgs.GetArg("-broadcastdelay", DEFAULT_INV_BROADCAST_DELAY);
        if(!SetInvBroadcastDelay(nDelayMillisecs)){
            return InitError(strprintf(_("Error setting broadcastdelay=%d"), nDelayMillisecs));
        }
    }
    peerLogic->RegisterValidationInterface();
    RegisterNodeSignals(GetNodeSignals());

    if (gArgs.IsArgSet("-onlynet")) {
        std::set<enum Network> nets;
        for (const std::string &snet : gArgs.GetArgs("-onlynet")) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(
                    _("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net)) SetLimited(net);
        }
    }

    if (gArgs.IsArgSet("-whitelist")) {
        for (const std::string &net : gArgs.GetArgs("-whitelist")) {
            CSubNet subnet;
            LookupSubNet(net.c_str(), subnet);
            if (!subnet.IsValid())
                return InitError(strprintf(
                    _("Invalid netmask specified in -whitelist: '%s'"), net));
            connman.AddWhitelistedRange(subnet);
        }
    }

    bool proxyRandomize =
        gArgs.GetBoolArg("-proxyrandomize", DEFAULT_PROXYRANDOMIZE);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set
    // a proxy, this is the default
    std::string proxyArg = gArgs.GetArg("-proxy", "");

    if (proxyArg != "" && proxyArg != "0") {
        CService resolved(LookupNumeric(proxyArg.c_str(), 9050));
        proxyType addrProxy = proxyType(resolved, proxyRandomize);
        if (!addrProxy.IsValid()) {
            return InitError(
                strprintf(_("Invalid -proxy address: '%s'"), proxyArg));
        }

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetNameProxy(addrProxy);
    }

    // see Step 2: parameter interactions for more information about these
    fListen = gArgs.GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = gArgs.GetBoolArg("-discover", true);
    fNameLookup = gArgs.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);
    fRelayTxes = !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);

    if (fListen) {
        bool fBound = false;
        if (gArgs.IsArgSet("-bind")) {
            for (const std::string &strBind : gArgs.GetArgs("-bind")) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(),
                            false)) {
                    return InitError(ResolveErrMsg("bind", strBind));
                }
                fBound |=
                    Bind(connman, addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
        }
        if (gArgs.IsArgSet("-whitebind")) {
            for (const std::string &strBind : gArgs.GetArgs("-whitebind")) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false)) {
                    return InitError(ResolveErrMsg("whitebind", strBind));
                }
                if (addrBind.GetPort() == 0) {
                    return InitError(strprintf(
                        _("Need to specify a port with -whitebind: '%s'"),
                        strBind));
                }
                fBound |= Bind(connman, addrBind,
                               (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        }
        if (!gArgs.IsArgSet("-bind") && !gArgs.IsArgSet("-whitebind")) {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |=
                Bind(connman, CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= Bind(connman, CService(inaddr_any, GetListenPort()),
                           !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound) {
            return InitError(_("Failed to listen on any port. Use -listen=0 if "
                               "you want this."));
        }
    }

    if (gArgs.IsArgSet("-externalip")) {
        for (const std::string &strAddr : gArgs.GetArgs("-externalip")) {
            CService addrLocal;
            if (Lookup(strAddr.c_str(), addrLocal, GetListenPort(),
                       fNameLookup) &&
                addrLocal.IsValid()) {
                AddLocal(addrLocal, LOCAL_MANUAL);
            } else {
                return InitError(ResolveErrMsg("externalip", strAddr));
            }
        }
    }

    if (gArgs.IsArgSet("-seednode")) {
        for (const std::string &strDest : gArgs.GetArgs("-seednode")) {
            connman.AddOneShot(strDest);
        }
    }

#if ENABLE_ZMQ
    {
        LOCK(cs_zmqNotificationInterface);
        pzmqNotificationInterface = CZMQNotificationInterface::Create();
        if (pzmqNotificationInterface) {
            pzmqNotificationInterface->RegisterValidationInterface();
        }
    }
#endif
    // unlimited unless -maxuploadtarget is set
    uint64_t nMaxOutboundLimit = 0;
    uint64_t nMaxOutboundTimeframe = MAX_UPLOAD_TIMEFRAME;

    if (gArgs.IsArgSet("-maxuploadtarget")) {
        nMaxOutboundLimit =
            gArgs.GetArgAsBytes("-maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET, ONE_MEBIBYTE);
    }

    if (gArgs.IsArgSet("-maxpendingresponses_getheaders")) {
        auto v = gArgs.GetArg("-maxpendingresponses_getheaders", -1);
        if (v<0 || v>std::numeric_limits<unsigned int>::max()) {
            return InitError( strprintf(_("Invalid value for -maxpendingresponses_getheaders: '%s'"), gArgs.GetArg("-maxpendingresponses_getheaders", "")) );
        }
    }
    if (gArgs.IsArgSet("-maxpendingresponses_gethdrsen")) {
        auto v = gArgs.GetArg("-maxpendingresponses_gethdrsen", -1);
        if (v<0 || v>std::numeric_limits<unsigned int>::max()) {
            return InitError( strprintf(_("Invalid value for -maxpendingresponses_gethdrsen: '%s'"), gArgs.GetArg("-maxpendingresponses_gethdrsen", "")) );
        }
    }

    // Step 7: load block chain

    fReindex = gArgs.GetBoolArg("-reindex", false);
    bool fReindexChainState = gArgs.GetBoolArg("-reindex-chainstate", false);

    // cache size calculations
    int64_t nTotalCache = gArgs.GetArgAsBytes("-dbcache", nDefaultDbCache, ONE_MEBIBYTE);
    // total cache cannot be less than nMinDbCache
    nTotalCache = std::max(nTotalCache, nMinDbCache << 20);
    // total cache cannot be greater than nMaxDbcache
    nTotalCache = std::min(nTotalCache, nMaxDbCache << 20);
    int64_t nBlockTreeDBCache = nTotalCache / 8;
    nBlockTreeDBCache = std::min(nBlockTreeDBCache,
                                 (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)
                                      ? nMaxBlockDBAndTxIndexCache
                                      : nMaxBlockDBCache)
                                     << 20);
    nTotalCache -= nBlockTreeDBCache;
    // use 25%-50% of the remainder for disk cache
    int64_t nCoinDBCache =
        std::min(nTotalCache / 2, (nTotalCache / 4) + (1 << 23));
    // cap total coins db cache
    nCoinDBCache = std::min(nCoinDBCache, nMaxCoinsDBCache << 20);
    nTotalCache -= nCoinDBCache;
    // calculate cache for Merkle Tree database
    int64_t nMerkleTreeIndexDBCache = nBlockTreeDBCache / 4;
    nTotalCache -= nMerkleTreeIndexDBCache;
    // the rest goes to in-memory cache
    nCoinCacheUsage = nTotalCache;
    MempoolSizeLimits limits = MempoolSizeLimits::FromConfig();
    LogPrintf("Cache configuration:\n");
    LogPrintf("* Using %.1fMiB for block index database\n",
              nBlockTreeDBCache * (1.0 / ONE_MEBIBYTE));
    LogPrintf("* Using %.1fMiB for Merkle Tree index database\n",
              nMerkleTreeIndexDBCache * (1.0 / ONE_MEBIBYTE));
    LogPrintf("* Using %.1fMiB for chain state database\n",
              nCoinDBCache * (1.0 / ONE_MEBIBYTE));
    LogPrintf("* Using %.1fMiB for in-memory UTXO set (plus up to %.1fMiB of "
              "unused mempool space and %.1fMiB of disk space)\n",
              nCoinCacheUsage * (1.0 / 1024 / 1024),
              limits.Memory() * (1.0 / 1024 / 1024),
              limits.Disk() * (1.0 / 1024 / 1024));

    std::int64_t frozen_txo_db_cache_size = gArgs.GetArg("-frozentxodbcache", static_cast<std::int64_t>(DEFAULT_FROZEN_TXO_DB_CACHE));
    if(frozen_txo_db_cache_size<0)
    {
        return InitError(_("Negative value specified for -frozentxodbcache!"));
    }
    InitFrozenTXO(static_cast<std::size_t>(frozen_txo_db_cache_size));

    bool fLoaded = false;
    while (!fLoaded && !shutdownToken.IsCanceled()) {
        bool fReset = fReindex;
        std::string strLoadError;

        uiInterface.InitMessage(_("Loading block index..."));

        nStart = GetTimeMillis();
        do {
            try {
                UnloadBlockIndex();
                pcoinsTip.reset();
                delete pblocktree;

                pblocktree =
                    new CBlockTreeDB(nBlockTreeDBCache, false, fReindex);
                pMerkleTreeFactory = std::make_unique<CMerkleTreeFactory>(GetDataDir() / "merkle", static_cast<size_t>(nMerkleTreeIndexDBCache), GetMaxNumberOfMerkleTreeThreads());
                pcoinsTip =
                    std::make_unique<CoinsDB>(
                        config.GetMaxCoinsProviderCacheSize(),
                        nCoinDBCache,
                        CDBWrapper::MaxFiles{config.GetMaxCoinsDbOpenFiles()},
                        false,
                        fReindex || fReindexChainState);

                if (fReindex) {
                    pblocktree->WriteReindexing(true);
                    // If we're reindexing in prune mode, wipe away unusable
                    // block files and all undo data files
                    if (fPruneMode) {
                        CleanupBlockRevFiles();
                    }
                } else if (pcoinsTip->IsOldDBFormat()) {
                    strLoadError = _("Refusing to start, older database format detected");
                    break;
                }
                if (shutdownToken.IsCanceled()) break;

                if (!LoadBlockIndex(chainparams)) {
                    strLoadError = _("Error loading block database");
                    break;
                }

                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way
                // around).
                if (mapBlockIndex.Count() &&
                    mapBlockIndex.Get(
                        chainparams.GetConsensus().hashGenesisBlock) == nullptr)
                {
                    return InitError(_("Incorrect or no genesis block found. "
                                       "Wrong datadir for network?"));
                }

                // Initialize the block index (no-op if non-empty database was
                // already loaded)
                if (!InitBlockIndex(config)) {
                    strLoadError = _("Error initializing block database");
                    break;
                }

                // Check for changed -txindex state
                if (fTxIndex != gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
                    strLoadError = _("You need to rebuild the database using "
                                     "-reindex-chainstate to change -txindex");
                    break;
                }

                // Check for changed -prune state.  What we are concerned about
                // is a user who has pruned blocks in the past, but is now
                // trying to run unpruned.
                if (fHavePruned && !fPruneMode) {
                    strLoadError =
                        _("You need to rebuild the database using -reindex to "
                          "go back to unpruned mode.  This will redownload the "
                          "entire blockchain");
                    break;
                }

                if (!ReplayBlocks(config, *pcoinsTip)) {
                    strLoadError =
                        _("Unable to replay blocks. You will need to rebuild "
                          "the database using -reindex-chainstate.");
                    break;
                }

                {
                    LOCK(cs_main);
                    LoadChainTip(chainparams);
                }

                if (!fReindex && chainActive.Tip() != nullptr) {
                    uiInterface.InitMessage(_("Rewinding blocks..."));
                    if (!RewindBlockIndex(config)) {
                        strLoadError = _("Unable to rewind the database to a "
                                         "pre-fork state. You will need to "
                                         "redownload the blockchain");
                        break;
                    }
                }

                uiInterface.InitMessage(_("Verifying blocks..."));
                if (fHavePruned &&
                    gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS) >
                        config.GetMinBlocksToKeep()) {
                    LogPrintf("Prune: pruned datadir may not have more than %d "
                              "blocks; only checking available blocks\n",
                              config.GetMinBlocksToKeep());
                }

                {
                    LOCK(cs_main);
                    CBlockIndex *tip = chainActive.Tip();
                    RPCNotifyBlockChange(true, tip);
                    if (tip &&
                        tip->GetBlockTime() >
                            GetAdjustedTime() + MAX_FUTURE_BLOCK_TIME) {
                        strLoadError =
                            _("The block database contains a block which "
                              "appears to be from the future. "
                              "This may be due to your computer's date and "
                              "time being set incorrectly. "
                              "Only rebuild the block database if you are sure "
                              "that your computer's date and time are correct");
                        break;
                    }
                }

                if (!CVerifyDB().VerifyDB(
                        config, *pcoinsTip,
                        gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL),
                        gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS),
                        shutdownToken)) {
                    strLoadError = _("Corrupted block database detected");
                    break;
                }
                // Make sure that nothing stays in cache as VerifyDB loads coins
                // from database.
                FlushStateToDisk();

                InvalidateBlocksFromConfig(config);

            } catch (const std::exception &e) {
                LogPrintf("%s\n", e.what());
                strLoadError = _("Error opening block database");
                break;
            }

            fLoaded = true;
        } while (false);

        if (!fLoaded && !shutdownToken.IsCanceled()) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeQuestion(
                    strLoadError + ".\n\n" +
                        _("Do you want to rebuild the block database now?"),
                    strLoadError + ".\nPlease restart with -reindex or "
                                   "-reindex-chainstate to recover.",
                    "",
                    CClientUIInterface::MSG_ERROR |
                        CClientUIInterface::BTN_ABORT);
                if (fRet && !shutdownToken.IsCanceled()) {
                    fReindex = true;
                } else {
                    LogPrintf("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes.
    // As the program has not fully started yet, Shutdown() is possibly
    // overkill.
    if (shutdownToken.IsCanceled()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    // After block chain is loaded check fork tip statuses and
    // restore global safe mode state.
    CheckSafeModeParametersForAllForksOnStartup(config);

    LogPrintf(" block index %15dms\n", GetTimeMillis() - nStart);

// Step 8: load wallet
#ifdef ENABLE_WALLET
    if (!CWallet::InitLoadWallet(chainparams)) return false;
#else
    LogPrintf("No wallet support compiled in!\n");
#endif

    gArgs.LogSetParameters();

    // Step 9: data directory maintenance

    // if pruning, unset the service bit and perform the initial blockstore
    // prune after any wallet rescanning has taken place.
    if (fPruneMode) {
        LogPrintf("Unsetting NODE_NETWORK on prune mode\n");
        nLocalServices = ServiceFlags(nLocalServices & ~NODE_NETWORK);
        if (!fReindex) {
            uiInterface.InitMessage(_("Pruning blockstore..."));
            PruneAndFlush();
        }
    }

    // Step 10: import blocks

    if (!CheckDiskSpace()) {
        return false;
    }

    // Either install a handler to notify us when genesis activates, or set
    // fHaveGenesis directly.
    // No locking, as this happens before any background thread is started.
    if (chainActive.Tip() == nullptr) {
        uiInterface.NotifyBlockTip.connect(BlockNotifyGenesisWait);
    } else {
        fHaveGenesis = true;
    }

    if (gArgs.IsArgSet("-blocknotify")) {
        uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);
    }

    std::vector<fs::path> vImportFiles;
    if (gArgs.IsArgSet("-loadblock")) {
        for (const std::string &strFile : gArgs.GetArgs("-loadblock")) {
            vImportFiles.push_back(strFile);
        }
    }

    threadGroup.create_thread(
        [&config, vImportFiles, shutdownToken]
        {
            TraceThread(
                "import_files",
                [&config, &vImportFiles, shutdownToken]{ThreadImport(config, vImportFiles, shutdownToken);});
        });

    // Wait for genesis block to be processed
    {
        boost::unique_lock<boost::mutex> lock(cs_GenesisWait);
        while (!fHaveGenesis) {
            condvar_GenesisWait.wait(lock);
        }
        uiInterface.NotifyBlockTip.disconnect(&BlockNotifyGenesisWait);
    }

    preloadChainState(threadGroup);

    // Create minerID database  and dataref index if required
    if(config.GetMinerIdEnabled()) {
        try {
            g_minerIDs = std::make_unique<MinerIdDatabase>(config);
            ScheduleMinerIdPeriodicTasks(scheduler, *g_minerIDs);
        }
        catch(const std::exception& e) {
            LogPrintf("Error creating miner ID database: %s\n", e.what());
        }
        try {
            g_dataRefIndex = std::make_unique<DataRefTxnDB>(config);
        }
        catch(const std::exception& e) {
            LogPrintf("Error creating dataRef index: %s\n", e.what());
        }
    }

    // Step 11: start node

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n", mapBlockIndex.Count());
    LogPrintf("nBestHeight = %d\n", chainActive.Height());

    Discover(threadGroup);

    // Map ports with UPnP
    MapPort(gArgs.GetBoolArg("-upnp", DEFAULT_UPNP));

    std::string strNodeError;
    CConnman::Options connOptions;
    connOptions.nLocalServices = nLocalServices;
    connOptions.nRelevantServices = nRelevantServices;
    connOptions.nMaxConnections = nMaxConnections;
    connOptions.nMaxConnectionsFromAddr = nMaxConnectionsFromAddr;
    connOptions.nMaxOutbound = nMaxOutboundConnections;
    connOptions.nMaxAddnode = config.GetMaxAddNodeConnections();
    connOptions.nMaxFeeler = 1;
    connOptions.nBestHeight = chainActive.Height();
    connOptions.uiInterface = &uiInterface;
    connOptions.nSendBufferMaxSize =
        gArgs.GetArgAsBytes("-maxsendbuffer", DEFAULT_MAXSENDBUFFER, ONE_KILOBYTE);
    connOptions.nReceiveFloodSize =
        gArgs.GetArgAsBytes("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER, ONE_KILOBYTE);

    connOptions.nMaxOutboundTimeframe = nMaxOutboundTimeframe;
    connOptions.nMaxOutboundLimit = nMaxOutboundLimit;

    if (!connman.Start(scheduler, strNodeError, connOptions)) {
        return InitError(strNodeError);
    }

    // Create mining factory
    assert(!mining::g_miningFactory);
    mining::g_miningFactory = std::make_unique<mining::CMiningFactory>(config);

    // Launch non-final mempool periodic checks
    mempool.getNonFinalPool().startPeriodicChecks(scheduler);

    // Create webhook client
    assert(!rpc::client::g_pWebhookClient);
    rpc::client::g_pWebhookClient = std::make_unique<rpc::client::WebhookClient>(config);

    // Step 12: finished

    SetRPCWarmupFinished();

    uiInterface.InitMessage(_("Done loading"));

#ifdef ENABLE_WALLET
    for (CWalletRef pwallet : vpwallets) {
        pwallet->postInitProcess(scheduler);
    }
#endif

    return !shutdownToken.IsCanceled();
}

// Get/set AppInit finished flag
std::atomic_bool& GetAppInitCompleted()
{
    static std::atomic_bool appInitCompleted {false};
    return appInitCompleted;
}

