// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "net.h"

#include "addrman.h"
#include "chainparams.h"
#include "clientversion.h"
#include "config.h"
#include "consensus/consensus.h"
#include "crypto/common.h"
#include "crypto/sha256.h"
#include "hash.h"
#include "netbase.h"
#include "primitives/transaction.h"
#include "scheduler.h"
#include "taskcancellation.h"
#include "txn_propagator.h"
#include "txn_validator.h"
#include "ui_interface.h"
#include "utilstrencodings.h"

#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#ifdef USE_UPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/miniwget.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

#include <cmath>
#include <optional>
#include <utility>

// Dump addresses to peers.dat and banlist.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

// We add a random period time (0 to 1 seconds) to feeler connections to prevent
// synchronization.
#define FEELER_SLEEP_WINDOW 1

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW
// version.
#ifdef WIN32
#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 23
#endif
#endif

static const std::string NET_MESSAGE_COMMAND_OTHER = "*other*";

// SHA256("netgroup")[0:8]
static const uint64_t RANDOMIZER_ID_NETGROUP = 0x6c0edd8036ef4036ULL;
// SHA256("localhostnonce")[0:8]
static const uint64_t RANDOMIZER_ID_LOCALHOSTNONCE = 0xd93e69e2bbfa5735ULL;
//
// Global state variables
//
bool fDiscover = true;
bool fListen = true;
bool fRelayTxes = true;
CCriticalSection cs_mapLocalHost;
std::map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfLimited[NET_MAX] = {};
std::atomic_size_t CSendQueueBytes::nTotalSendQueuesBytes = 0;

CCriticalSection cs_invQueries;
limitedmap<uint256, int64_t> mapAlreadyAskedFor(CInv::estimateMaxInvElements(MAX_PROTOCOL_SEND_PAYLOAD_LENGTH));

/** The maximum number of entries in mapAskFor */
static const size_t MAPASKFOR_MAX_SIZE = CInv::estimateMaxInvElements(MAX_PROTOCOL_RECV_PAYLOAD_LENGTH);
/** The maximum number of entries in setAskFor (larger due to getdata latency)*/
static const size_t SETASKFOR_MAX_SIZE = MAPASKFOR_MAX_SIZE * 4;

// Signals for message handling
static CNodeSignals g_signals;
CNodeSignals &GetNodeSignals() {
    return g_signals;
}

void CConnman::AddOneShot(const std::string &strDest) {
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

unsigned short GetListenPort() {
    return (unsigned short)(gArgs.GetArg("-port", Params().GetDefaultPort()));
}

// find 'best' local address for a particular peer
bool GetLocal(CService &addr, const CNetAddr *paddrPeer) {
    if (!fListen) return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(cs_mapLocalHost);
        for (std::map<CNetAddr, LocalServiceInfo>::iterator it =
                 mapLocalHost.begin();
             it != mapLocalHost.end(); it++) {
            int nScore = (*it).second.nScore;
            int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability ||
                (nReachability == nBestReachability && nScore > nBestScore)) {
                addr = CService((*it).first, (*it).second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

//! Convert the pnSeeds6 array into usable address objects.
static std::vector<CAddress>
convertSeed6(const std::vector<SeedSpec6> &vSeedsIn) {
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps. Seed nodes are given
    // a random 'last seen time' of between one and two weeks ago.
    const int64_t nOneWeek = 7 * 24 * 60 * 60;
    std::vector<CAddress> vSeedsOut;
    vSeedsOut.reserve(vSeedsIn.size());
    for (std::vector<SeedSpec6>::const_iterator i(vSeedsIn.begin());
         i != vSeedsIn.end(); ++i) {
        struct in6_addr ip;
        memcpy(&ip, i->addr, sizeof(ip));
        CAddress addr(CService(ip, i->port), NODE_NETWORK);
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
    return vSeedsOut;
}

// Get best local address for a particular peer as a CAddress. Otherwise, return
// the unroutable 0.0.0.0 but filled in with the normal parameters, since the IP
// may be changed to a useful one by discovery.
CAddress GetLocalAddress(const CNetAddr *paddrPeer,
                         ServiceFlags nLocalServices) {
    CAddress ret(CService(CNetAddr(), GetListenPort()), NODE_NONE);
    CService addr;
    if (GetLocal(addr, paddrPeer)) {
        ret = CAddress(addr, nLocalServices);
    }
    ret.nTime = GetAdjustedTime();
    return ret;
}

int GetnScore(const CService &addr) {
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == LOCAL_NONE) {
        return 0;
    }
    return mapLocalHost[addr].nScore;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(const CNodePtr& pnode) {
    CService addrLocal = pnode->GetAddrLocal();
    return fDiscover && pnode->addr.IsRoutable() && addrLocal.IsRoutable() &&
           !IsLimited(addrLocal.GetNetwork());
}

// Pushes our own address to a peer.
void AdvertiseLocal(const CNodePtr& pnode) {
    if (fListen && pnode->fSuccessfullyConnected) {
        CAddress addrLocal =
            GetLocalAddress(&pnode->addr, pnode->GetLocalServices());
        // If discovery is enabled, sometimes give our peer the address it tells
        // us that it sees us as in case it has a better idea of our address
        // than we do.
        if (IsPeerAddrLocalGood(pnode) &&
            (!addrLocal.IsRoutable() ||
             GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8 : 2) == 0)) {
            addrLocal.SetIP(pnode->GetAddrLocal());
        }
        if (addrLocal.IsRoutable()) {
            LogPrint(BCLog::NET, "AdvertiseLocal: advertising address %s\n",
                     addrLocal.ToString());
            FastRandomContext insecure_rand;
            pnode->PushAddress(addrLocal, insecure_rand);
        }
    }
}

// Learn a new local address.
bool AddLocal(const CService &addr, int nScore) {
    if (!addr.IsRoutable()) {
        return false;
    }

    if (!fDiscover && nScore < LOCAL_MANUAL) {
        return false;
    }

    if (IsLimited(addr)) {
        return false;
    }

    LogPrintf("AddLocal(%s,%i)\n", addr.ToString(), nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore) {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
    }

    return true;
}

bool AddLocal(const CNetAddr &addr, int nScore) {
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

bool RemoveLocal(const CService &addr) {
    LOCK(cs_mapLocalHost);
    LogPrintf("RemoveLocal(%s)\n", addr.ToString());
    mapLocalHost.erase(addr);
    return true;
}

/** Make a particular network entirely off-limits (no automatic connects to it)
 */
void SetLimited(enum Network net, bool fLimited) {
    if (net == NET_UNROUTABLE) {
        return;
    }
    LOCK(cs_mapLocalHost);
    vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net) {
    LOCK(cs_mapLocalHost);
    return vfLimited[net];
}

bool IsLimited(const CNetAddr &addr) {
    return IsLimited(addr.GetNetwork());
}

/** vote for a local address */
bool SeenLocal(const CService &addr) {
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == 0) {
        return false;
    }
    mapLocalHost[addr].nScore++;
    return true;
}

/** check whether a given address is potentially local */
bool IsLocal(const CService &addr) {
    LOCK(cs_mapLocalHost);
    return mapLocalHost.count(addr) > 0;
}

/** check whether a given network is one we can probably connect to */
bool IsReachable(enum Network net) {
    LOCK(cs_mapLocalHost);
    return !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr &addr) {
    enum Network net = addr.GetNetwork();
    return IsReachable(net);
}

CNodePtr CConnman::FindNode(const CNetAddr &ip) {
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if ((CNetAddr)pnode->addr == ip) {
            return pnode;
        }
    }
    return nullptr;
}

CNodePtr CConnman::FindNode(const CSubNet &subNet) {
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if (subNet.Match((CNetAddr)pnode->addr)) {
            return pnode;
        }
    }
    return nullptr;
}

CNodePtr CConnman::FindNode(const std::string &addrName) {
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if (pnode->GetAddrName() == addrName) {
            return pnode;
        }
    }
    return nullptr;
}

CNodePtr CConnman::FindNode(const CService &addr) {
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if ((CService)pnode->addr == addr) {
            return pnode;
        }
    }
    return nullptr;
}

bool CConnman::CheckIncomingNonce(uint64_t nonce) {
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if (!pnode->fSuccessfullyConnected && !pnode->fInbound &&
            pnode->GetLocalNonce() == nonce)
            return false;
    }
    return true;
}

CNodePtr CConnman::ConnectNode(CAddress addrConnect, const char *pszDest,
                               bool fCountFailure) {
    if (pszDest == nullptr) {
        if (IsLocal(addrConnect)) {
            return nullptr;
        }

        // Look for an existing connection
        CNodePtr pnode = FindNode((CService)addrConnect);
        if (pnode) {
            LogPrintf("Failed to open new connection, already connected\n");
            return nullptr;
        }
    }

    /// debug print
    LogPrint(BCLog::NET, "trying connection %s lastseen=%.1fhrs\n",
             pszDest ? pszDest : addrConnect.ToString(),
             pszDest
                 ? 0.0
                 : (double)(GetAdjustedTime() - addrConnect.nTime) / 3600.0);

    // Connect
    SOCKET hSocket;
    bool proxyConnectionFailed = false;
    if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest,
                                      config->GetChainParams().GetDefaultPort(),
                                      nConnectTimeout, &proxyConnectionFailed)
                : ConnectSocket(addrConnect, hSocket, nConnectTimeout,
                                &proxyConnectionFailed)) {
        if (!IsSelectableSocket(hSocket)) {
            LogPrintf("Cannot create connection: non-selectable socket created "
                      "(fd >= FD_SETSIZE ?)\n");
            CloseSocket(hSocket);
            return nullptr;
        }

        if (pszDest && addrConnect.IsValid()) {
            // It is possible that we already have a connection to the IP/port
            // pszDest resolved to. In that case, drop the connection that was
            // just created, and return the existing CNode instead. Also store
            // the name we used to connect in that CNode, so that future
            // FindNode() calls to that name catch this early.
            LOCK(cs_vNodes);
            CNodePtr pnode = FindNode((CService)addrConnect);
            if (pnode) {
                pnode->MaybeSetAddrName(std::string(pszDest));
                CloseSocket(hSocket);
                LogPrintf("Failed to open new connection, already connected\n");
                return nullptr;
            }
        }

        addrman.Attempt(addrConnect, fCountFailure);

        // Add node
        NodeId id = GetNewNodeId();
        uint64_t nonce =
            GetDeterministicRandomizer(RANDOMIZER_ID_LOCALHOSTNONCE)
                .Write(id)
                .Finalize();
        CNodePtr pnode =
            CNode::Make(
                id,
                nLocalServices,
                GetBestHeight(),
                hSocket,
                addrConnect,
                CalculateKeyedNetGroup(addrConnect),
                nonce,
                mAsyncTaskPool,
                pszDest ? pszDest : "",
                false);
        pnode->nServicesExpected = ServiceFlags(addrConnect.nServices & nRelevantServices);

        return pnode;
    } else if (!proxyConnectionFailed) {
        // If connecting to the node failed, and failure is not caused by a
        // problem connecting to the proxy, mark this as an attempt.
        addrman.Attempt(addrConnect, fCountFailure);
    }

    return nullptr;
}

void CConnman::DumpBanlist() {
    // Clean unused entries (if bantime has expired)
    SweepBanned();

    if (!BannedSetIsDirty()) {
        return;
    }

    int64_t nStart = GetTimeMillis();

    CBanDB bandb(config->GetChainParams());
    banmap_t banmap;
    GetBanned(banmap);
    if (bandb.Write(banmap)) {
        SetBannedSetDirty(false);
    }

    LogPrint(BCLog::NET,
             "Flushed %d banned node ips/subnets to banlist.dat  %dms\n",
             banmap.size(), GetTimeMillis() - nStart);
}

void CNode::CloseSocketDisconnect() {
    fDisconnect = true;
    LOCK(cs_hSocket);
    if (hSocket != INVALID_SOCKET) {
        LogPrint(BCLog::NET, "disconnecting peer=%d\n", id);
        CloseSocket(hSocket);
    }
}

void CConnman::ClearBanned() {
    {
        LOCK(cs_setBanned);
        setBanned.clear();
        setBannedIsDirty = true;
    }

    // Store banlist to disk.
    DumpBanlist();
    if (clientInterface) {
        clientInterface->BannedListChanged();
    }
}

bool CConnman::IsBanned(CNetAddr ip) {
    LOCK(cs_setBanned);

    bool fResult = false;
    for (banmap_t::iterator it = setBanned.begin(); it != setBanned.end();
         it++) {
        CSubNet subNet = (*it).first;
        CBanEntry banEntry = (*it).second;

        if (subNet.Match(ip) && GetTime() < banEntry.nBanUntil) {
            fResult = true;
        }
    }

    return fResult;
}

bool CConnman::IsBanned(CSubNet subnet) {
    LOCK(cs_setBanned);

    bool fResult = false;
    banmap_t::iterator i = setBanned.find(subnet);
    if (i != setBanned.end()) {
        CBanEntry banEntry = (*i).second;
        if (GetTime() < banEntry.nBanUntil) {
            fResult = true;
        }
    }

    return fResult;
}

