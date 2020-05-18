// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include "addrdb.h"
#include "addrman.h"
#include "amount.h"
#include "bloom.h"
#include "chainparams.h"
#include "compat.h"
#include "fs.h"
#include "hash.h"
#include "limitedmap.h"
#include "netaddress.h"
#include "protocol.h"
#include "random.h"
#include "streams.h"
#include "sync.h"
#include "task_helpers.h"
#include "threadinterrupt.h"
#include "txmempool.h"
#include "txn_sending_details.h"
#include "uint256.h"
#include "validation.h"

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <thread>
#include <vector>
#include <functional>

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include <boost/circular_buffer.hpp>
#include <boost/signals2/signal.hpp>

class CAddrMan;
class Config;
class CNode;
class CScheduler;
class CTxnPropagator;
class CTxnValidator;

using CNodePtr = std::shared_ptr<CNode>;

namespace boost {
class thread_group;
} // namespace boost

namespace task
{
    class CCancellationSource;
}

/** Time between pings automatically sent out for latency probing and keepalive
 * (in seconds). */
static const int PING_INTERVAL = 2 * 60;
/** Time after which to disconnect, after waiting for a ping response (or
 * inactivity). */
static const int DEFAULT_P2P_TIMEOUT_INTERVAL = 20 * 60;
/** Run the feeler connection loop once every 2 minutes or 120 seconds. **/
static const int FEELER_INTERVAL = 120;
/** The maximum number of new addresses to accumulate before announcing. */
static const unsigned int MAX_ADDR_TO_SEND = 1000;
/** Maximum length of strSubVer in `version` message */
static const unsigned int MAX_SUBVERSION_LENGTH = 256;
/** Maximum number of automatic outgoing nodes */
static const int MAX_OUTBOUND_CONNECTIONS = 8;
/** Maximum number of addnode outgoing nodes */
static const int MAX_ADDNODE_CONNECTIONS = 8;
/** -listen default */
static const bool DEFAULT_LISTEN = true;
/** -upnp default */
#ifdef USE_UPNP
static const bool DEFAULT_UPNP = USE_UPNP;
#else
static const bool DEFAULT_UPNP = false;
#endif
/** The maximum number of peer connections to maintain. */
static const unsigned int DEFAULT_MAX_PEER_CONNECTIONS = 125;
/** The default for -maxuploadtarget. 0 = Unlimited */
static const uint64_t DEFAULT_MAX_UPLOAD_TARGET = 0;
/** The default timeframe for -maxuploadtarget. 1 day. */
static const uint64_t MAX_UPLOAD_TIMEFRAME = 60 * 60 * 24;
/** Default for blocks only*/
static const bool DEFAULT_BLOCKSONLY = false;
/** Default factor that will be multiplied with excessiveBlockSize
* to limit the maximum bytes in all sending queues. If this
* size is exceeded, no response to block related P2P messages is sent.
**/
static const unsigned int DEFAULT_FACTOR_MAX_SEND_QUEUES_BYTES = 4;
/** Microseconds in a second */
static const unsigned int MICROS_PER_SECOND = 1000000;
/** Peer average bandwidth measurement interval */
static const unsigned PEER_AVG_BANDWIDTH_CALC_FREQUENCY_SECS = 5;

// Force DNS seed use ahead of UAHF fork, to ensure peers are found
// as long as seeders are working.
// TODO: Change this back to false after the forked network is stable.
static const bool DEFAULT_FORCEDNSSEED = true;
static const size_t DEFAULT_MAXRECEIVEBUFFER = 5 * 1000;
static const size_t DEFAULT_MAXSENDBUFFER = 1 * 1000;

static const ServiceFlags REQUIRED_SERVICES = ServiceFlags(NODE_NETWORK);

// Default 24-hour ban.
// NOTE: When adjusting this, update rpcnet:setban's help ("24h")
static const unsigned int DEFAULT_MISBEHAVING_BANTIME = 60 * 60 * 24;

/**
 * Default maximum amount of concurrent async tasks per node before node message
 * processing is skipped until the amount is freed up again.
 */
constexpr size_t DEFAULT_NODE_ASYNC_TASKS_LIMIT = 3;

typedef int64_t NodeId;

struct AddedNodeInfo {
    std::string strAddedNode;
    CService resolvedAddress;
    bool fConnected;
    bool fInbound;
};

class CGetBlockMessageRequest
{
public:
    CGetBlockMessageRequest(CDataStream& vRecv)
        : mRequestTime{std::chrono::system_clock::now()}
    {
        vRecv >> mLocator >> mHashStop;
    }

    auto GetRequestTime() const
        -> const std::chrono::time_point<std::chrono::system_clock>&
    {
        return mRequestTime;
    }
    const CBlockLocator& GetLocator() const {return mLocator;}
    const uint256& GetHashStop() const {return mHashStop;}
private:
    std::chrono::time_point<std::chrono::system_clock> mRequestTime;
    CBlockLocator mLocator;
    uint256 mHashStop;
};

class CTransaction;
class CNodeStats;
class CClientUIInterface;

class CSerializedNetMsg
{
public:
    CSerializedNetMsg(CSerializedNetMsg &&) = default;
    CSerializedNetMsg &operator=(CSerializedNetMsg &&) = default;
    // No copying, only moves.
    CSerializedNetMsg(const CSerializedNetMsg &msg) = delete;
    CSerializedNetMsg &operator=(const CSerializedNetMsg &) = delete;