void CConnman::Ban(const CNetAddr &addr, const BanReason &banReason,
                   int64_t bantimeoffset, bool sinceUnixEpoch) {
    CSubNet subNet(addr);
    Ban(subNet, banReason, bantimeoffset, sinceUnixEpoch);
}

void CConnman::Ban(const CSubNet &subNet, const BanReason &banReason,
                   int64_t bantimeoffset, bool sinceUnixEpoch) {
    CBanEntry banEntry(GetTime());
    banEntry.banReason = banReason;
    if (bantimeoffset <= 0) {
        bantimeoffset = gArgs.GetArg("-bantime", DEFAULT_MISBEHAVING_BANTIME);
        sinceUnixEpoch = false;
    }
    banEntry.nBanUntil = (sinceUnixEpoch ? 0 : GetTime()) + bantimeoffset;

    {
        LOCK(cs_setBanned);
        if (setBanned[subNet].nBanUntil < banEntry.nBanUntil) {
            setBanned[subNet] = banEntry;
            setBannedIsDirty = true;
        } else {
            return;
        }
    }

    if (clientInterface) {
        clientInterface->BannedListChanged();
    }

    {
        LOCK(cs_vNodes);
        for (const CNodePtr& pnode : vNodes) {
            if (subNet.Match((CNetAddr)pnode->addr)) {
                pnode->fDisconnect = true;
            }
        }
    }

    if (banReason == BanReasonManuallyAdded) {
        // Store banlist to disk immediately if user requested ban.
        DumpBanlist();
    }
}

bool CConnman::Unban(const CNetAddr &addr) {
    CSubNet subNet(addr);
    return Unban(subNet);
}

bool CConnman::Unban(const CSubNet &subNet) {
    {
        LOCK(cs_setBanned);
        if (!setBanned.erase(subNet)) {
            return false;
        }
        setBannedIsDirty = true;
    }

    if (clientInterface) {
        clientInterface->BannedListChanged();
    }

    // Store banlist to disk immediately.
    DumpBanlist();
    return true;
}

void CConnman::GetBanned(banmap_t &banMap) {
    LOCK(cs_setBanned);
    // Sweep the banlist so expired bans are not returned
    SweepBanned();
    // Create a thread safe copy.
    banMap = setBanned;
}

void CConnman::SetBanned(const banmap_t &banMap) {
    LOCK(cs_setBanned);
    setBanned = banMap;
    setBannedIsDirty = true;
}

void CConnman::SweepBanned() {
    int64_t now = GetTime();

    LOCK(cs_setBanned);
    banmap_t::iterator it = setBanned.begin();
    while (it != setBanned.end()) {
        CSubNet subNet = (*it).first;
        CBanEntry banEntry = (*it).second;
        if (now > banEntry.nBanUntil) {
            setBanned.erase(it++);
            setBannedIsDirty = true;
            LogPrint(BCLog::NET,
                     "%s: Removed banned node ip/subnet from banlist.dat: %s\n",
                     __func__, subNet.ToString());
        } else {
            ++it;
        }
    }
}

bool CConnman::BannedSetIsDirty() {
    LOCK(cs_setBanned);
    return setBannedIsDirty;
}

void CConnman::SetBannedSetDirty(bool dirty) {
    // Reuse setBanned lock for the isDirty flag.
    LOCK(cs_setBanned);
    setBannedIsDirty = dirty;
}

bool CConnman::IsWhitelistedRange(const CNetAddr &addr) {
    LOCK(cs_vWhitelistedRange);
    for (const CSubNet &subnet : vWhitelistedRange) {
        if (subnet.Match(addr)) {
            return true;
        }
    }
    return false;
}

void CConnman::AddWhitelistedRange(const CSubNet &subnet) {
    LOCK(cs_vWhitelistedRange);
    vWhitelistedRange.push_back(subnet);
}

CConnman::CAsyncTaskPool::CAsyncTaskPool(const Config& config)
    : mPool{
        "CAsyncTaskPool",
        // +1 so that we have more async threads than there are block checker
        // queues so that a better block can terminate one of the existing
        // blocked block check queues on exhaustion
        static_cast<size_t>(config.GetMaxParallelBlocks()) + 1}
    , mPerInstanceSoftAsyncTaskLimit{config.GetMaxConcurrentAsyncTasksPerNode()}
{/**/}

CConnman::CAsyncTaskPool::~CAsyncTaskPool()
{
    for(auto& task : mRunningTasks)
    {
        task.mCancellationSource->Cancel();
    }
    for(auto& task : mRunningTasks)
    {
        task.mFuture.wait();
    }
}

void CConnman::CAsyncTaskPool::AddToPool(
    const std::shared_ptr<CNode>& node,
    std::function<void(std::weak_ptr<CNode>)> function,
    std::shared_ptr<task::CCancellationSource> source)
{
    mRunningTasks.emplace_back(
        node->GetId(),
        make_task(
            mPool,
            function,
            std::weak_ptr<CNode>{node}),
        std::move(source));
}

void CConnman::CAsyncTaskPool::HandleCompletedAsyncProcessing()
{
    using namespace std::literals::chrono_literals;

    for(size_t i=0; i<mRunningTasks.size();)
    {
        if(mRunningTasks[i].mFuture.wait_for(1ms) == std::future_status::ready)
        {
            try
            {
                mRunningTasks[i].mFuture.get();
            }
            catch(const std::exception& e)
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
            catch(...)
            {
                PrintExceptionContinue(nullptr, "ProcessMessages()");
            }

            mRunningTasks.erase(std::next(mRunningTasks.begin(), i));
        }
        else
        {
            ++i;
        }
    }
}

std::string CNode::GetAddrName() const {
    LOCK(cs_addrName);
    return addrName;
}

void CNode::MaybeSetAddrName(const std::string &addrNameIn) {
    LOCK(cs_addrName);
    if (addrName.empty()) {
        addrName = addrNameIn;
    }
}

void CNode::RunAsyncProcessing(
    std::function<void(std::weak_ptr<CNode>)> function,
    std::shared_ptr<task::CCancellationSource> source)
{
    mAsyncTaskPool.AddToPool(
        shared_from_this(),
        function,
        source);
}

CService CNode::GetAddrLocal() const {
    LOCK(cs_addrLocal);
    return addrLocal;
}

void CNode::SetAddrLocal(const CService &addrLocalIn) {
    LOCK(cs_addrLocal);
    if (addrLocal.IsValid()) {
        error("Addr local already set for node: %i. Refusing to change from %s "
              "to %s",
              id, addrLocal.ToString(), addrLocalIn.ToString());
    } else {
        addrLocal = addrLocalIn;
    }
}

// If we have sufficinet samples then get average bandwidth from node,
// otherwise we must be in early startup measuring the bandwidth so just
// report it as 0.
uint64_t CNode::GetAverageBandwidth() {
    LOCK(cs_vRecv);

    if(!vAvgBandwidth.empty())
    {
        // If we don't yet have a full minutes worth of measurements then just
        // average with what we have
        return static_cast<uint64_t>(Average(vAvgBandwidth.begin(), vAvgBandwidth.end()));
    }

    return 0;
}

void CNode::copyStats(CNodeStats &stats) {
    stats.nodeid = this->GetId();
    stats.nServices = nServices;
    stats.addr = addr;
    {
        LOCK(cs_filter);
        stats.fRelayTxes = fRelayTxes;
    }
    stats.nLastSend = nLastSend;
    stats.nLastRecv = nLastRecv;
    stats.fPauseRecv = fPauseRecv;
    stats.fPauseSend = fPauseSend;
    stats.nTimeConnected = nTimeConnected;
    stats.nTimeOffset = nTimeOffset;
    stats.addrName = GetAddrName();
    stats.nVersion = nVersion;
    {
        LOCK(cs_SubVer);
        stats.cleanSubVer = cleanSubVer;
    }
    stats.fInbound = fInbound;
    stats.fAddnode = fAddnode;
    stats.nStartingHeight = nStartingHeight;
    {
        LOCK(cs_vSend);
        stats.mapSendBytesPerMsgCmd = mapSendBytesPerMsgCmd;
        stats.nSendBytes = nSendBytes;
        stats.nSendSize = nSendSize.getSendQueueBytes();
    }
    {
        LOCK(cs_vRecv);
        stats.mapRecvBytesPerMsgCmd = mapRecvBytesPerMsgCmd;
        stats.nRecvBytes = nRecvBytes;

        // Avg bandwidth measurements
        if(!vAvgBandwidth.empty())
        {
            stats.nMinuteBytesPerSec = GetAverageBandwidth();
            stats.nSpotBytesPerSec = static_cast<uint64_t>(vAvgBandwidth.back());
        }
        else
        {
            stats.nMinuteBytesPerSec = 0;
            stats.nSpotBytesPerSec = 0;
        }
    }
    stats.fWhitelisted = fWhitelisted;

    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer. Merely reporting
    // pingtime might fool the caller into thinking the node was still
    // responsive, since pingtime does not update until the ping is complete,
    // which might take a while. So, if a ping is taking an unusually long time
    // in flight, the caller can immediately detect that this is happening.
    int64_t nPingUsecWait = 0;
    if ((0 != nPingNonceSent) && (0 != nPingUsecStart)) {
        nPingUsecWait = GetTimeMicros() - nPingUsecStart;
    }

    // Raw ping time is in microseconds, but show it to user as whole seconds
    // (Bitcoin users should be well used to small numbers with many decimal
    // places by now :)
    stats.dPingTime = ((double(nPingUsecTime)) / 1e6);
    stats.dMinPing = ((double(nMinPingUsecTime)) / 1e6);
    stats.dPingWait = ((double(nPingUsecWait)) / 1e6);

    // Leave string empty if addrLocal invalid (not filled in yet)
    CService addrLocalUnlocked = GetAddrLocal();
    stats.addrLocal =
        addrLocalUnlocked.IsValid() ? addrLocalUnlocked.ToString() : "";

    {
        // Fetch size of inventory queue
        LOCK(cs_mInvList);
        stats.nInvQueueSize = mInvList.size();
    }
}

static bool IsOversizedMessage(const Config &config, const CNetMessage &msg) {
    if (!msg.in_data) {
        // Header only, cannot be oversized.
        return false;
    }

    return msg.hdr.IsOversized(config);
}

/**
* Add some new transactions to our pending inventory list.
* Assumes the caller has taken care of locking access to the mempool,
* and so can be called in parallel.
*/
void CNode::AddTxnsToInventory(const std::vector<CTxnSendingDetails>& txns)
{
    // Get our minimum fee
    Amount filterrate {0};
    {   
        LOCK(cs_feeFilter);
        filterrate = minFeeFilter;
    }

    // reason for larger cs_inventory lock scope than needed is that if we need
    // to lock both cs_inventory and cs_filter we need to consistently lock
    // inventory before cs_filter to prevent deadlocks
    LOCK(cs_inventory);
    LOCK(cs_filter);
    LOCK(cs_mInvList);

    if(!fRelayTxes)
    {
        // Clear any txns we have queued for this peer
        mInvList.clear();
    }
    else
    {
        for(const CTxnSendingDetails& txn : txns)
        {
            // Don't bother if below peer's fee rate
            if(filterrate != Amount{0} && txn.getInfo().feeRate.GetFeePerK() < filterrate)
                continue;

            // Check and update bloom filters
            if(filterInventoryKnown.contains(txn.getInv().hash))
                continue;
            if(!mFilter.IsRelevantAndUpdate(*(txn.getTxnRef())))
                continue;

            mInvList.emplace_back(txn);
            filterInventoryKnown.insert(txn.getInv().hash);
        }
    }
}

/**
* Remove some transactions from our pending inventory list.
* Assumes the caller has taken care of locking access to the mempool,
* and so can be called in parallel.
*/
void CNode::RemoveTxnsFromInventory(const std::vector<CTxnSendingDetails>& txns)
{
    // Remove them
    LOCK(cs_mInvList);
    for (const auto& el : txns)
    {
         mInvList.erase(std::remove_if(mInvList.begin(), mInvList.end(), [&el](const CTxnSendingDetails& i) {
             return i.getInv() == el.getInv(); }),
                mInvList.end());
      }
}

/** Fetch the next N items from our inventory */
std::vector<CTxnSendingDetails> CNode::FetchNInventory(size_t n)
{
    std::vector<CTxnSendingDetails> results {};

    TRY_LOCK(cs_mInvList, invLocked);
    if(!invLocked)
    {
        return results;
    }
 
    if (n > mInvList.size())
    {
        n = mInvList.size();
    }

    results.reserve(n);
    auto endIt = std::next(std::begin(mInvList), n);
    std::move(std::begin(mInvList), endIt, std::back_inserter(results));
    mInvList.erase(std::begin(mInvList), endIt);

    return results;
}

CNode::RECV_STATUS CNode::ReceiveMsgBytes(const Config &config, const char *pch,
                            uint32_t nBytes, bool &complete) {
    complete = false;
    int64_t nTimeMicros = GetTimeMicros();
    LOCK(cs_vRecv);
    nLastRecv = nTimeMicros / MICROS_PER_SECOND;
    nRecvBytes += nBytes;
    nBytesRecvThisSpot += nBytes;

    while (nBytes > 0) {
        // Get current incomplete message, or create a new one.
        if (vRecvMsg.empty() || vRecvMsg.back().complete()) {
            vRecvMsg.push_back(CNetMessage(Params().NetMagic(), SER_NETWORK,
                                           INIT_PROTO_VERSION));
        }

        CNetMessage &msg = vRecvMsg.back();

        // Absorb network data.
        int handled;
        if (!msg.in_data) {
            handled = msg.readHeader(config, pch, nBytes);
            if (handled < 0) {
                return RECV_BAD_LENGTH;//Notify bad message as soon as seen in the header
            }
        } else {
            handled = msg.readData(pch, nBytes);
        }

        if (handled < 0) {
            return RECV_FAIL;
        }

        if (IsOversizedMessage(config, msg)) {
            LogPrint(BCLog::NET,
                     "Oversized message from peer=%i, disconnecting\n",
                     GetId());
            return RECV_BAD_LENGTH;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete()) {
            // Store received bytes per message command to prevent a memory DOS,
            // only allow valid commands.
            mapMsgCmdSize::iterator i =
                mapRecvBytesPerMsgCmd.find(msg.hdr.pchCommand);
            if (i == mapRecvBytesPerMsgCmd.end()) {
                i = mapRecvBytesPerMsgCmd.find(NET_MESSAGE_COMMAND_OTHER);
            }

            assert(i != mapRecvBytesPerMsgCmd.end());
            i->second += msg.hdr.nPayloadLength + CMessageHeader::HEADER_SIZE;

            msg.nTime = nTimeMicros;
            complete = true;
        }
    }

    return RECV_OK;
}

void CNode::SetSendVersion(int nVersionIn) {
    // Send version may only be changed in the version message, and only one
    // version message is allowed per session. We can therefore treat this value
    // as const and even atomic as long as it's only used once a version message
    // has been successfully processed. Any attempt to set this twice is an
    // error.
    if (nSendVersion != 0) {
        error("Send version already set for node: %i. Refusing to change from "
              "%i to %i",
              id, nSendVersion, nVersionIn);
    } else {
        nSendVersion = nVersionIn;
    }
}

int CNode::GetSendVersion() const {
    // The send version should always be explicitly set to INIT_PROTO_VERSION
    // rather than using this value until SetSendVersion has been called.
    if (nSendVersion == 0) {
        error("Requesting unset send version for node: %i. Using %i", id,
              INIT_PROTO_VERSION);
        return INIT_PROTO_VERSION;
    }
    return nSendVersion;
}

int CNetMessage::readHeader(const Config &config, const char *pch,
                            uint32_t nBytes) {
    // copy data to temporary parsing buffer
    uint32_t nRemaining = 24 - nHdrPos;
    uint32_t nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24) {
        return nCopy;
    }

    // deserialize to CMessageHeader
    try {
        hdrbuf >> hdr;
    } catch (const std::exception &) {
        LogPrint(BCLog::NET, "Bad header format\n");
        return -1;
    }

    // Reject oversized messages
    if (hdr.IsOversized(config)) {
        LogPrint(BCLog::NET, "Oversized header detected\n");
        return -1;
    }

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char *pch, uint32_t nBytes) {
    unsigned int nRemaining = hdr.nPayloadLength - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy) {
        // Allocate up to 256 KiB ahead, but never more than the total message
        // size.
        vRecv.resize(std::min(hdr.nPayloadLength, nDataPos + nCopy + 256 * 1024));
    }

    hasher.Write((const uint8_t *)pch, nCopy);
    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}

const uint256 &CNetMessage::GetMessageHash() const {
    assert(complete());
    if (data_hash.IsNull()) {
        hasher.Finalize(data_hash.begin());
    }
    return data_hash;
}

// requires LOCK(cs_vSend)
size_t CConnman::SocketSendData(const CNodePtr& pnode) const {
    AssertLockHeld(pnode->cs_vSend);
    size_t nSentSize = 0;
    size_t nMsgCount = 0;

    for (const auto &data : pnode->vSendMsg) {
        auto sent = pnode->SendMessage(*data, nSendBufferMaxSize);
        nSentSize += sent.sentSize;
        pnode->nSendSize -= sent.sentSize;

        if(sent.sendComplete == false)
        {
            break;
        }

        pnode->fPauseSend =
            pnode->nSendSize.getSendQueueBytes() > nSendBufferMaxSize;
        ++nMsgCount;
    }

    pnode->vSendMsg.erase(pnode->vSendMsg.begin(),
                          pnode->vSendMsg.begin() + nMsgCount);

    if (pnode->vSendMsg.empty()) {
        assert(!pnode->mSendChunk);
        assert(pnode->nSendSize.getSendQueueBytes() == 0);
    }

    return nSentSize;
}

struct NodeEvictionCandidate {
    NodeId id;
    int64_t nTimeConnected;
    int64_t nMinPingUsecTime;
    int64_t nLastBlockTime;
    int64_t nLastTXTime;
    bool fRelevantServices;
    bool fRelayTxes;
    CAddress addr;
    uint64_t nKeyedNetGroup;
};

static bool ReverseCompareNodeMinPingTime(const NodeEvictionCandidate &a,
                                          const NodeEvictionCandidate &b) {
    return a.nMinPingUsecTime > b.nMinPingUsecTime;
}

static bool ReverseCompareNodeTimeConnected(const NodeEvictionCandidate &a,
                                            const NodeEvictionCandidate &b) {
    return a.nTimeConnected > b.nTimeConnected;
}

static bool CompareNetGroupKeyed(const NodeEvictionCandidate &a,
                                 const NodeEvictionCandidate &b) {
    return a.nKeyedNetGroup < b.nKeyedNetGroup;
}

static bool CompareNodeBlockTime(const NodeEvictionCandidate &a,
                                 const NodeEvictionCandidate &b) {
    // There is a fall-through here because it is common for a node to have many
    // peers which have not yet relayed a block.
    if (a.nLastBlockTime != b.nLastBlockTime) {
        return a.nLastBlockTime < b.nLastBlockTime;
    }

    if (a.fRelevantServices != b.fRelevantServices) {
        return b.fRelevantServices;
    }

    return a.nTimeConnected > b.nTimeConnected;
}

static bool CompareNodeTXTime(const NodeEvictionCandidate &a,
                              const NodeEvictionCandidate &b) {
    // There is a fall-through here because it is common for a node to have more
    // than a few peers that have not yet relayed txn.
    if (a.nLastTXTime != b.nLastTXTime) {
        return a.nLastTXTime < b.nLastTXTime;
    }

    if (a.fRelayTxes != b.fRelayTxes) {
        return b.fRelayTxes;
    }

    return a.nTimeConnected > b.nTimeConnected;
}

/**
 * Try to find a connection to evict when the node is full. Extreme care must be
 * taken to avoid opening the node to attacker triggered network partitioning.
 * The strategy used here is to protect a small number of peers for each of
 * several distinct characteristics which are difficult to forge. In order to
 * partition a node the attacker must be simultaneously better at all of them
 * than honest peers.
 */
bool CConnman::AttemptToEvictConnection() {
    std::vector<NodeEvictionCandidate> vEvictionCandidates;
    {
        LOCK(cs_vNodes);

        for (const CNodePtr& node : vNodes) {
            if (node->fWhitelisted || !node->fInbound || node->fDisconnect) {
                continue;
            }
            NodeEvictionCandidate candidate = {
                node->id,
                node->nTimeConnected,
                node->nMinPingUsecTime,
                node->nLastBlockTime,
                node->nLastTXTime,
                (node->nServices & nRelevantServices) == nRelevantServices,
                node->fRelayTxes,
                node->addr,
                node->nKeyedNetGroup};
            vEvictionCandidates.push_back(candidate);
        }
    }

    if (vEvictionCandidates.empty()) {
        return false;
    }

    // Protect connections with certain characteristics

    // Deterministically select 4 peers to protect by netgroup. An attacker
    // cannot predict which netgroups will be protected.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(),
              CompareNetGroupKeyed);
    vEvictionCandidates.erase(
        vEvictionCandidates.end() -
            std::min(4, static_cast<int>(vEvictionCandidates.size())),
        vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) {
        return false;
    }

    // Protect the 8 nodes with the lowest minimum ping time. An attacker cannot
    // manipulate this metric without physically moving nodes closer to the
    // target.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(),
              ReverseCompareNodeMinPingTime);
    vEvictionCandidates.erase(
        vEvictionCandidates.end() -
            std::min(8, static_cast<int>(vEvictionCandidates.size())),
        vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) {
        return false;
    }

    // Protect 4 nodes that most recently sent us transactions. An attacker
    // cannot manipulate this metric without performing useful work.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(),
              CompareNodeTXTime);
    vEvictionCandidates.erase(
        vEvictionCandidates.end() -
            std::min(4, static_cast<int>(vEvictionCandidates.size())),
        vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) {
        return false;
    }

    // Protect 4 nodes that most recently sent us blocks. An attacker cannot
    // manipulate this metric without performing useful work.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(),
              CompareNodeBlockTime);
    vEvictionCandidates.erase(
        vEvictionCandidates.end() -
            std::min(4, static_cast<int>(vEvictionCandidates.size())),
        vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) {
        return false;
    }

    // Protect the half of the remaining nodes which have been connected the
    // longest. This replicates the non-eviction implicit behavior, and
    // precludes attacks that start later.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(),
              ReverseCompareNodeTimeConnected);
    vEvictionCandidates.erase(
        vEvictionCandidates.end() -
            static_cast<int>(vEvictionCandidates.size() / 2),
        vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) {
        return false;
    }

    // Identify the network group with the most connections and youngest member.
    // (vEvictionCandidates is already sorted by reverse connect time)
    uint64_t naMostConnections;
    unsigned int nMostConnections = 0;
    int64_t nMostConnectionsTime = 0;
    std::map<uint64_t, std::vector<NodeEvictionCandidate>> mapNetGroupNodes;
    for (const NodeEvictionCandidate &node : vEvictionCandidates) {
        mapNetGroupNodes[node.nKeyedNetGroup].push_back(node);
        int64_t grouptime =
            mapNetGroupNodes[node.nKeyedNetGroup][0].nTimeConnected;
        size_t groupsize = mapNetGroupNodes[node.nKeyedNetGroup].size();

        if (groupsize > nMostConnections ||
            (groupsize == nMostConnections &&
             grouptime > nMostConnectionsTime)) {
            nMostConnections = groupsize;
            nMostConnectionsTime = grouptime;
            naMostConnections = node.nKeyedNetGroup;
        }
    }

    // Reduce to the network group with the most connections
    vEvictionCandidates = std::move(mapNetGroupNodes[naMostConnections]);

    // Disconnect from the network group with the most connections
    NodeId evicted = vEvictionCandidates.front().id;
    LOCK(cs_vNodes);
    for(const CNodePtr& node : vNodes) {
        if (node->GetId() == evicted) {
            node->fDisconnect = true;
            return true;
        }
    }
    return false;
}

void CConnman::AcceptConnection(const ListenSocket &hListenSocket) {
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    SOCKET hSocket =
        accept(hListenSocket.socket, (struct sockaddr *)&sockaddr, &len);
    CAddress addr;
    int nInbound = 0;
    int nMaxInbound = nMaxConnections - (nMaxOutbound + nMaxFeeler);

    if (hSocket != INVALID_SOCKET) {
        if (!addr.SetSockAddr((const struct sockaddr *)&sockaddr)) {
            LogPrintf("Warning: Unknown socket family\n");
        }
    }

    bool whitelisted = hListenSocket.whitelisted || IsWhitelistedRange(addr);
    {
        LOCK(cs_vNodes);
        for (const CNodePtr& pnode : vNodes) {
            if (pnode->fInbound) {
                nInbound++;
            }
        }
    }

    if (hSocket == INVALID_SOCKET) {
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK) {
            LogPrintf("socket error accept failed: %s\n",
                      NetworkErrorString(nErr));
        }
        return;
    }

    if (!fNetworkActive) {
        LogPrintf("connection from %s dropped: not accepting new connections\n",
                  addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (!IsSelectableSocket(hSocket)) {
        LogPrintf("connection from %s dropped: non-selectable socket\n",
                  addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    // According to the internet TCP_NODELAY is not carried into accepted
    // sockets on all platforms.  Set it again here just to be sure.
    int set = 1;
#ifdef WIN32
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&set,
               sizeof(int));
#else
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void *)&set, sizeof(int));
#endif

    if (IsBanned(addr) && !whitelisted) {
        LogPrint(BCLog::NET, "connection from %s dropped (banned)\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (nInbound >= nMaxInbound) {
        if (!AttemptToEvictConnection()) {
            // No connection to evict, disconnect the new connection
            LogPrint(BCLog::NET, "failed to find an eviction candidate - "
                                 "connection dropped (full)\n");
            CloseSocket(hSocket);
            return;
        }
    }

    NodeId id = GetNewNodeId();
    uint64_t nonce = GetDeterministicRandomizer(RANDOMIZER_ID_LOCALHOSTNONCE)
                         .Write(id)
                         .Finalize();

    CNodePtr pnode =
        CNode::Make(
            id,
            nLocalServices,
            GetBestHeight(),
            hSocket,
            addr,
            CalculateKeyedNetGroup(addr),
            nonce,
            mAsyncTaskPool,
            "",
            true);
    pnode->fWhitelisted = whitelisted;

    GetNodeSignals().InitializeNode(pnode, *this);

    LogPrint(BCLog::NET, "connection from %s accepted\n", addr.ToString());

    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }
}