    CSerializedNetMsg(
        std::string&& command,
        std::vector<uint8_t>&& data)
        : mCommand{std::move(command)}
        , mHash{::Hash(data.data(), data.data() + data.size())}
        , mSize{data.size()}
        , mData{std::make_unique<CVectorStream>(std::move(data))}
    {/**/}

    CSerializedNetMsg(
        std::string&& command,
        const uint256& hash,
        size_t size,
        std::unique_ptr<CForwardAsyncReadonlyStream> data)
        : mCommand{std::move(command)}
        , mHash{hash}
        , mSize{size}
        , mData{std::move(data)}
    {/**/}

    const std::string& Command() const {return mCommand;}
    std::unique_ptr<CForwardAsyncReadonlyStream> MoveData() {return std::move(mData);}
    const uint256& Hash() const {return mHash;}
    size_t Size() const {return mSize;}

private:
    std::string mCommand;
    uint256 mHash;
    size_t mSize;
    std::unique_ptr<CForwardAsyncReadonlyStream> mData;
};

class CConnman {
public:
    enum NumConnections {
        CONNECTIONS_NONE = 0,
        CONNECTIONS_IN = (1U << 0),
        CONNECTIONS_OUT = (1U << 1),
        CONNECTIONS_ALL = (CONNECTIONS_IN | CONNECTIONS_OUT),
    };

    struct Options {
        ServiceFlags nLocalServices = NODE_NONE;
        ServiceFlags nRelevantServices = NODE_NONE;
        int nMaxConnections = 0;
        int nMaxOutbound = 0;
        int nMaxAddnode = 0;
        int nMaxFeeler = 0;
        int nBestHeight = 0;
        CClientUIInterface *uiInterface = nullptr;
        unsigned int nSendBufferMaxSize = 0;
        unsigned int nReceiveFloodSize = 0;
        uint64_t nMaxOutboundTimeframe = 0;
        uint64_t nMaxOutboundLimit = 0;
    };
    CConnman(
        const Config &configIn,
        uint64_t seed0,
        uint64_t seed1,
        std::chrono::milliseconds debugP2PTheadStallsThreshold);
    ~CConnman();
    bool Start(CScheduler &scheduler, std::string &strNodeError,
               Options options);
    void Stop();
    void Interrupt();
    bool BindListenPort(const CService &bindAddr, std::string &strError,
                        bool fWhitelisted = false);
    bool GetNetworkActive() const { return fNetworkActive; };
    void SetNetworkActive(bool active);
    bool OpenNetworkConnection(const CAddress &addrConnect, bool fCountFailure,
                               CSemaphoreGrant *grantOutbound = nullptr,
                               const char *strDest = nullptr,
                               bool fOneShot = false, bool fFeeler = false,
                               bool fAddnode = false);
    bool CheckIncomingNonce(uint64_t nonce);

    bool ForNode(NodeId id, std::function<bool(const CNodePtr& pnode)> func);

    void PushMessage(const CNodePtr& pnode, CSerializedNetMsg &&msg);

    /** Enqueue a new transaction for later sending to our peers */
    void EnqueueTransaction(const CTxnSendingDetails& txn);
    /** Remove some transactions from our peers list of new transactions */
    void DequeueTransactions(const std::vector<CTransactionRef>& txns);

    /** Get a handle to our transaction propagator */
    const std::shared_ptr<CTxnPropagator>& getTransactionPropagator() const { return mTxnPropagator; }

    /** Call the specified function for each node */
    template <typename Callable> void ForEachNode(Callable&& func) const {
        LOCK(cs_vNodes);
        for(const CNodePtr& node : vNodes) {
            if(NodeFullyConnected(node))
                func(node);
        }
    };

    /** Call the specified function for each node in parallel */
    template <typename Callable>
    auto ParallelForEachNode(Callable&& func)
        -> std::vector<std::future<typename std::result_of<Callable(const CNodePtr&)>::type>>
    {
        using resultType = typename std::result_of<Callable(const CNodePtr&)>::type;
        std::vector<std::future<resultType>> results {};

        LOCK(cs_vNodes);
        results.reserve(vNodes.size());
        for(const CNodePtr& node : vNodes) {
            if(NodeFullyConnected(node))
                results.emplace_back(make_task(mThreadPool, func, node));
        }

        return results;
    };