void CConnman::ThreadSocketHandler() {
    unsigned int nPrevNodeCount = 0;
    while (!interruptNet) {
        //
        // Disconnect nodes
        //
        {
            LOCK(cs_vNodes);
            // Disconnect unused nodes
            auto pred = [](const CNodePtr& node) { return node->fDisconnect.load(); };
            std::vector<CNodePtr> toBeRemoved {};
            for(const CNodePtr& node : vNodes)
            {
                if(pred(node))
                    toBeRemoved.emplace_back(node);
            }
            // Remove from vNodes
            vNodes.erase(std::remove_if(vNodes.begin(), vNodes.end(), pred), vNodes.end());

            for(const CNodePtr& node : toBeRemoved)
            {
                // Release outbound grant (if any)
                node->grantOutbound.Release();

                // Close socket and cleanup
                node->CloseSocketDisconnect();

                // Hold in disconnected pool until all refs are released
                vNodesDisconnected.push_back(node);
            }
        }
        {
            // Delete disconnected nodes
            auto nodeIt { vNodesDisconnected.begin() };
            while(nodeIt != vNodesDisconnected.end())
            {
                // Wait until threads are done using it
                const CNodePtr& node { *nodeIt };
                bool fDelete {false};
                if(node.use_count() <= 1)
                {
                    {
                        TRY_LOCK(node->cs_inventory, lockInv);
                        if (lockInv) {
                            TRY_LOCK(node->cs_vSend, lockSend);
                            if (lockSend) {
                                fDelete = true;
                            }
                        }
                    }
                }
                if (fDelete) {
                    DeleteNode(node);
                    nodeIt = vNodesDisconnected.erase(nodeIt);
                }
                else {
                    ++nodeIt;
                }
            }
        }
        size_t vNodesSize;
        {
            LOCK(cs_vNodes);
            vNodesSize = vNodes.size();
        }
        if (vNodesSize != nPrevNodeCount) {
            nPrevNodeCount = vNodesSize;
            if (clientInterface) {
                clientInterface->NotifyNumConnectionsChanged(nPrevNodeCount);
            }
        }

        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec = 0;
        // Frequency to poll pnode->vSend
        timeout.tv_usec = 50000;

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

        for (const ListenSocket &hListenSocket : vhListenSocket) {
            FD_SET(hListenSocket.socket, &fdsetRecv);
            hSocketMax = std::max(hSocketMax, hListenSocket.socket);
            have_fds = true;
        }

        {
            LOCK(cs_vNodes);
            for (const CNodePtr& pnode : vNodes) {
                // Implement the following logic:
                // * If there is data to send, select() for sending data. As
                // this only happens when optimistic write failed, we choose to
                // first drain the write buffer in this case before receiving
                // more. This avoids needlessly queueing received data, if the
                // remote peer is not themselves receiving data. This means
                // properly utilizing TCP flow control signalling.
                // * Otherwise, if there is space left in the receive buffer,
                // select() for receiving data.
                // * Hand off all complete messages to the processor, to be
                // handled without blocking here.

                bool select_recv = !pnode->fPauseRecv;
                bool select_send;
                {
                    LOCK(pnode->cs_vSend);
                    select_send = !pnode->vSendMsg.empty();
                }

                LOCK(pnode->cs_hSocket);
                if (pnode->hSocket == INVALID_SOCKET) {
                    continue;
                }

                FD_SET(pnode->hSocket, &fdsetError);
                hSocketMax = std::max(hSocketMax, pnode->hSocket);
                have_fds = true;

                if (select_send) {
                    FD_SET(pnode->hSocket, &fdsetSend);
                    continue;
                }
                if (select_recv) {
                    FD_SET(pnode->hSocket, &fdsetRecv);
                }
            }
        }

        int nSelect = select(have_fds ? hSocketMax + 1 : 0, &fdsetRecv,
                             &fdsetSend, &fdsetError, &timeout);
        if (interruptNet) {
            return;
        }

        if (nSelect == SOCKET_ERROR) {
            if (have_fds) {
                int nErr = WSAGetLastError();
                LogPrintf("socket select error %s\n", NetworkErrorString(nErr));
                for (unsigned int i = 0; i <= hSocketMax; i++) {
                    FD_SET(i, &fdsetRecv);
                }
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            if (!interruptNet.sleep_for(
                    std::chrono::milliseconds(timeout.tv_usec / 1000))) {
                return;
            }
        }

        //
        // Accept new connections
        //
        for (const ListenSocket &hListenSocket : vhListenSocket) {
            if (hListenSocket.socket != INVALID_SOCKET &&
                FD_ISSET(hListenSocket.socket, &fdsetRecv)) {
                AcceptConnection(hListenSocket);
            }
        }

        //
        // Service each socket
        //
        std::vector<CNodePtr> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
        }
        for (const CNodePtr& pnode : vNodesCopy) {
            if (interruptNet) {
                return;
            }

            //
            // Receive
            //
            bool recvSet = false;
            bool sendSet = false;
            bool errorSet = false;
            {
                LOCK(pnode->cs_hSocket);
                if (pnode->hSocket == INVALID_SOCKET) {
                    continue;
                }
                recvSet = FD_ISSET(pnode->hSocket, &fdsetRecv);
                sendSet = FD_ISSET(pnode->hSocket, &fdsetSend);
                errorSet = FD_ISSET(pnode->hSocket, &fdsetError);
            }
            if (recvSet || errorSet) {
                // typical socket buffer is 8K-64K
                char pchBuf[0x10000];
                int32_t nBytes = 0;
                {
                    LOCK(pnode->cs_hSocket);
                    if (pnode->hSocket == INVALID_SOCKET) {
                        continue;
                    }
                    nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf),
                                  MSG_DONTWAIT);
                }
                if (nBytes > 0) {
                    bool notify = false;
                    const CNode::RECV_STATUS status = pnode->ReceiveMsgBytes(*config, pchBuf, nBytes, notify); 
                    if (status != CNode::RECV_OK) {
                        pnode->CloseSocketDisconnect();
                        if (status == CNode::RECV_BAD_LENGTH) {
                            Ban(pnode->addr, BanReasonNodeMisbehaving); // Ban the peer if try to send messages with bad length
                        }   
                    }
                    RecordBytesRecv(nBytes);
                    if (notify) {
                        size_t nSizeAdded = 0;
                        auto it(pnode->vRecvMsg.begin());
                        for (; it != pnode->vRecvMsg.end(); ++it) {
                            if (!it->complete()) {
                                break;
                            }
                            nSizeAdded +=
                                it->vRecv.size() + CMessageHeader::HEADER_SIZE;
                        }
                        {
                            LOCK(pnode->cs_vProcessMsg);
                            pnode->vProcessMsg.splice(
                                pnode->vProcessMsg.end(), pnode->vRecvMsg,
                                pnode->vRecvMsg.begin(), it);
                            pnode->nProcessQueueSize += nSizeAdded;
                            pnode->fPauseRecv =
                                pnode->nProcessQueueSize > nReceiveFloodSize;
                        }
                        WakeMessageHandler();
                    }
                } else if (nBytes == 0) {
                    // socket closed gracefully
                    if (!pnode->fDisconnect) {
                        LogPrint(BCLog::NET, "socket closed\n");
                    }
                    pnode->CloseSocketDisconnect();
                } else if (nBytes < 0) {
                    // error
                    int nErr = WSAGetLastError();
                    if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE &&
                        nErr != WSAEINTR && nErr != WSAEINPROGRESS) {
                        if (!pnode->fDisconnect) {
                            LogPrintf("socket recv error %s\n",
                                      NetworkErrorString(nErr));
                        }
                        pnode->CloseSocketDisconnect();
                    }
                }
            }

            //
            // Send
            //
            if (sendSet) {
                LOCK(pnode->cs_vSend);
                size_t nBytes = SocketSendData(pnode);
                if (nBytes) {
                    RecordBytesSent(nBytes);
                }
            }

            //
            // Inactivity checking
            //
            int64_t nTime = GetSystemTimeInSeconds();
            if (nTime - pnode->nTimeConnected > 60) {
                auto timeout = gArgs.GetArg("-p2ptimeout", DEFAULT_P2P_TIMEOUT_INTERVAL);
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0) {
                    LogPrint(BCLog::NET, "socket no message in first 60 seconds, %d %d from %d\n",
                             pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->id);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastSend > timeout) {
                    LogPrintf("socket sending timeout: %is\n", nTime - pnode->nLastSend);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastRecv >
                           (pnode->nVersion > BIP0031_VERSION ? timeout
                                                              : 90 * 60)) {
                    LogPrintf("socket receive timeout: %is\n", nTime - pnode->nLastRecv);
                    pnode->fDisconnect = true;
                }
                else if (pnode->nPingNonceSent && pnode->nPingUsecStart + (timeout * MICROS_PER_SECOND) < GetTimeMicros()) {
                    LogPrintf("ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
                    pnode->fDisconnect = true;
                }
                else if (!pnode->fSuccessfullyConnected) {
                    LogPrintf("version handshake timeout from %d\n", pnode->id);
                    pnode->fDisconnect = true;
                }
            }
        }
    }
}

void CConnman::WakeMessageHandler() {
    {
        std::lock_guard<std::mutex> lock(mutexMsgProc);
        fMsgProcWake = true;
    }
    condMsgProc.notify_one();
}

#ifdef USE_UPNP
void ThreadMapPort() {
    std::string port = strprintf("%u", GetListenPort());
    const char *multicastif = 0;
    const char *minissdpdpath = 0;
    struct UPNPDev *devlist = 0;
    char lanaddr[64];

#ifndef UPNPDISCOVER_SUCCESS
    /* miniupnpc 1.5 */
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0);
#elif MINIUPNPC_API_VERSION < 14
    /* miniupnpc 1.6 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
    /* miniupnpc 1.9.20150730 */
    int error = 0;
    devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

    struct UPNPUrls urls;
    struct IGDdatas data;
    int r;

    r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
    if (r == 1) {
        if (fDiscover) {
            char externalIPAddress[40];
            r = UPNP_GetExternalIPAddress(
                urls.controlURL, data.first.servicetype, externalIPAddress);
            if (r != UPNPCOMMAND_SUCCESS) {
                LogPrintf("UPnP: GetExternalIPAddress() returned %d\n", r);
            } else {
                if (externalIPAddress[0]) {
                    CNetAddr resolved;
                    if (LookupHost(externalIPAddress, resolved, false)) {
                        LogPrintf("UPnP: ExternalIPAddress = %s\n",
                                  resolved.ToString().c_str());
                        AddLocal(resolved, LOCAL_UPNP);
                    }
                } else {
                    LogPrintf("UPnP: GetExternalIPAddress failed.\n");
                }
            }
        }

        std::string strDesc = "Bitcoin " + FormatFullVersion();

        try {
            while (true) {
#ifndef UPNPDISCOVER_SUCCESS
                /* miniupnpc 1.5 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                        port.c_str(), port.c_str(), lanaddr,
                                        strDesc.c_str(), "TCP", 0);
#else
                /* miniupnpc 1.6 */
                r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype,
                                        port.c_str(), port.c_str(), lanaddr,
                                        strDesc.c_str(), "TCP", 0, "0");
#endif

                if (r != UPNPCOMMAND_SUCCESS) {
                    LogPrintf(
                        "AddPortMapping(%s, %s, %s) failed with code %d (%s)\n",
                        port, port, lanaddr, r, strupnperror(r));
                } else {
                    LogPrintf("UPnP Port Mapping successful.\n");
                }

                // Refresh every 20 minutes
                MilliSleep(20 * 60 * 1000);
            }
        } catch (const boost::thread_interrupted &) {
            r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype,
                                       port.c_str(), "TCP", 0);
            LogPrintf("UPNP_DeletePortMapping() returned: %d\n", r);
            freeUPNPDevlist(devlist);
            devlist = 0;
            FreeUPNPUrls(&urls);
            throw;
        }
    } else {
        LogPrintf("No valid UPnP IGDs found\n");
        freeUPNPDevlist(devlist);
        devlist = 0;
        if (r != 0) {
            FreeUPNPUrls(&urls);
        }
    }
}

void MapPort(bool fUseUPnP) {
    static boost::thread *upnp_thread = nullptr;

    if (fUseUPnP) {
        if (upnp_thread) {
            upnp_thread->interrupt();
            upnp_thread->join();
            delete upnp_thread;
        }
        upnp_thread = new boost::thread(
            boost::bind(&TraceThread<void (*)()>, "upnp", &ThreadMapPort));
    } else if (upnp_thread) {
        upnp_thread->interrupt();
        upnp_thread->join();
        delete upnp_thread;
        upnp_thread = nullptr;
    }
}

#else
void MapPort(bool) {
    // Intentionally left blank.
}
#endif

static std::string GetDNSHost(const CDNSSeedData &data,
                              ServiceFlags *requiredServiceBits) {
    // use default host for non-filter-capable seeds or if we use the default
    // service bits (NODE_NETWORK)
    if (!data.supportsServiceBitsFiltering ||
        *requiredServiceBits == NODE_NETWORK) {
        *requiredServiceBits = NODE_NETWORK;
        return data.host;
    }

    // See chainparams.cpp, most dnsseeds only support one or two possible
    // servicebits hostnames
    return strprintf("x%x.%s", *requiredServiceBits, data.host);
}

void CConnman::ThreadDNSAddressSeed() {
    // goal: only query DNS seeds if address need is acute.
    // Avoiding DNS seeds when we don't need them improves user privacy by
    // creating fewer identifying DNS requests, reduces trust by giving seeds
    // less influence on the network topology, and reduces traffic to the seeds.
    if ((addrman.size() > 0) &&
        (!gArgs.GetBoolArg("-forcednsseed", DEFAULT_FORCEDNSSEED))) {
        if (!interruptNet.sleep_for(std::chrono::seconds(11))) {
            return;
        }

        LOCK(cs_vNodes);
        int nRelevant = 0;
        for (auto pnode : vNodes) {
            nRelevant +=
                pnode->fSuccessfullyConnected &&
                ((pnode->nServices & nRelevantServices) == nRelevantServices);
        }
        if (nRelevant >= 2) {
            LogPrintf("P2P peers available. Skipped DNS seeding.\n");
            return;
        }
    }

    const std::vector<CDNSSeedData> &vSeeds =
        config->GetChainParams().DNSSeeds();
    int found = 0;

    LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

    for (const CDNSSeedData &seed : vSeeds) {
        if (HaveNameProxy()) {
            AddOneShot(seed.host);
        } else {
            std::vector<CNetAddr> vIPs;
            std::vector<CAddress> vAdd;
            ServiceFlags requiredServiceBits = nRelevantServices;
            if (LookupHost(GetDNSHost(seed, &requiredServiceBits).c_str(), vIPs,
                           0, true)) {
                for (const CNetAddr &ip : vIPs) {
                    int nOneDay = 24 * 3600;
                    CAddress addr = CAddress(
                        CService(ip, config->GetChainParams().GetDefaultPort()),
                        requiredServiceBits);
                    // Use a random age between 3 and 7 days old.
                    addr.nTime = GetTime() - 3 * nOneDay - GetRand(4 * nOneDay);
                    vAdd.push_back(addr);
                    found++;
                }
            }
            // TODO: The seed name resolve may fail, yielding an IP of [::],
            // which results in addrman assigning the same source to results
            // from different seeds. This should switch to a hard-coded stable
            // dummy IP for each seed name, so that the resolve is not required
            // at all.
            if (!vIPs.empty()) {
                CService seedSource;
                Lookup(seed.name.c_str(), seedSource, 0, true);
                addrman.Add(vAdd, seedSource);
            }
        }
    }

    LogPrintf("%d addresses found from DNS seeds\n", found);
}

void CConnman::DumpAddresses() {
    int64_t nStart = GetTimeMillis();

    CAddrDB adb(config->GetChainParams());
    adb.Write(addrman);

    LogPrint(BCLog::NET, "Flushed %d addresses to peers.dat  %dms\n",
             addrman.size(), GetTimeMillis() - nStart);
}

void CConnman::DumpData() {
    DumpAddresses();
    DumpBanlist();
}

void CConnman::ProcessOneShot() {
    std::string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty()) {
            return;
        }
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    CSemaphoreGrant grant(semOutbound, true);
    if (grant) {
        if (!OpenNetworkConnection(addr, false, &grant, strDest.c_str(),
                                   true)) {
            AddOneShot(strDest);
        }
    }
}

void CConnman::ThreadOpenConnections() {
    // Connect to specific addresses
    if (gArgs.IsArgSet("-connect") && gArgs.GetArgs("-connect").size() > 0) {
        for (int64_t nLoop = 0;; nLoop++) {
            ProcessOneShot();
            for (const std::string &strAddr : gArgs.GetArgs("-connect")) {
                CAddress addr(CService(), NODE_NONE);
                OpenNetworkConnection(addr, false, nullptr, strAddr.c_str());
                for (int i = 0; i < 10 && i < nLoop; i++) {
                    if (!interruptNet.sleep_for(
                            std::chrono::milliseconds(500))) {
                        return;
                    }
                }
            }
            if (!interruptNet.sleep_for(std::chrono::milliseconds(500))) {
                return;
            }
        }
    }

    // Initiate network connections
    int64_t nStart = GetTime();

    // Minimum time before next feeler connection (in microseconds).
    int64_t nNextFeeler =
        PoissonNextSend(nStart * 1000 * 1000, FEELER_INTERVAL);
    while (!interruptNet) {
        ProcessOneShot();

        if (!interruptNet.sleep_for(std::chrono::milliseconds(500))) {
            return;
        }

        CSemaphoreGrant grant(semOutbound);
        if (interruptNet) {
            return;
        }

        // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
        if (addrman.size() == 0 && (GetTime() - nStart > 60)) {
            static bool done = false;
            if (!done) {
                LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be "
                          "available.\n");
                CNetAddr local;
                LookupHost("127.0.0.1", local, false);
                addrman.Add(convertSeed6(config->GetChainParams().FixedSeeds()),
                            local);
                done = true;
            }
        }

        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4). Do
        // this here so we don't have to critsect vNodes inside mapAddresses
        // critsect.
        int nOutbound = 0;
        std::set<std::vector<uint8_t>> setConnected;
        {
            LOCK(cs_vNodes);
            for (const CNodePtr& pnode : vNodes) {
                if (!pnode->fInbound && !pnode->fAddnode) {
                    // Netgroups for inbound and addnode peers are not excluded
                    // because our goal here is to not use multiple of our
                    // limited outbound slots on a single netgroup but inbound
                    // and addnode peers do not use our outbound slots. Inbound
                    // peers also have the added issue that they're attacker
                    // controlled and could be used to prevent us from
                    // connecting to particular hosts if we used them here.
                    setConnected.insert(pnode->addr.GetGroup());
                    nOutbound++;
                }
            }
        }

        // Feeler Connections
        //
        // Design goals:
        //  * Increase the number of connectable addresses in the tried table.
        //
        // Method:
        //  * Choose a random address from new and attempt to connect to it if
        //  we can connect successfully it is added to tried.
        //  * Start attempting feeler connections only after node finishes
        //  making outbound connections.
        //  * Only make a feeler connection once every few minutes.
        //
        bool fFeeler = false;
        if (nOutbound >= nMaxOutbound) {
            // The current time right now (in microseconds).
            int64_t nTime = GetTimeMicros();
            if (nTime > nNextFeeler) {
                nNextFeeler = PoissonNextSend(nTime, FEELER_INTERVAL);
                fFeeler = true;
            } else {
                continue;
            }
        }

        int64_t nANow = GetAdjustedTime();
        int nTries = 0;
        while (!interruptNet) {
            CAddrInfo addr = addrman.Select(fFeeler);

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup()) ||
                IsLocal(addr)) {
                break;
            }

            // If we didn't find an appropriate destination after trying 100
            // addresses fetched from addrman, stop this loop, and let the outer
            // loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman
            // addresses.
            nTries++;
            if (nTries > 100) {
                break;
            }

            if (IsLimited(addr)) {
                continue;
            }

            // only connect to full nodes
            if ((addr.nServices & REQUIRED_SERVICES) != REQUIRED_SERVICES) {
                continue;
            }

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30) {
                continue;
            }

            // only consider nodes missing relevant services after 40 failed
            // attempts and only if less than half the outbound are up.
            if ((addr.nServices & nRelevantServices) != nRelevantServices &&
                (nTries < 40 || nOutbound >= (nMaxOutbound >> 1))) {
                continue;
            }

            // do not allow non-default ports, unless after 50 invalid addresses
            // selected already.
            if (addr.GetPort() != config->GetChainParams().GetDefaultPort() &&
                nTries < 50) {
                continue;
            }

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid()) {

            if (fFeeler) {
                // Add small amount of random noise before connection to avoid
                // synchronization.
                int randsleep = GetRandInt(FEELER_SLEEP_WINDOW * 1000);
                if (!interruptNet.sleep_for(
                        std::chrono::milliseconds(randsleep))) {
                    return;
                }
                LogPrint(BCLog::NET, "Making feeler connection to %s\n",
                         addrConnect.ToString());
            }

            OpenNetworkConnection(addrConnect,
                                  (int)setConnected.size() >=
                                      std::min(nMaxConnections - 1, 2),
                                  &grant, nullptr, false, fFeeler);
        }
    }
}

std::vector<AddedNodeInfo> CConnman::GetAddedNodeInfo() {
    std::vector<AddedNodeInfo> ret;

    std::list<std::string> lAddresses(0);
    {
        LOCK(cs_vAddedNodes);
        ret.reserve(vAddedNodes.size());
        for (const std::string &strAddNode : vAddedNodes) {
            lAddresses.push_back(strAddNode);
        }
    }

    // Build a map of all already connected addresses (by IP:port and by name)
    // to inbound/outbound and resolved CService
    std::map<CService, bool> mapConnected;
    std::map<std::string, std::pair<bool, CService>> mapConnectedByName;
    {
        LOCK(cs_vNodes);
        for (const CNodePtr& pnode : vNodes) {
            if (pnode->addr.IsValid()) {
                mapConnected[pnode->addr] = pnode->fInbound;
            }
            std::string addrName = pnode->GetAddrName();
            if (!addrName.empty()) {
                mapConnectedByName[std::move(addrName)] =
                    std::make_pair(pnode->fInbound,
                                   static_cast<const CService &>(pnode->addr));
            }
        }
    }

    for (const std::string &strAddNode : lAddresses) {
        CService service(LookupNumeric(
            strAddNode.c_str(), config->GetChainParams().GetDefaultPort()));
        if (service.IsValid()) {
            // strAddNode is an IP:port
            auto it = mapConnected.find(service);
            if (it != mapConnected.end()) {
                ret.push_back(
                    AddedNodeInfo{strAddNode, service, true, it->second});
            } else {
                ret.push_back(
                    AddedNodeInfo{strAddNode, CService(), false, false});
            }
        } else {
            // strAddNode is a name
            auto it = mapConnectedByName.find(strAddNode);
            if (it != mapConnectedByName.end()) {
                ret.push_back(AddedNodeInfo{strAddNode, it->second.second, true,
                                            it->second.first});
            } else {
                ret.push_back(
                    AddedNodeInfo{strAddNode, CService(), false, false});
            }
        }
    }

    return ret;
}

void CConnman::ThreadOpenAddedConnections() {
    {
        LOCK(cs_vAddedNodes);
        if (gArgs.IsArgSet("-addnode")) {
            vAddedNodes = gArgs.GetArgs("-addnode");
        }
    }

    while (true) {
        CSemaphoreGrant grant(semAddnode);
        std::vector<AddedNodeInfo> vInfo = GetAddedNodeInfo();
        bool tried = false;
        for (const AddedNodeInfo &info : vInfo) {
            if (!info.fConnected) {
                if (!grant.TryAcquire()) {
                    // If we've used up our semaphore and need a new one, lets
                    // not wait here since while we are waiting the
                    // addednodeinfo state might change.
                    break;
                }
                // If strAddedNode is an IP/port, decode it immediately, so
                // OpenNetworkConnection can detect existing connections to that
                // IP/port.
                tried = true;
                CService service(
                    LookupNumeric(info.strAddedNode.c_str(),
                                  config->GetChainParams().GetDefaultPort()));
                OpenNetworkConnection(CAddress(service, NODE_NONE), false,
                                      &grant, info.strAddedNode.c_str(), false,
                                      false, true);
                if (!interruptNet.sleep_for(std::chrono::milliseconds(500))) {
                    return;
                }
            }
        }
        // Retry every 60 seconds if a connection was attempted, otherwise two
        // seconds.
        if (!interruptNet.sleep_for(std::chrono::seconds(tried ? 60 : 2))) {
            return;
        }
    }
}