    /** Call the specified function for parallel validation */
    template <typename Callable>
    auto ParallelTxnValidation(
            Callable&& func,
            const Config* config,
            CTxMemPool *pool,
            TxInputDataSPtrVec& vNewTxns,
            CTxnHandlers& handlers,
            bool fUseTimedCancellationSource,
            std::chrono::milliseconds maxasynctasksrunduration)
        -> std::vector<std::future<typename std::result_of<
            Callable(const TxInputDataSPtr&,
                const Config*,
                CTxMemPool*,
                CTxnHandlers&,
                bool,
                std::chrono::steady_clock::time_point)>::type>> {
        using resultType = typename std::result_of<
            Callable(const TxInputDataSPtr&,
                const Config*,
                CTxMemPool*,
                CTxnHandlers&,
                bool,
                std::chrono::steady_clock::time_point)>::type;
        // A variable which stors results
        std::vector<std::future<resultType>> results {};
        // Allocate a buffer for results
        results.reserve(vNewTxns.size());
        // Set the time point
        std::chrono::steady_clock::time_point zero_time_point(std::chrono::milliseconds(0));
        std::chrono::steady_clock::time_point end_time_point =
            std::chrono::steady_clock::time_point(maxasynctasksrunduration) == zero_time_point
                ? zero_time_point : std::chrono::steady_clock::now() + maxasynctasksrunduration;
        // Create validation tasks
        for (const TxInputDataSPtr& txn : vNewTxns) {
            results.emplace_back(
                make_task(
                    mValidatorThreadPool,
                    txn->mTxValidationPriority == TxValidationPriority::low ? CTask::Priority::Low : CTask::Priority::High,
                    func,
                    txn,
                    config,
                    pool,
                    handlers,
                    fUseTimedCancellationSource,
                    end_time_point));
        }
        return results;
    };

    /** Get a handle to our transaction validator */
    std::shared_ptr<CTxnValidator> getTxnValidator();
    /** Enqueue a new transaction for validation */
    void EnqueueTxnForValidator(TxInputDataSPtr pTxInputData);
    /* Support for a vector */
    void EnqueueTxnForValidator(std::vector<TxInputDataSPtr> vTxInputData);
    /** Resubmit a transaction for validation */
    void ResubmitTxnForValidator(TxInputDataSPtr pTxInputData);
    /** Check if the given txn is already known by the Validator */
    bool CheckTxnExistsInValidatorsQueue(const uint256& txHash) const;
    /* Find node by it's id */
    CNodePtr FindNodeById(int64_t nodeId);
    /* Erase transaction from the given peer */
    void EraseOrphanTxnsFromPeer(NodeId peer);
    /* Erase transaction by it's hash */
    int EraseOrphanTxn(const uint256& txHash);
    /* Check if orphan transaction exists by prevout */
    bool CheckOrphanTxnExists(const COutPoint& prevout) const;
    /* Check if orphan transaction exists by txn hash */
    bool CheckOrphanTxnExists(const uint256& txHash) const;
    /* Get transaction's hash for orphan transactions (by prevout) */
    std::vector<uint256> GetOrphanTxnsHash(const COutPoint& prevout) const;
    /* Check if transaction exists in recent rejects */
    bool CheckTxnInRecentRejects(const uint256& txHash) const;
    /* Reset recent rejects */
    void ResetRecentRejects();
    /* Get extra txns for block reconstruction */
    std::vector<std::pair<uint256, CTransactionRef>> GetCompactExtraTxns() const;

    // Addrman functions
    size_t GetAddressCount() const;
    void SetServices(const CService &addr, ServiceFlags nServices);
    void MarkAddressGood(const CAddress &addr);
    void AddNewAddress(const CAddress &addr, const CAddress &addrFrom,
                       int64_t nTimePenalty = 0);
    void AddNewAddresses(const std::vector<CAddress> &vAddr,
                         const CAddress &addrFrom, int64_t nTimePenalty = 0);
    std::vector<CAddress> GetAddresses();
    void AddressCurrentlyConnected(const CService &addr);

    // Denial-of-service detection/prevention. The idea is to detect peers that
    // are behaving badly and disconnect/ban them, but do it in a
    // one-coding-mistake-won't-shatter-the-entire-network way.
    // IMPORTANT: There should be nothing I can give a node that it will forward
    // on that will make that node's peers drop it. If there is, an attacker can
    // isolate a node and/or try to split the network. Dropping a node for
    // sending stuff that is invalid now but might be valid in a later version
    // is also dangerous, because it can cause a network split between nodes
    // running old code and nodes running new code.
    void Ban(const CNetAddr &netAddr, const BanReason &reason,
             int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    void Ban(const CSubNet &subNet, const BanReason &reason,
             int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    // Needed for unit testing.
    void ClearBanned();
    bool IsBanned(CNetAddr ip);
    bool IsBanned(CSubNet subnet);
    bool Unban(const CNetAddr &ip);
    bool Unban(const CSubNet &ip);
    void GetBanned(banmap_t &banmap);
    void SetBanned(const banmap_t &banmap);

    void AddOneShot(const std::string &strDest);

    bool AddNode(const std::string &node);
    bool RemoveAddedNode(const std::string &node);
    std::vector<AddedNodeInfo> GetAddedNodeInfo();

    size_t GetNodeCount(NumConnections num);
    void GetNodeStats(std::vector<CNodeStats> &vstats);
    bool DisconnectNode(const std::string &node);
    bool DisconnectNode(NodeId id);

    unsigned int GetSendBufferSize() const;

    void AddWhitelistedRange(const CSubNet &subnet);

    ServiceFlags GetLocalServices() const;

    //! set the max outbound target in bytes.
    void SetMaxOutboundTarget(uint64_t limit);
    uint64_t GetMaxOutboundTarget();

    //! set the timeframe for the max outbound target.
    void SetMaxOutboundTimeframe(uint64_t timeframe);
    uint64_t GetMaxOutboundTimeframe();

    //! check if the outbound target is reached.
    // If param historicalBlockServingLimit is set true, the function will
    // response true if the limit for serving historical blocks has been
    // reached.
    bool OutboundTargetReached(bool historicalBlockServingLimit);

    //! response the bytes left in the current max outbound cycle
    // in case of no limit, it will always response 0
    uint64_t GetOutboundTargetBytesLeft();

    //! response the time in second left in the current max outbound cycle
    // in case of no limit, it will always response 0
    uint64_t GetMaxOutboundTimeLeftInCycle();

    uint64_t GetTotalBytesRecv();
    uint64_t GetTotalBytesSent();

    void SetBestHeight(int height);
    int GetBestHeight() const;

    /** Get a unique deterministic randomizer. */
    CSipHasher GetDeterministicRandomizer(uint64_t id) const;

    unsigned int GetReceiveFloodSize() const;

    void WakeMessageHandler();

    // Task pool for executing async node tasks. Task queue size is implicitly
    // limited by maximum allowed connections (DEFAULT_MAX_PEER_CONNECTIONS)
    // times maximum async requests that a node may have active at any given
    // time.
    class CAsyncTaskPool
    {
    public:
        CAsyncTaskPool(const Config& config);
        ~CAsyncTaskPool();

        void AddToPool(
            const std::shared_ptr<CNode>& node,
            std::function<void(std::weak_ptr<CNode>)> function,
            std::shared_ptr<task::CCancellationSource> source);

        bool HasReachedSoftAsyncTaskLimit(NodeId id)
        {
            return
                std::count_if(
                    mRunningTasks.begin(),
                    mRunningTasks.end(),
                    [id](const CRunningTask& container)
                    {
                        return container.mId == id;
                    }) >= mPerInstanceSoftAsyncTaskLimit;
        }

        /**
         * Node can be used to execute some code on a different thread to return
         * control back to CConnman. Each node stores its pending futures that are
         * removed once the task is done.
         */
        void HandleCompletedAsyncProcessing();

    private:
        struct CRunningTask
        {
            CRunningTask(
                NodeId id,
                std::future<void>&& future,
                std::shared_ptr<task::CCancellationSource>&& cancellationSource)
                : mId{id}
                , mFuture{std::move(future)}
                , mCancellationSource{std::move(cancellationSource)}
            {/**/}
            NodeId mId;
            std::future<void> mFuture;
            std::shared_ptr<task::CCancellationSource> mCancellationSource;
        };

        CThreadPool<CQueueAdaptor> mPool;
        std::vector<CRunningTask> mRunningTasks;
        int mPerInstanceSoftAsyncTaskLimit;
    };

private:
    struct ListenSocket {
        SOCKET socket;
        bool whitelisted;

        ListenSocket(SOCKET socket_, bool whitelisted_)
            : socket(socket_), whitelisted(whitelisted_) {}
    };

    void ThreadOpenAddedConnections();
    void ProcessOneShot();
    void ThreadOpenConnections();
    void ThreadMessageHandler();
    void AcceptConnection(const ListenSocket &hListenSocket);
    void ThreadSocketHandler();
    void ThreadDNSAddressSeed();

    uint64_t CalculateKeyedNetGroup(const CAddress &ad) const;

    CNodePtr FindNode(const CNetAddr &ip);
    CNodePtr FindNode(const CSubNet &subNet);
    CNodePtr FindNode(const std::string &addrName);
    CNodePtr FindNode(const CService &addr);

    bool AttemptToEvictConnection();
    CNodePtr ConnectNode(CAddress addrConnect, const char *pszDest,
                         bool fCountFailure);
    bool IsWhitelistedRange(const CNetAddr &addr);

    void DeleteNode(const CNodePtr& pnode);

    NodeId GetNewNodeId();

    size_t SocketSendData(const CNodePtr& pnode) const;
    //! check is the banlist has unwritten changes
    bool BannedSetIsDirty();
    //! set the "dirty" flag for the banlist
    void SetBannedSetDirty(bool dirty = true);
    //! clean unused entries (if bantime has expired)
    void SweepBanned();
    void DumpAddresses();
    void DumpData();
    void DumpBanlist();

    // Network stats
    void RecordBytesRecv(uint64_t bytes);
    void RecordBytesSent(uint64_t bytes);

    // Whether the node should be passed out in ForEach* callbacks
    static bool NodeFullyConnected(const CNodePtr& pnode);

    // Peer average bandwidth calculation
    void PeerAvgBandwithCalc();

    const Config *config;

    // Network usage totals
    CCriticalSection cs_totalBytesRecv;
    CCriticalSection cs_totalBytesSent;
    uint64_t nTotalBytesRecv;
    uint64_t nTotalBytesSent;

    // outbound limit & stats
    uint64_t nMaxOutboundTotalBytesSentInCycle;
    uint64_t nMaxOutboundCycleStartTime;
    uint64_t nMaxOutboundLimit;
    uint64_t nMaxOutboundTimeframe;

    // Whitelisted ranges. Any node connecting from these is automatically
    // whitelisted (as well as those connecting to whitelisted binds).
    std::vector<CSubNet> vWhitelistedRange;
    CCriticalSection cs_vWhitelistedRange;

    unsigned int nSendBufferMaxSize;
    unsigned int nReceiveFloodSize;

    std::vector<ListenSocket> vhListenSocket;
    std::atomic<bool> fNetworkActive;
    banmap_t setBanned;
    CCriticalSection cs_setBanned;
    bool setBannedIsDirty;
    bool fAddressesInitialized;
    CAddrMan addrman;
    std::deque<std::string> vOneShots;
    CCriticalSection cs_vOneShots;
    std::vector<std::string> vAddedNodes;
    CCriticalSection cs_vAddedNodes;
    std::vector<CNodePtr> vNodes;
    std::list<CNodePtr> vNodesDisconnected;
    mutable CCriticalSection cs_vNodes;
    std::atomic<NodeId> nLastNodeId;

    /** Services this instance offers */
    ServiceFlags nLocalServices;

    /** Services this instance cares about */
    ServiceFlags nRelevantServices;

    std::shared_ptr<CSemaphore> semOutbound {nullptr};
    std::shared_ptr<CSemaphore> semAddnode {nullptr};
    int nMaxConnections;
    int nMaxOutbound;
    int nMaxAddnode;
    int nMaxFeeler;
    std::atomic<int> nBestHeight;
    CClientUIInterface *clientInterface;

    /** SipHasher seeds for deterministic randomness */
    const uint64_t nSeed0, nSeed1;

    /** flag for waking the message processor. */
    bool fMsgProcWake;

    std::condition_variable condMsgProc;
    std::mutex mutexMsgProc;
    std::atomic<bool> flagInterruptMsgProc;

    /** Transaction tracker/propagator */
    std::shared_ptr<CTxnPropagator> mTxnPropagator {};

    CThreadPool<CQueueAdaptor> mThreadPool { "ConnmanPool" };

    /** Transaction validator */
    std::shared_ptr<CTxnValidator> mTxnValidator {};
    CThreadPool<CDualQueueAdaptor> mValidatorThreadPool;

    CThreadInterrupt interruptNet;

    std::thread threadDNSAddressSeed;
    std::thread threadSocketHandler;
    std::thread threadOpenAddedConnections;
    std::thread threadOpenConnections;
    std::thread threadMessageHandler;

    std::chrono::milliseconds mDebugP2PTheadStallsThreshold;

    CAsyncTaskPool mAsyncTaskPool;
    uint8_t mNodeAsyncTaskLimit;
};
extern std::unique_ptr<CConnman> g_connman;
void Discover(boost::thread_group &threadGroup);
void MapPort(bool fUseUPnP);
unsigned short GetListenPort();
bool BindListenPort(const CService &bindAddr, std::string &strError,
                    bool fWhitelisted = false);

struct CombinerAll {
    typedef bool result_type;

    template <typename I> bool operator()(I first, I last) const {
        while (first != last) {
            if (!(*first)) {
                return false;
            }
            ++first;
        }
        return true;
    }
};

// Signals for message handling
struct CNodeSignals {
    boost::signals2::signal<bool(const Config &, const CNodePtr& , CConnman &,
                                 std::atomic<bool> &),
                            CombinerAll>
        ProcessMessages;
    boost::signals2::signal<bool(const Config &, const CNodePtr& , CConnman &,
                                 std::atomic<bool> &),
                            CombinerAll>
        SendMessages;
    boost::signals2::signal<void(const CNodePtr& , CConnman &)>
        InitializeNode;
    boost::signals2::signal<void(NodeId, bool &)> FinalizeNode;
};

CNodeSignals &GetNodeSignals();

enum {
    // unknown
    LOCAL_NONE,
    // address a local interface listens on
    LOCAL_IF,
    // address explicit bound to
    LOCAL_BIND,
    // address reported by UPnP
    LOCAL_UPNP,
    // address explicitly specified (-externalip=)
    LOCAL_MANUAL,

    LOCAL_MAX
};

bool IsPeerAddrLocalGood(const CNodePtr& pnode);
void AdvertiseLocal(const CNodePtr& pnode);
void SetLimited(enum Network net, bool fLimited = true);
bool IsLimited(enum Network net);
bool IsLimited(const CNetAddr &addr);
bool AddLocal(const CService &addr, int nScore = LOCAL_NONE);
bool AddLocal(const CNetAddr &addr, int nScore = LOCAL_NONE);
bool RemoveLocal(const CService &addr);
bool SeenLocal(const CService &addr);
bool IsLocal(const CService &addr);
bool GetLocal(CService &addr, const CNetAddr *paddrPeer = nullptr);
bool IsReachable(enum Network net);
bool IsReachable(const CNetAddr &addr);
CAddress GetLocalAddress(const CNetAddr *paddrPeer,
                         ServiceFlags nLocalServices);

extern bool fDiscover;
extern bool fListen;
extern bool fRelayTxes;

extern CCriticalSection cs_invQueries;
extern limitedmap<uint256, int64_t> mapAlreadyAskedFor;

struct LocalServiceInfo {
    int nScore;
    int nPort;
};

extern CCriticalSection cs_mapLocalHost;
extern std::map<CNetAddr, LocalServiceInfo> mapLocalHost;

// Command, total bytes
typedef std::map<std::string, uint64_t> mapMsgCmdSize;

class CNodeStats {
public:
    NodeId nodeid;
    ServiceFlags nServices;
    bool fRelayTxes;
    int64_t nLastSend;
    int64_t nLastRecv;
    bool fPauseSend;
    bool fPauseRecv;
    int64_t nSendSize;
    int64_t nTimeConnected;
    int64_t nTimeOffset;
    std::string addrName;
    int nVersion;
    std::string cleanSubVer;
    bool fInbound;
    bool fAddnode;
    int nStartingHeight;
    uint64_t nSendBytes;
    mapMsgCmdSize mapSendBytesPerMsgCmd;
    uint64_t nRecvBytes;
    mapMsgCmdSize mapRecvBytesPerMsgCmd;
    bool fWhitelisted;
    double dPingTime;
    double dPingWait;
    double dMinPing;
    // What this peer sees as my address
    std::string addrLocal;
    CAddress addr;
    size_t nInvQueueSize;
    uint64_t nSpotBytesPerSec;
    uint64_t nMinuteBytesPerSec;
};

class CNetMessage {
private:
    mutable CHash256 hasher;
    mutable uint256 data_hash;

public:
    // Parsing header (false) or data (true)
    bool in_data;

    // Partially received header.
    CDataStream hdrbuf;
    // Complete header.
    CMessageHeader hdr;
    uint32_t nHdrPos;

    // Received message data.
    CDataStream vRecv;
    uint32_t nDataPos;

    // Time (in microseconds) of message receipt.
    int64_t nTime;

    CNetMessage(const CMessageHeader::MessageMagic &pchMessageStartIn,
                int nTypeIn, int nVersionIn)
        : hdrbuf(nTypeIn, nVersionIn), hdr(pchMessageStartIn),
          vRecv(nTypeIn, nVersionIn) {
        hdrbuf.resize(24);
        in_data = false;
        nHdrPos = 0;
        nDataPos = 0;
        nTime = 0;
    }

    bool complete() const {
        if (!in_data) {
            return false;
        }

        return (hdr.nPayloadLength == nDataPos);
    }

    const uint256 &GetMessageHash() const;

    void SetVersion(int nVersionIn) {
        hdrbuf.SetVersion(nVersionIn);
        vRecv.SetVersion(nVersionIn);
    }

    int readHeader(const Config &config, const char *pch, uint32_t nBytes);
    int readData(const char *pch, uint32_t nBytes);
};

class CSendQueueBytes {
    // nSendQueueBytes holds data of how many bytes are currently in queue for specific node
    size_t nSendQueueBytes = 0;
    // nTotalSendQueuesBytes holds data of how many bytes are currently in all queues across the network (all nodes)
    static std::atomic_size_t nTotalSendQueuesBytes;

public:
    ~CSendQueueBytes() {
        nTotalSendQueuesBytes -= nSendQueueBytes;
    }

    size_t operator-= (size_t nBytes) {
        nSendQueueBytes -= nBytes;
        nTotalSendQueuesBytes -= nBytes;
        return nSendQueueBytes;
    }

     size_t operator+= (size_t nBytes) {
        nSendQueueBytes += nBytes;
        nTotalSendQueuesBytes += nBytes;
        return nSendQueueBytes;
    }

    size_t getSendQueueBytes() const {
        return nSendQueueBytes;
    }

    static size_t getTotalSendQueuesBytes() {
        return nTotalSendQueuesBytes;
    }
};

/** Information about a peer */
class CNode : public std::enable_shared_from_this<CNode>
{
    friend class CConnman;

public:
    /**
     * Notification structure for SendMessage function that returns:
     * sendComplete: whether the send was fully complete/partially complete and
     *               data is needed for sending the rest later.
     * sentSize: amount of data that was sent.
     */
    struct CSendResult
    {
        bool sendComplete;
        size_t sentSize;
    };

    // socket
    std::atomic<ServiceFlags> nServices {NODE_NONE};
    // Services expected from a peer, otherwise it will be disconnected
    ServiceFlags nServicesExpected {NODE_NONE};
    SOCKET hSocket {0};
    // Total size of all vSendMsg entries.
    CSendQueueBytes nSendSize;
    std::deque<std::unique_ptr<CForwardAsyncReadonlyStream>> vSendMsg {};
    CCriticalSection cs_vSend {};
    CCriticalSection cs_hSocket {};
    CCriticalSection cs_vRecv {};

    CCriticalSection cs_vProcessMsg {};
    std::list<CNetMessage> vProcessMsg {};
    size_t nProcessQueueSize {0};

    CCriticalSection cs_sendProcessing {};

    std::optional<CGetBlockMessageRequest> mGetBlockMessageRequest;
    std::deque<CInv> vRecvGetData {};
    uint64_t nRecvBytes {0};
    std::atomic<int> nRecvVersion {INIT_PROTO_VERSION};

    /** Average bandwidth measurements */
    // Keep enough spot measurements to cover 1 minute
    boost::circular_buffer<double> vAvgBandwidth {60 / PEER_AVG_BANDWIDTH_CALC_FREQUENCY_SECS};
    // Time we last took a spot measurement
    int64_t nLastSpotMeasurementTime { GetTimeMicros() };
    // Bytes received since last spot measurement
    uint64_t nBytesRecvThisSpot {0};

    std::atomic<int64_t> nLastSend {0};
    std::atomic<int64_t> nLastRecv {0};
    const int64_t nTimeConnected {0};
    std::atomic<int64_t> nTimeOffset {0};
    // The address of the remote peer
    const CAddress addr {};
    std::atomic<int> nVersion {0};
    // strSubVer is whatever byte array we read from the wire. However, this
    // field is intended to be printed out, displayed to humans in various forms
    // and so on. So we sanitize it and store the sanitized version in
    // cleanSubVer. The original should be used when dealing with the network or
    // wire types and the cleaned string used when displayed or logged.
    std::string strSubVer {}, cleanSubVer {};
    // Used for both cleanSubVer and strSubVer.
    CCriticalSection cs_SubVer {};
    // This peer can bypass DoS banning.
    std::atomic_bool fWhitelisted {false};
    // If true this node is being used as a short lived feeler.
    bool fFeeler {false};
    bool fOneShot {false};
    bool fAddnode {false};
    bool fClient {false};
    const bool fInbound {false};
    std::atomic_bool fSuccessfullyConnected {false};
    std::atomic_bool fDisconnect {false};
    // We use fRelayTxes for two purposes -
    // a) it allows us to not relay tx invs before receiving the peer's version
    // message.
    // b) the peer may tell us in its version message that we should not relay
    // tx invs unless it loads a bloom filter.

    // protected by cs_filter
    bool fRelayTxes {false};
    bool fSentAddr {false};
    CSemaphoreGrant grantOutbound {};
    CCriticalSection cs_filter {};
    CBloomFilter mFilter;
    const NodeId id {};

    const uint64_t nKeyedNetGroup {0};
    std::atomic_bool fPauseRecv {false};
    std::atomic_bool fPauseSend {false};

protected:
    mapMsgCmdSize mapSendBytesPerMsgCmd {};
    mapMsgCmdSize mapRecvBytesPerMsgCmd {};

public:
    uint256 hashContinue { uint256() };
    std::atomic<int> nStartingHeight {-1};

    // flood relay
    std::vector<CAddress> vAddrToSend {};
    CRollingBloomFilter addrKnown { 5000, 0.001 };
    // Has an ADDR been requested?
    std::atomic_bool fGetAddr {false};
    int64_t nNextAddrSend {0};
    int64_t nNextLocalAddrSend {0};

    // Inventory based relay.
    CRollingBloomFilter filterInventoryKnown { 50000, 0.000001 };
    // Set of transaction ids we still have to announce. They are sorted by the
    // mempool before relay, so the order is not important.
    std::set<uint256> setInventoryTxToSend {};
    // List of block ids we still have announce. There is no final sorting
    // before sending, as they are always sent immediately and in the order
    // requested.
    std::vector<uint256> vInventoryBlockToSend {};
    CCriticalSection cs_inventory {};
    std::set<uint256> setAskFor {};
    std::multimap<int64_t, CInv> mapAskFor {};
    int64_t nNextInvSend {0};
    // Used for headers announcements - unfiltered blocks to relay. Also
    // protected by cs_inventory.
    std::vector<uint256> vBlockHashesToAnnounce {};
    // Used for BIP35 mempool sending, also protected by cs_inventory.
    bool fSendMempool {false};

    // Last time a "MEMPOOL" request was serviced.
    std::atomic<int64_t> timeLastMempoolReq {0};

    // Block and TXN accept times
    std::atomic<int64_t> nLastBlockTime {0};
    std::atomic<int64_t> nLastTXTime {0};

    // Ping time measurement:
    // The pong reply we're expecting, or 0 if no pong expected.
    std::atomic<uint64_t> nPingNonceSent {0};
    // Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
    std::atomic<int64_t> nPingUsecStart {0};
    // Last measured round-trip time.
    std::atomic<int64_t> nPingUsecTime {0};
    // Best measured round-trip time.
    std::atomic<int64_t> nMinPingUsecTime { std::numeric_limits<int64_t>::max() };
    // Whether a ping is requested.
    std::atomic_bool fPingQueued {false};
    // Minimum fee rate with which to filter inv's to this node
    Amount minFeeFilter {0};
    CCriticalSection cs_feeFilter {};
    Amount lastSentFeeFilter {0};
    int64_t nextSendTimeFeeFilter {0};

    /** Maximum number of CInv elements this peers is willing to accept */
    uint32_t maxInvElements {CInv::estimateMaxInvElements(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH)};
    /** protoconfReceived is false by default and set to true when protoconf is received from peer **/
    bool protoconfReceived {false};

    /** Constructor for producing CNode shared pointer instances */
    template<typename ... Args>
    static std::shared_ptr<CNode> Make(Args&& ... args)
    {
        return std::shared_ptr<CNode>(new CNode{std::forward<Args>(args)...});
    }

    ~CNode();

    CNode(CNode&&) = delete;
    CNode& operator=(CNode&&) = delete;
    CNode(const CNode&) = delete;
    CNode& operator=(const CNode&) = delete;

private:
    CNode(
        NodeId id,
        ServiceFlags nLocalServicesIn,
        int nMyStartingHeightIn,
        SOCKET hSocketIn,
        const CAddress& addrIn,
        uint64_t nKeyedNetGroupIn,
        uint64_t nLocalHostNonceIn,
        CConnman::CAsyncTaskPool& asyncTaskPool,
        const std::string &addrNameIn = "",
        bool fInboundIn = false);

    /**
     * Storage for the last chunk being sent to the peer. This variable contains
     * data for the duration of sending the chunk. Once the chunk is sent it is
     * cleared.
     * In case there is an interruption during sending (sent size exceeded or
     * network layer can not process any more data at the moment) this variable
     * remains set and is used to continue streaming on the next try.
     */
    std::optional<CSpan> mSendChunk;
    uint64_t nSendBytes {0};

    const uint64_t nLocalHostNonce {};
    // Services offered to this peer
    const ServiceFlags nLocalServices {};
    const int nMyStartingHeight {};
    int nSendVersion {0};
    // Used only by SocketHandler thread.
    std::list<CNetMessage> vRecvMsg {};

    mutable CCriticalSection cs_addrName {};
    std::string addrName {};

    CService addrLocal {};
    mutable CCriticalSection cs_addrLocal {};

    /** Deque of inventory msgs for transactions to send */
    std::deque<CTxnSendingDetails> mInvList;
    CCriticalSection cs_mInvList {};

    CConnman::CAsyncTaskPool& mAsyncTaskPool;

public:
    enum RECV_STATUS {RECV_OK, RECV_BAD_LENGTH, RECV_FAIL};

    CSendResult SendMessage(
        CForwardAsyncReadonlyStream& data,
        size_t maxChunkSize);

    /** Add some new transactions to our pending inventory list */
    void AddTxnsToInventory(const std::vector<CTxnSendingDetails>& txns);
    /** Remove some transactions from our pending inventroy list */
    void RemoveTxnsFromInventory(const std::vector<CTxnSendingDetails>& txns);
    /** Fetch the next N items from our inventory */
    std::vector<CTxnSendingDetails> FetchNInventory(size_t n);

    NodeId GetId() const { return id; }

    uint64_t GetLocalNonce() const { return nLocalHostNonce; }

    int GetMyStartingHeight() const { return nMyStartingHeight; }

    RECV_STATUS ReceiveMsgBytes(const Config &config, const char *pch, uint32_t nBytes,
                         bool &complete);

    void SetRecvVersion(int nVersionIn) { nRecvVersion = nVersionIn; }
    int GetRecvVersion() { return nRecvVersion; }
    void SetSendVersion(int nVersionIn);
    int GetSendVersion() const;

    CService GetAddrLocal() const;
    //! May not be called more than once
    void SetAddrLocal(const CService &addrLocalIn);

    void AddAddressKnown(const CAddress &_addr) {
        addrKnown.insert(_addr.GetKey());
    }

    void PushAddress(const CAddress &_addr, FastRandomContext &insecure_rand) {
        // Known checking here is only to save space from duplicates.
        // SendMessages will filter it again for knowns that were added
        // after addresses were pushed.
        if (_addr.IsValid() && !addrKnown.contains(_addr.GetKey())) {
            if (vAddrToSend.size() >= MAX_ADDR_TO_SEND) {
                vAddrToSend[insecure_rand.randrange(vAddrToSend.size())] =
                    _addr;
            } else {
                vAddrToSend.push_back(_addr);
            }
        }
    }

    void AddInventoryKnown(const CInv &inv) {
        LOCK(cs_inventory);
        filterInventoryKnown.insert(inv.hash);
    }

    void PushInventory(const CInv &inv) {
        LOCK(cs_inventory);
        if (inv.type == MSG_TX) {
            if (!filterInventoryKnown.contains(inv.hash)) {
                setInventoryTxToSend.insert(inv.hash);
            }
        } else if (inv.type == MSG_BLOCK) {
            vInventoryBlockToSend.push_back(inv.hash);
        }
    }

    void PushBlockHash(const uint256 &hash) {
        LOCK(cs_inventory);
        vBlockHashesToAnnounce.push_back(hash);
    }

    void AskFor(const CInv &inv);

    void CloseSocketDisconnect();

    void copyStats(CNodeStats &stats);

    uint64_t GetAverageBandwidth();

    ServiceFlags GetLocalServices() const { return nLocalServices; }

    std::string GetAddrName() const;
    //! Sets the addrName only if it was not previously set
    void MaybeSetAddrName(const std::string &addrNameIn);

    /**
     * Run node related task asynchronously.
     * Tasks may live longer than CNode instance exists as they are gracefully
     * terminated before completion only when CConnman instance is terminated
     * (CConnman destructor implicitly calls CAsyncTaskPool destructor which
     * terminates the tasks).
     *
     * The reason for task lifetime extension past CNode lifetime is that the
     * network connection can be dropped which destroys CNode instance while
     * on the other hand tasks should not be bound to this external event - for
     * example current tasks call ActivateBestChain() which should finish even
     * if connection is dropped (not related to a specific node).
     *
     * NOTE: Function should never capture bare pointer or shared pointer
     *       reference to this node internally as that can lead to unwanted
     *       lifetime extension (tasks may run longer than node is kept alive).
     *       For this reason the weak pointer that is provided as call parameter
     *       should be used instead.
     *       In current tasks the weak pointer is used only for updating
     *       CNode::nLastBlockTime which is relevant only while node instance
     *       exists and has no side effects when we can't perform it after the
     *       node instance no longer exists.
     */
    void RunAsyncProcessing(
        std::function<void(std::weak_ptr<CNode>)> function,
        std::shared_ptr<task::CCancellationSource> source);
};

/**
 * Return a timestamp in the future (in microseconds) for exponentially
 * distributed events.
 */
int64_t PoissonNextSend(int64_t nNow, int average_interval_seconds);

std::string userAgent();
#endif // BITCOIN_NET_H