// If successful, this moves the passed grant to the constructed node.
bool CConnman::OpenNetworkConnection(const CAddress &addrConnect,
                                     bool fCountFailure,
                                     CSemaphoreGrant *grantOutbound,
                                     const char *pszDest, bool fOneShot,
                                     bool fFeeler, bool fAddnode) {
    //
    // Initiate outbound network connection
    //
    if (interruptNet) {
        return false;
    }
    if (!fNetworkActive) {
        return false;
    }
    if (!pszDest) {
        if (IsLocal(addrConnect) || FindNode((CNetAddr)addrConnect) ||
            IsBanned(addrConnect) || FindNode(addrConnect.ToStringIPPort())) {
            return false;
        }
    } else if (FindNode(std::string(pszDest))) {
        return false;
    }

    CNodePtr pnode = ConnectNode(addrConnect, pszDest, fCountFailure);

    if (!pnode) {
        return false;
    }
    if (grantOutbound) {
        grantOutbound->MoveTo(pnode->grantOutbound);
    }
    if (fOneShot) {
        pnode->fOneShot = true;
    }
    if (fFeeler) {
        pnode->fFeeler = true;
    }
    if (fAddnode) {
        pnode->fAddnode = true;
    }

    GetNodeSignals().InitializeNode(pnode, *this);
    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }

    return true;
}

namespace
{
    /**
     * Helper class for logging the duration of ThreadMessageHandler reqest
     * processing. It writes to log all the requests that take more time to
     * process than the provided threshold.
     */
    class CLogP2PStallDuration
    {
    public:
        CLogP2PStallDuration(
            std::string command,
            std::chrono::milliseconds debugP2PTheadStallsThreshold)
            : mDebugP2PTheadStallsThreshold{debugP2PTheadStallsThreshold}
            , mProcessingStart{std::chrono::steady_clock::now()}
            , mCommand{std::move(command)}
        {/**/}


        ~CLogP2PStallDuration()
        {
            if(!mCommand.empty())
            {
                auto processingDuration =
                        std::chrono::steady_clock::now() - mProcessingStart;

                if(processingDuration > mDebugP2PTheadStallsThreshold)
                {
                    LogPrint(
                        BCLog::NET,
                        "CConnman request processing took %s ms to complete "
                        "processing '%s' request!\n",
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            processingDuration).count(),
                        mCommand);
                }
            }
        }

    private:
        std::chrono::milliseconds mDebugP2PTheadStallsThreshold;
        std::chrono::time_point<std::chrono::steady_clock> mProcessingStart;
        std::string mCommand;
    };
}

void CConnman::ThreadMessageHandler()
{
    std::vector<CNodePtr> vNodesCopy;

    while (!flagInterruptMsgProc)
    {
        vNodesCopy.clear();

        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
        }

        bool fMoreWork = false;

        mAsyncTaskPool.HandleCompletedAsyncProcessing();

        for (const CNodePtr& pnode : vNodesCopy)
        {
            if (pnode->fDisconnect ||
                mAsyncTaskPool.HasReachedSoftAsyncTaskLimit(pnode->GetId()))
            {
                continue;
            }

            std::optional<CLogP2PStallDuration> durationLog;

            using namespace std::literals::chrono_literals;

            if(mDebugP2PTheadStallsThreshold > 0ms)
            {
                LOCK(pnode->cs_vProcessMsg);
                if(!pnode->vProcessMsg.empty())
                {
                    durationLog =
                        {
                            pnode->vProcessMsg.begin()->hdr.GetCommand(),
                            mDebugP2PTheadStallsThreshold
                        };
                }
            }

            // Receive messages
            bool fMoreNodeWork = GetNodeSignals().ProcessMessages(
                *config, pnode, *this, flagInterruptMsgProc);
            fMoreWork |= (fMoreNodeWork && !pnode->fPauseSend);

            if (flagInterruptMsgProc) {
                return;
            }

            // Send messages
            {
                LOCK(pnode->cs_sendProcessing);
                GetNodeSignals().SendMessages(*config, pnode, *this,
                                              flagInterruptMsgProc);
            }

            if (flagInterruptMsgProc) {
                return;
            }
        }

        std::unique_lock<std::mutex> lock(mutexMsgProc);
        if (!fMoreWork) {
            condMsgProc.wait_until(lock,
                                   std::chrono::steady_clock::now() +
                                       std::chrono::milliseconds(100),
                                   [this] { return fMsgProcWake; });
        }
        fMsgProcWake = false;
    }
}

bool CConnman::BindListenPort(const CService &addrBind, std::string &strError,
                              bool fWhitelisted) {
    strError = "";
    int nOne = 1;

    // Create socket for listening for incoming connections
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr *)&sockaddr, &len)) {
        strError = strprintf("Error: Bind address family for %s not supported",
                             addrBind.ToString());
        LogPrintf("%s\n", strError);
        return false;
    }

    SOCKET hListenSocket = socket(((struct sockaddr *)&sockaddr)->sa_family,
                                  SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET) {
        strError = strprintf("Error: Couldn't open socket for incoming "
                             "connections (socket returned error %s)",
                             NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }
    if (!IsSelectableSocket(hListenSocket)) {
        strError = "Error: Couldn't create a listenable socket for incoming "
                   "connections";
        LogPrintf("%s\n", strError);
        return false;
    }

#ifndef WIN32
#ifdef SO_NOSIGPIPE
    // Different way of disabling SIGPIPE on BSD
    setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void *)&nOne,
               sizeof(int));
#endif
    // Allow binding if the port is still in TIME_WAIT state after
    // the program was closed and restarted.
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void *)&nOne,
               sizeof(int));
    // Disable Nagle's algorithm
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (void *)&nOne,
               sizeof(int));
#else
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (const char *)&nOne,
               sizeof(int));
    setsockopt(hListenSocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&nOne,
               sizeof(int));
#endif

    // Set to non-blocking, incoming connections will also inherit this
    if (!SetSocketNonBlocking(hListenSocket, true)) {
        strError = strprintf("BindListenPort: Setting listening socket to "
                             "non-blocking failed, error %s\n",
                             NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }

    // Some systems don't have IPV6_V6ONLY but are always v6only; others do have
    // the option and enable it by default or not. Try to enable it, if
    // possible.
    if (addrBind.IsIPv6()) {
#ifdef IPV6_V6ONLY
#ifdef WIN32
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY,
                   (const char *)&nOne, sizeof(int));
#else
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&nOne,
                   sizeof(int));
#endif
#endif
#ifdef WIN32
        int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL,
                   (const char *)&nProtLevel, sizeof(int));
#endif
    }

    if (::bind(hListenSocket, (struct sockaddr *)&sockaddr, len) ==
        SOCKET_ERROR) {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE) {
            strError = strprintf(_("Unable to bind to %s on this computer. %s "
                                   "is probably already running."),
                                 addrBind.ToString(), _(PACKAGE_NAME));
        } else {
            strError = strprintf(_("Unable to bind to %s on this computer "
                                   "(bind returned error %s)"),
                                 addrBind.ToString(), NetworkErrorString(nErr));
        }
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }
    LogPrintf("Bound to %s\n", addrBind.ToString());

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        strError = strprintf(_("Error: Listening for incoming connections "
                               "failed (listen returned error %s)"),
                             NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }

    vhListenSocket.push_back(ListenSocket(hListenSocket, fWhitelisted));

    if (addrBind.IsRoutable() && fDiscover && !fWhitelisted) {
        AddLocal(addrBind, LOCAL_BIND);
    }

    return true;
}

void Discover(boost::thread_group &threadGroup) {
    if (!fDiscover) {
        return;
    }

#ifdef WIN32
    // Get local host IP
    char pszHostName[256] = "";
    if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR) {
        std::vector<CNetAddr> vaddr;
        if (LookupHost(pszHostName, vaddr, 0, true)) {
            for (const CNetAddr &addr : vaddr) {
                if (AddLocal(addr, LOCAL_IF)) {
                    LogPrintf("%s: %s - %s\n", __func__, pszHostName,
                              addr.ToString());
                }
            }
        }
    }
#else
    // Get local host ip
    struct ifaddrs *myaddrs;
    if (getifaddrs(&myaddrs) == 0) {
        for (struct ifaddrs *ifa = myaddrs; ifa != nullptr;
             ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr || (ifa->ifa_flags & IFF_UP) == 0 ||
                strcmp(ifa->ifa_name, "lo") == 0 ||
                strcmp(ifa->ifa_name, "lo0") == 0) {
                continue;
            }
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *s4 = reinterpret_cast<sockaddr_in *>(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LOCAL_IF)) {
                    LogPrintf("%s: IPv4 %s: %s\n", __func__, ifa->ifa_name,
                              addr.ToString());
                }
            } else if (ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *s6 =
                    reinterpret_cast<sockaddr_in6 *>(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (AddLocal(addr, LOCAL_IF)) {
                    LogPrintf("%s: IPv6 %s: %s\n", __func__, ifa->ifa_name,
                              addr.ToString());
                }
            }
        }
        freeifaddrs(myaddrs);
    }
#endif
}

void CConnman::SetNetworkActive(bool active) {
    LogPrint(BCLog::NET, "SetNetworkActive: %s\n", active);

    if (!active) {
        fNetworkActive = false;

        LOCK(cs_vNodes);
        // Close sockets to all nodes
        for (const CNodePtr& pnode : vNodes) {
            pnode->CloseSocketDisconnect();
        }
    } else {
        fNetworkActive = true;
    }

    uiInterface.NotifyNetworkActiveChanged(fNetworkActive);
}

CConnman::CConnman(
    const Config &configIn,
    uint64_t nSeed0In,
    uint64_t nSeed1In,
    std::chrono::milliseconds debugP2PTheadStallsThreshold)
    : config(&configIn)
    , nSeed0(nSeed0In)
    , nSeed1(nSeed1In)
    , mValidatorThreadPool{"TxnValidatorPool",
         static_cast<size_t>(gArgs.GetArg("-numstdtxvalidationthreads", GetNumHighPriorityValidationThrs())),
         static_cast<size_t>(gArgs.GetArg("-numnonstdtxvalidationthreads", GetNumLowPriorityValidationThrs()))}
    , mDebugP2PTheadStallsThreshold{debugP2PTheadStallsThreshold}
    , mAsyncTaskPool{configIn}
{
    fNetworkActive = true;
    setBannedIsDirty = false;
    fAddressesInitialized = false;
    nLastNodeId = 0;
    nSendBufferMaxSize = 0;
    nReceiveFloodSize = 0;
    nMaxConnections = 0;
    nMaxOutbound = 0;
    nMaxAddnode = 0;
    nBestHeight = 0;
    clientInterface = nullptr;
    flagInterruptMsgProc = false;
    // Create an instance of the Validator
    mTxnValidator =
        std::make_shared<CTxnValidator>(
            configIn,
            mempool,
            std::make_shared<CTxnDoubleSpendDetector>());
    mTxnPropagator = std::make_shared<CTxnPropagator>();
}

NodeId CConnman::GetNewNodeId() {
    return nLastNodeId.fetch_add(1, std::memory_order_relaxed);
}

bool CConnman::Start(CScheduler &scheduler, std::string &strNodeError,
                     Options connOptions) {
    nTotalBytesRecv = 0;
    nTotalBytesSent = 0;
    nMaxOutboundTotalBytesSentInCycle = 0;
    nMaxOutboundCycleStartTime = 0;

    nRelevantServices = connOptions.nRelevantServices;
    nLocalServices = connOptions.nLocalServices;
    nMaxConnections = connOptions.nMaxConnections;
    nMaxOutbound = std::min((connOptions.nMaxOutbound), nMaxConnections);
    nMaxAddnode = connOptions.nMaxAddnode;
    nMaxFeeler = connOptions.nMaxFeeler;

    nSendBufferMaxSize = connOptions.nSendBufferMaxSize;
    nReceiveFloodSize = connOptions.nReceiveFloodSize;

    nMaxOutboundLimit = connOptions.nMaxOutboundLimit;
    nMaxOutboundTimeframe = connOptions.nMaxOutboundTimeframe;

    SetBestHeight(connOptions.nBestHeight);

    clientInterface = connOptions.uiInterface;
    if (clientInterface) {
        clientInterface->InitMessage(_("Loading addresses..."));
    }
    // Load addresses from peers.dat
    int64_t nStart = GetTimeMillis();
    {
        CAddrDB adb(config->GetChainParams());
        if (adb.Read(addrman)) {
            LogPrintf("Loaded %i addresses from peers.dat  %dms\n",
                      addrman.size(), GetTimeMillis() - nStart);
        } else {
            // Addrman can be in an inconsistent state after failure, reset it
            addrman.Clear();
            LogPrintf("Invalid or missing peers.dat; recreating\n");
            DumpAddresses();
        }
    }
    if (clientInterface) {
        clientInterface->InitMessage(_("Loading banlist..."));
    }
    // Load addresses from banlist.dat
    nStart = GetTimeMillis();
    CBanDB bandb(config->GetChainParams());
    banmap_t banmap;
    if (bandb.Read(banmap)) {
        // thread save setter
        SetBanned(banmap);
        // no need to write down, just read data
        SetBannedSetDirty(false);
        // sweep out unused entries
        SweepBanned();

        LogPrint(BCLog::NET,
                 "Loaded %d banned node ips/subnets from banlist.dat  %dms\n",
                 banmap.size(), GetTimeMillis() - nStart);
    } else {
        LogPrintf("Invalid or missing banlist.dat; recreating\n");
        // force write
        SetBannedSetDirty(true);
        DumpBanlist();
    }

    uiInterface.InitMessage(_("Starting network threads..."));

    fAddressesInitialized = true;

    if (semOutbound == nullptr) {
        // initialize semaphore
        semOutbound = std::make_shared<CSemaphore>(std::min((nMaxOutbound + nMaxFeeler), nMaxConnections));
    }
    if (semAddnode == nullptr) {
        // initialize semaphore
        semAddnode = std::make_shared<CSemaphore>(nMaxAddnode);
    }

    //
    // Start threads
    //
    InterruptSocks5(false);
    interruptNet.reset();
    flagInterruptMsgProc = false;

    {
        std::unique_lock<std::mutex> lock(mutexMsgProc);
        fMsgProcWake = false;
    }

    // Send and receive from sockets, accept connections
    threadSocketHandler = std::thread(
        &TraceThread<std::function<void()>>, "net",
        std::function<void()>(std::bind(&CConnman::ThreadSocketHandler, this)));

    if (!gArgs.GetBoolArg("-dnsseed", true)) {
        LogPrintf("DNS seeding disabled\n");
    } else {
        threadDNSAddressSeed =
            std::thread(&TraceThread<std::function<void()>>, "dnsseed",
                        std::function<void()>(
                            std::bind(&CConnman::ThreadDNSAddressSeed, this)));
    }

    // Initiate outbound connections from -addnode
    threadOpenAddedConnections =
        std::thread(&TraceThread<std::function<void()>>, "addcon",
                    std::function<void()>(std::bind(
                        &CConnman::ThreadOpenAddedConnections, this)));

    // Initiate outbound connections unless connect=0
    if (!gArgs.IsArgSet("-connect") || gArgs.GetArgs("-connect").size() != 1 ||
        gArgs.GetArgs("-connect")[0] != "0") {
        threadOpenConnections =
            std::thread(&TraceThread<std::function<void()>>, "opencon",
                        std::function<void()>(
                            std::bind(&CConnman::ThreadOpenConnections, this)));
    }

    // Process messages
    threadMessageHandler =
        std::thread(&TraceThread<std::function<void()>>, "msghand",
                    std::function<void()>(
                        std::bind(&CConnman::ThreadMessageHandler, this)));

    // Dump network addresses
    scheduler.scheduleEvery(std::bind(&CConnman::DumpData, this),
                            DUMP_ADDRESSES_INTERVAL * 1000);

    // Schedule average bandwidth measurements
    scheduler.scheduleEvery(std::bind(&CConnman::PeerAvgBandwithCalc, this),
                            PEER_AVG_BANDWIDTH_CALC_FREQUENCY_SECS * 1000);


    return true;
}

class CNetCleanup {
public:
    CNetCleanup() {}

    ~CNetCleanup() {
#ifdef WIN32
        // Shutdown Windows Sockets
        WSACleanup();
#endif
    }
} instance_of_cnetcleanup;

void CConnman::Interrupt() {
    {
        std::lock_guard<std::mutex> lock(mutexMsgProc);
        flagInterruptMsgProc = true;
    }
    condMsgProc.notify_all();

    interruptNet();
    InterruptSocks5(true);

    if (semOutbound) {
        for (int i = 0; i < (nMaxOutbound + nMaxFeeler); i++) {
            semOutbound->post();
        }
    }

    if (semAddnode) {
        for (int i = 0; i < nMaxAddnode; i++) {
            semAddnode->post();
        }
    }
}

void CConnman::Stop() {
    if (threadMessageHandler.joinable()) {
        threadMessageHandler.join();
    }
    if (threadOpenConnections.joinable()) {
        threadOpenConnections.join();
    }
    if (threadOpenAddedConnections.joinable()) {
        threadOpenAddedConnections.join();
    }
    if (threadDNSAddressSeed.joinable()) {
        threadDNSAddressSeed.join();
    }
    if (threadSocketHandler.joinable()) {
        threadSocketHandler.join();
    }

    if (fAddressesInitialized) {
        DumpData();
        fAddressesInitialized = false;
    }

   mTxnValidator->shutdown();
   mTxnPropagator->shutdown();

    // Close sockets
    for (const CNodePtr& pnode : vNodes) {
        pnode->CloseSocketDisconnect();
    }
    for (ListenSocket &hListenSocket : vhListenSocket) {
        if (hListenSocket.socket != INVALID_SOCKET) {
            if (!CloseSocket(hListenSocket.socket)) {
                LogPrintf("CloseSocket(hListenSocket) failed with error %s\n",
                          NetworkErrorString(WSAGetLastError()));
            }
        }
    }

    // clean up some globals (to help leak detection)
    for (const CNodePtr& pnode : vNodes) {
        DeleteNode(pnode);
    }
    for (const CNodePtr& pnode : vNodesDisconnected) {
        DeleteNode(pnode);
    }
    vNodes.clear();
    vNodesDisconnected.clear();
    vhListenSocket.clear();
    semOutbound = nullptr;
    semAddnode = nullptr;
}

void CConnman::DeleteNode(const CNodePtr& pnode) {
    assert(pnode);
    bool fUpdateConnectionTime = false;
    GetNodeSignals().FinalizeNode(pnode->GetId(), fUpdateConnectionTime);
    if (fUpdateConnectionTime) {
        addrman.Connected(pnode->addr);
    }
}

CConnman::~CConnman() {
    Interrupt();
    Stop();
}

size_t CConnman::GetAddressCount() const {
    return addrman.size();
}

void CConnman::SetServices(const CService &addr, ServiceFlags nServices) {
    addrman.SetServices(addr, nServices);
}

void CConnman::MarkAddressGood(const CAddress &addr) {
    addrman.Good(addr);
}

void CConnman::AddNewAddress(const CAddress &addr, const CAddress &addrFrom,
                             int64_t nTimePenalty) {
    addrman.Add(addr, addrFrom, nTimePenalty);
}

void CConnman::AddNewAddresses(const std::vector<CAddress> &vAddr,
                               const CAddress &addrFrom, int64_t nTimePenalty) {
    addrman.Add(vAddr, addrFrom, nTimePenalty);
}

std::vector<CAddress> CConnman::GetAddresses() {
    return addrman.GetAddr();
}

bool CConnman::AddNode(const std::string &strNode) {
    LOCK(cs_vAddedNodes);
    for (std::vector<std::string>::const_iterator it = vAddedNodes.begin();
         it != vAddedNodes.end(); ++it) {
        if (strNode == *it) {
            return false;
        }
    }

    vAddedNodes.push_back(strNode);
    return true;
}

bool CConnman::RemoveAddedNode(const std::string &strNode) {
    LOCK(cs_vAddedNodes);
    for (std::vector<std::string>::iterator it = vAddedNodes.begin();
         it != vAddedNodes.end(); ++it) {
        if (strNode == *it) {
            vAddedNodes.erase(it);
            return true;
        }
    }
    return false;
}

size_t CConnman::GetNodeCount(NumConnections flags) {
    LOCK(cs_vNodes);
    // Shortcut if we want total
    if (flags == CConnman::CONNECTIONS_ALL) {
        return vNodes.size();
    }

    int nNum = 0;
    for(const CNodePtr& node : vNodes) {
        if (flags & (node->fInbound ? CONNECTIONS_IN : CONNECTIONS_OUT)) {
            nNum++;
        }
    }

    return nNum;
}

void CConnman::GetNodeStats(std::vector<CNodeStats> &vstats) {
    vstats.clear();
    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    for (const CNodePtr& pnode : vNodes) {
        vstats.emplace_back();
        pnode->copyStats(vstats.back());
    }
}

bool CConnman::DisconnectNode(const std::string &strNode) {
    LOCK(cs_vNodes);
    if (const CNodePtr& pnode = FindNode(strNode)) {
        pnode->fDisconnect = true;
        return true;
    }
    return false;
}
bool CConnman::DisconnectNode(NodeId id) {
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if (id == pnode->id) {
            pnode->fDisconnect = true;
            return true;
        }
    }
    return false;
}

void CConnman::RecordBytesRecv(uint64_t bytes) {
    LOCK(cs_totalBytesRecv);
    nTotalBytesRecv += bytes;
}

void CConnman::RecordBytesSent(uint64_t bytes) {
    LOCK(cs_totalBytesSent);
    nTotalBytesSent += bytes;

    uint64_t now = GetTime();
    if (nMaxOutboundCycleStartTime + nMaxOutboundTimeframe < now) {
        // timeframe expired, reset cycle
        nMaxOutboundCycleStartTime = now;
        nMaxOutboundTotalBytesSentInCycle = 0;
    }

    // TODO, exclude whitebind peers
    nMaxOutboundTotalBytesSentInCycle += bytes;
}

void CConnman::SetMaxOutboundTarget(uint64_t limit) {
    LOCK(cs_totalBytesSent);
    nMaxOutboundLimit = limit;
}

uint64_t CConnman::GetMaxOutboundTarget() {
    LOCK(cs_totalBytesSent);
    return nMaxOutboundLimit;
}

uint64_t CConnman::GetMaxOutboundTimeframe() {
    LOCK(cs_totalBytesSent);
    return nMaxOutboundTimeframe;
}

uint64_t CConnman::GetMaxOutboundTimeLeftInCycle() {
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundLimit == 0) {
        return 0;
    }

    if (nMaxOutboundCycleStartTime == 0) {
        return nMaxOutboundTimeframe;
    }

    uint64_t cycleEndTime = nMaxOutboundCycleStartTime + nMaxOutboundTimeframe;
    uint64_t now = GetTime();
    return (cycleEndTime < now) ? 0 : cycleEndTime - GetTime();
}

void CConnman::SetMaxOutboundTimeframe(uint64_t timeframe) {
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundTimeframe != timeframe) {
        // reset measure-cycle in case of changing the timeframe.
        nMaxOutboundCycleStartTime = GetTime();
    }
    nMaxOutboundTimeframe = timeframe;
}

bool CConnman::OutboundTargetReached(bool historicalBlockServingLimit) {
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundLimit == 0) {
        return false;
    }

    if (historicalBlockServingLimit) {
        // keep a large enough buffer to at least relay each block once.
        uint64_t timeLeftInCycle = GetMaxOutboundTimeLeftInCycle();
        uint64_t buffer = timeLeftInCycle / 600 * ONE_MEGABYTE;
        if (buffer >= nMaxOutboundLimit ||
            nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit - buffer) {
            return true;
        }
    } else if (nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit) {
        return true;
    }

    return false;
}

uint64_t CConnman::GetOutboundTargetBytesLeft() {
    LOCK(cs_totalBytesSent);
    if (nMaxOutboundLimit == 0) {
        return 0;
    }

    return (nMaxOutboundTotalBytesSentInCycle >= nMaxOutboundLimit)
               ? 0
               : nMaxOutboundLimit - nMaxOutboundTotalBytesSentInCycle;
}

uint64_t CConnman::GetTotalBytesRecv() {
    LOCK(cs_totalBytesRecv);
    return nTotalBytesRecv;
}

uint64_t CConnman::GetTotalBytesSent() {
    LOCK(cs_totalBytesSent);
    return nTotalBytesSent;
}

ServiceFlags CConnman::GetLocalServices() const {
    return nLocalServices;
}

void CConnman::SetBestHeight(int height) {
    nBestHeight.store(height, std::memory_order_release);
}

int CConnman::GetBestHeight() const {
    return nBestHeight.load(std::memory_order_acquire);
}

unsigned int CConnman::GetReceiveFloodSize() const {
    return nReceiveFloodSize;
}
unsigned int CConnman::GetSendBufferSize() const {
    return nSendBufferMaxSize;
}

// Calculate average bandwidth for our peers
void CConnman::PeerAvgBandwithCalc()
{
    LOCK(cs_vNodes);
    for(const CNodePtr& pnode : vNodes)
    {
        LOCK(pnode->cs_vRecv);
        int64_t currTime { GetTimeMicros() };
        if(pnode->nLastSpotMeasurementTime > 0)
        {
            double secsSinceLastSpot { static_cast<double>(currTime - pnode->nLastSpotMeasurementTime) / MICROS_PER_SECOND };
            if(secsSinceLastSpot > 0)
            {
                double spotbw { pnode->nBytesRecvThisSpot / secsSinceLastSpot };
                pnode->vAvgBandwidth.push_back(spotbw);
            }
        }

        pnode->nLastSpotMeasurementTime = currTime;
        pnode->nBytesRecvThisSpot = 0;
    }
}

CNode::CNode(
    NodeId idIn,
    ServiceFlags nLocalServicesIn,
    int nMyStartingHeightIn,
    SOCKET hSocketIn,
    const CAddress& addrIn,
    uint64_t nKeyedNetGroupIn,
    uint64_t nLocalHostNonceIn,
    CConnman::CAsyncTaskPool& asyncTaskPool,
    const std::string& addrNameIn,
    bool fInboundIn)
    : hSocket(hSocketIn)
    , nTimeConnected(GetSystemTimeInSeconds())
    , addr(addrIn)
    , fInbound(fInboundIn)
    , id(idIn)
    , nKeyedNetGroup(nKeyedNetGroupIn)
    , nLocalHostNonce(nLocalHostNonceIn)
    , nLocalServices(nLocalServicesIn)
    , nMyStartingHeight(nMyStartingHeightIn)
    , mAsyncTaskPool{asyncTaskPool}
{
    addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;

    for (const std::string &msg : getAllNetMessageTypes()) {
        mapRecvBytesPerMsgCmd[msg] = 0;
    }
    mapRecvBytesPerMsgCmd[NET_MESSAGE_COMMAND_OTHER] = 0;

    if (fLogIPs) {
        LogPrint(BCLog::NET, "Added connection to %s peer=%d\n", addrName, id);
    } else {
        LogPrint(BCLog::NET, "Added connection peer=%d\n", id);
    }
}

CNode::~CNode()
{
    CloseSocket(hSocket);
}

auto CNode::SendMessage(CForwardAsyncReadonlyStream& data, size_t maxChunkSize)
    -> CSendResult
{
    if (maxChunkSize == 0)
    {
        // if maxChunkSize is 0 assign some default chunk size value
        maxChunkSize = 1024;
    }
    size_t sentSize = 0;

    do
    {
        int nBytes = 0;
        if (!mSendChunk)
        {
            mSendChunk = data.ReadAsync(maxChunkSize);

            if (!mSendChunk->Size())
            {
                // we need to wait for data to load so we should let others
                // send data in the meantime
                mSendChunk = std::nullopt;
                return {false, sentSize};
            }
        }

        {
            LOCK(cs_hSocket);
            if (hSocket == INVALID_SOCKET)
            {
                return {false, sentSize};
            }

            nBytes = send(hSocket,
                          reinterpret_cast<const char *>(mSendChunk->Begin()),
                          mSendChunk->Size(),
                          MSG_NOSIGNAL | MSG_DONTWAIT);
        }

        if (nBytes == 0)
        {
            // couldn't send anything at all
            return {false, sentSize};
        }
        if (nBytes < 0)
        {
            // error
            int nErr = WSAGetLastError();
            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE &&
                nErr != WSAEINTR && nErr != WSAEINPROGRESS)
            {
                LogPrintf("socket send error %s\n", NetworkErrorString(nErr));
                CloseSocketDisconnect();
            }

            return {false, sentSize};
        }

        assert(nBytes > 0);
        nLastSend = GetSystemTimeInSeconds();
        nSendBytes += nBytes;
        sentSize += nBytes;
        if (static_cast<size_t>(nBytes) != mSendChunk->Size())
        {
            // could not send full message; stop sending more
            mSendChunk =
                CSpan{
                    mSendChunk->Begin() + nBytes,
                    mSendChunk->Size() - nBytes
                };
            return {false, sentSize};
        }

        mSendChunk = std::nullopt;
    } while(!data.EndOfStream());

    return {true, sentSize};
}

void CNode::AskFor(const CInv &inv) {
    LOCK(cs_invQueries);
    // if mapAskFor is too large, we will never ask for it (it becomes lost)
    if (mapAskFor.size() > MAPASKFOR_MAX_SIZE ||
        setAskFor.size() > SETASKFOR_MAX_SIZE) {
        return;
    }

    // a peer may not have multiple non-responded queue positions for a single
    // inv item.
    if (!setAskFor.insert(inv.hash).second) {
        return;
    }

    // We're using mapAskFor as a priority queue, the key is the earliest time
    // the request can be sent.
    int64_t nRequestTime;
    limitedmap<uint256, int64_t>::const_iterator it =
        mapAlreadyAskedFor.find(inv.hash);
    if (it != mapAlreadyAskedFor.end()) {
        nRequestTime = it->second;
    } else {
        nRequestTime = 0;
    }
    LogPrint(BCLog::NET, "askfor %s  %d (%s) peer=%d\n", inv.ToString(),
             nRequestTime,
             DateTimeStrFormat("%H:%M:%S", nRequestTime / MICROS_PER_SECOND), id);

    // Make sure not to reuse time indexes to keep things in the same order
    int64_t nNow = GetTimeMicros() - MICROS_PER_SECOND;
    static int64_t nLastTime;
    ++nLastTime;
    nNow = std::max(nNow, nLastTime);
    nLastTime = nNow;

    // Each retry is 1 minute after the last
    nRequestTime = std::max(nRequestTime + 1 * 60 * MICROS_PER_SECOND, nNow);
    if (it != mapAlreadyAskedFor.end()) {
        mapAlreadyAskedFor.update(it, nRequestTime);
    } else {
        mapAlreadyAskedFor.insert(std::make_pair(inv.hash, nRequestTime));
    }
    mapAskFor.insert(std::make_pair(nRequestTime, inv));
}

bool CConnman::NodeFullyConnected(const CNodePtr& pnode) {
    return pnode && pnode->fSuccessfullyConnected && !pnode->fDisconnect;
}

void CConnman::PushMessage(const CNodePtr& pnode, CSerializedNetMsg &&msg) {
    size_t nPayloadLength = msg.Size();
    size_t nTotalSize = nPayloadLength + CMessageHeader::HEADER_SIZE;

    if (nPayloadLength > std::numeric_limits<uint32_t>::max())
    {
        LogPrint(BCLog::NET, "message %s (%d bytes) cannot be sent because it exceeds max P2P message limit peer=%d\n",
            SanitizeString(msg.Command().c_str()), nPayloadLength, pnode->id);
        return;
    }
    LogPrint(BCLog::NET, "sending %s (%d bytes) peer=%d\n",
             SanitizeString(msg.Command().c_str()), nPayloadLength, pnode->id);

    std::vector<uint8_t> serializedHeader;
    serializedHeader.reserve(CMessageHeader::HEADER_SIZE);
    CMessageHeader hdr(config->GetChainParams().NetMagic(),
                       msg.Command().c_str(), nPayloadLength);
    memcpy(hdr.pchChecksum, msg.Hash().begin(), CMessageHeader::CHECKSUM_SIZE);

    CVectorWriter{SER_NETWORK, INIT_PROTO_VERSION, serializedHeader, 0, hdr};

    size_t nBytesSent = 0;
    {
        LOCK(pnode->cs_vSend);
        bool optimisticSend(pnode->vSendMsg.empty());

        // log total amount of bytes per command
        pnode->mapSendBytesPerMsgCmd[msg.Command()] += nTotalSize;
        pnode->nSendSize += nTotalSize;

        if (pnode->nSendSize.getSendQueueBytes() > nSendBufferMaxSize) {
            pnode->fPauseSend = true;
        }
        pnode->vSendMsg.push_back(
            std::make_unique<CVectorStream>(std::move(serializedHeader)));
        if (nPayloadLength) {
            pnode->vSendMsg.push_back(msg.MoveData());
        }

        // If write queue empty, attempt "optimistic write"
        if (optimisticSend == true) {
            nBytesSent = SocketSendData(pnode);
        }
    }
    if (nBytesSent) {
        RecordBytesSent(nBytesSent);
    }
}

std::shared_ptr<CTxnValidator> CConnman::getTxnValidator() {
	return mTxnValidator;
}

/** Enqueue a new transaction for validation */
void CConnman::EnqueueTxnForValidator(std::shared_ptr<CTxInputData> pTxInputData) {
    mTxnValidator->newTransaction(std::move(pTxInputData));
}
/* Support for a vector */
void CConnman::EnqueueTxnForValidator(std::vector<TxInputDataSPtr> vTxInputData) {
    mTxnValidator->newTransaction(std::move(vTxInputData));
}

/** Resubmit a transaction for validation */
void CConnman::ResubmitTxnForValidator(TxInputDataSPtr pTxInputData) {
    mTxnValidator->resubmitTransaction(std::move(pTxInputData));
}

/** Check if the txn is already known */
bool CConnman::CheckTxnExistsInValidatorsQueue(const uint256& txid) const {
    return mTxnValidator->isTxnKnown(txid);
}

/* Find node by it's id */
CNodePtr CConnman::FindNodeById(int64_t nodeId) {
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if (pnode->id == nodeId) {
            return pnode;
        }
    }
    return nullptr;
}

/* Erase transaction from the given peer */
void CConnman::EraseOrphanTxnsFromPeer(NodeId peer) {
    mTxnValidator->getOrphanTxnsPtr()->eraseTxnsFromPeer(peer);
}

/* Erase transaction by it's hash */
int CConnman::EraseOrphanTxn(const uint256& hash) {
    return mTxnValidator->getOrphanTxnsPtr()->eraseTxn(hash);
}

/* Check if orphan transaction exists by prevout */
bool CConnman::CheckOrphanTxnExists(const COutPoint& prevout) const {
    return mTxnValidator->getOrphanTxnsPtr()->checkTxnExists(prevout);
}

/* Check if orphan transaction exists by txn hash */
bool CConnman::CheckOrphanTxnExists(const uint256& txHash) const {
    return mTxnValidator->getOrphanTxnsPtr()->checkTxnExists(txHash);
}

/* Get transaction's hash for orphan transactions (by prevout) */
std::vector<uint256> CConnman::GetOrphanTxnsHash(const COutPoint& prevout) const {
    return mTxnValidator->getOrphanTxnsPtr()->getTxnsHash(prevout);
}

/* Check if transaction exists in recent rejects */
bool CConnman::CheckTxnInRecentRejects(const uint256& txHash) const {
    return mTxnValidator->getTxnRecentRejectsPtr()->isRejected(txHash);
}

/* Reset recent rejects */
void CConnman::ResetRecentRejects() {
    mTxnValidator->getTxnRecentRejectsPtr()->reset();
}

/* Get extra txns for block reconstruction */
std::vector<std::pair<uint256, CTransactionRef>>
CConnman::GetCompactExtraTxns() const {
    return mTxnValidator->getOrphanTxnsPtr()->getCompactExtraTxns();
}

/** Enqueue a new transaction for later sending to our peers */
void CConnman::EnqueueTransaction(const CTxnSendingDetails& txn)
{
    mTxnPropagator->newTransaction(txn);
}

/** Remove some transactions from our peers list of new transactions */
void CConnman::DequeueTransactions(const std::vector<CTransactionRef>& txns)
{
    mTxnPropagator->removeTransactions(txns);
}

bool CConnman::ForNode(NodeId id, std::function<bool(const CNodePtr& pnode)> func) {
    CNodePtr found {nullptr};
    LOCK(cs_vNodes);
    for (const CNodePtr& pnode : vNodes) {
        if (pnode->id == id) {
            found = pnode;
            break;
        }
    }
    return found && NodeFullyConnected(found) && func(found);
}

int64_t PoissonNextSend(int64_t nNow, int average_interval_seconds) {
    return nNow + int64_t(log1p(GetRand(1ULL << 48) *
                                -0.0000000000000035527136788 /* -1/2^48 */) *
                              average_interval_seconds * -1000000.0 +
                          0.5);
}

CSipHasher CConnman::GetDeterministicRandomizer(uint64_t id) const {
    return CSipHasher(nSeed0, nSeed1).Write(id);
}

uint64_t CConnman::CalculateKeyedNetGroup(const CAddress &ad) const {
    std::vector<uint8_t> vchNetGroup(ad.GetGroup());

    return GetDeterministicRandomizer(RANDOMIZER_ID_NETGROUP)
        .Write(&vchNetGroup[0], vchNetGroup.size())
        .Finalize();
}

std::string userAgent() {
    std::vector<std::string> uacomments;

    // sanitize comments per BIP-0014, format user agent and check total size
    if (gArgs.IsArgSet("-uacomment")) {
        for (const std::string &cmt : gArgs.GetArgs("-uacomment")) {
            if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT)) {
                LogPrintf(
                    "User Agent comment (%s) contains unsafe characters. "
                    "We are going to use a sanitize version of the comment.\n",
                    cmt);
            }
            uacomments.push_back(cmt);
        }
    }

    std::string subversion =
        FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (subversion.size() > MAX_SUBVERSION_LENGTH) {
        LogPrintf("Total length of network version string (%i) exceeds maximum "
                  "length (%i). Reduce the number or size of uacomments. "
                  "String has been resized to the max length allowed.\n",
                  subversion.size(), MAX_SUBVERSION_LENGTH);
        subversion.resize(MAX_SUBVERSION_LENGTH - 2);
        subversion.append(")/");
        LogPrintf("Current network string has been set to: %s\n", subversion);
    }

    return subversion;
}
