// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zmqnotificationinterface.h"
#include "zmqpublishnotifier.h"
#include "zmq_publisher.h"
#include "streams.h"
#include "util.h"
#include "validation.h"

void zmqError(const char *str) {
    LogPrint(BCLog::ZMQ, "zmq: Error: %s, errno=%s\n", str,
             zmq_strerror(errno));
}

CZMQNotificationInterface::CZMQNotificationInterface() :
    pcontext(nullptr),
    zmqPublisher(std::make_shared<CZMQPublisher>())
{}

CZMQNotificationInterface::~CZMQNotificationInterface() {
    Shutdown();

    for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin();
         i != notifiers.end(); ++i) {
        delete *i;
    }
}

void CZMQNotificationInterface::RegisterValidationInterface()
{   
    CMainSignals& sigs { GetMainSignals() };

    using namespace boost::placeholders;
    slotConnections.push_back(sigs.TransactionAddedToMempool.connect(boost::bind(&CZMQNotificationInterface::TransactionAddedToMempool, this, _1)));
    slotConnections.push_back(sigs.TransactionAddedToMempool.connect(boost::bind(&CZMQNotificationInterface::TransactionAdded, this, _1)));
    slotConnections.push_back(sigs.TransactionRemovedFromMempool.connect(boost::bind(&CZMQNotificationInterface::TransactionRemovedFromMempool, this, _1, _2, _3)));
    slotConnections.push_back(sigs.TransactionRemovedFromMempoolBlock.connect(boost::bind(&CZMQNotificationInterface::TransactionRemovedFromMempoolBlock, this, _1, _2)));
    slotConnections.push_back(sigs.BlockConnected.connect(boost::bind(&CZMQNotificationInterface::BlockConnected, this, _1, _2, _3)));
    slotConnections.push_back(sigs.BlockConnected2.connect(boost::bind(&CZMQNotificationInterface::BlockConnected2, this, _1, _2)));
    slotConnections.push_back(sigs.BlockDisconnected.connect(boost::bind(&CZMQNotificationInterface::BlockDisconnected, this, _1)));
    slotConnections.push_back(sigs.UpdatedBlockTip.connect(boost::bind(&CZMQNotificationInterface::UpdatedBlockTip, this, _1, _2, _3)));
    slotConnections.push_back(sigs.InvalidTxMessageZMQ.connect(boost::bind(&CZMQNotificationInterface::InvalidTxMessageZMQ, this, _1)));
}

void CZMQNotificationInterface::UnregisterValidationInterface()
{   
    slotConnections.clear();
}

CZMQNotificationInterface *CZMQNotificationInterface::Create() {
    CZMQNotificationInterface *notificationInterface = nullptr;
    std::map<std::string, CZMQNotifierFactory> factories;
    std::list<CZMQAbstractNotifier *> notifiers;

    factories["pubhashblock"] =
        CZMQAbstractNotifier::Create<CZMQPublishHashBlockNotifier>;
    factories["pubhashtx"] =
        CZMQAbstractNotifier::Create<CZMQPublishHashTransactionNotifier>;
    factories["pubrawblock"] =
        CZMQAbstractNotifier::Create<CZMQPublishRawBlockNotifier>;
    factories["pubrawtx"] =
        CZMQAbstractNotifier::Create<CZMQPublishRawTransactionNotifier>;
    factories["pubinvalidtx"] =
        CZMQAbstractNotifier::Create<CZMQPublishTextNotifier>;
    factories["pubdiscardedfrommempool"] =
        CZMQAbstractNotifier::Create<CZMQPublishRemovedFromMempoolNotifier>;
    factories["pubremovedfrommempoolblock"] =
        CZMQAbstractNotifier::Create<CZMQPublishRemovedFromMempoolBlockNotifier>;
    factories["pubhashblock2"] =
        CZMQAbstractNotifier::Create<CZMQPublishHashBlockNotifier2>;
    factories["pubrawblock2"] =
        CZMQAbstractNotifier::Create<CZMQPublishRawBlockNotifier2>;
    factories["pubhashtx2"] =
        CZMQAbstractNotifier::Create<CZMQPublishHashTransactionNotifier2>;
    factories["pubrawtx2"] =
        CZMQAbstractNotifier::Create<CZMQPublishRawTransactionNotifier2>;

    for (std::map<std::string, CZMQNotifierFactory>::const_iterator i =
             factories.begin();
         i != factories.end(); ++i) {
        std::string arg("-zmq" + i->first);
        if (gArgs.IsArgSet(arg)) {
            CZMQNotifierFactory factory = i->second;
            std::string address = gArgs.GetArg(arg, "");
            CZMQAbstractNotifier *notifier = factory();
            notifier->SetType(i->first);
            notifier->SetAddress(address);
            notifiers.push_back(notifier);
        }
    }

    if (!notifiers.empty()) {
        notificationInterface = new CZMQNotificationInterface();
        notificationInterface->notifiers = notifiers;

        if (!notificationInterface->Initialize()) {
            delete notificationInterface;
            notificationInterface = nullptr;
        }
    }

    return notificationInterface;
}

// Called at startup to conditionally set up ZMQ socket(s)
bool CZMQNotificationInterface::Initialize() {
    int major = 0, minor = 0, patch = 0;
    zmq_version(&major, &minor, &patch);
    LogPrint(BCLog::ZMQ, "zmq: version %d.%d.%d\n", major, minor, patch);

    LogPrint(BCLog::ZMQ, "zmq: Initialize notification interface\n");
    assert(!pcontext);

    pcontext = zmq_ctx_new();

    if (!pcontext) {
        zmqError("Unable to initialize context");
        return false;
    }

    std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin();
    for (; i != notifiers.end(); ++i) {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->Initialize(pcontext, zmqPublisher)) {
            LogPrint(BCLog::ZMQ, "  Notifier %s ready (address = %s)\n",
                     notifier->GetType(), notifier->GetAddress());
        } else {
            LogPrint(BCLog::ZMQ, "  Notifier %s failed (address = %s)\n",
                     notifier->GetType(), notifier->GetAddress());
            break;
        }
    }

    if (i != notifiers.end()) {
        return false;
    }

    return true;
}

// Called during shutdown sequence
void CZMQNotificationInterface::Shutdown() {
    LogPrint(BCLog::ZMQ, "zmq: Shutdown notification interface\n");
    if (pcontext) {
        for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin();
             i != notifiers.end(); ++i) {
            CZMQAbstractNotifier *notifier = *i;
            LogPrint(BCLog::ZMQ, "   Shutdown notifier %s at %s\n",
                     notifier->GetType(), notifier->GetAddress());
            notifier->Shutdown();
        }
        zmq_ctx_term(pcontext);

        pcontext = 0;
    }
    // stop publisher thread
    zmqPublisher = nullptr;
}

std::vector<ActiveZMQNotifier> CZMQNotificationInterface::ActiveZMQNotifiers()
{
    std::vector<ActiveZMQNotifier> arrNotifiers;

    for (auto& n : notifiers)
    {
        arrNotifiers.push_back({n->GetType(), n->GetAddress()});
    }

    return arrNotifiers;
}

void CZMQNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindexNew,
                                                const CBlockIndex *pindexFork,
                                                bool fInitialDownload) {
    // In IBD or blocks were disconnected without any new ones
    if (fInitialDownload || pindexNew == pindexFork) return;

    for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin();
         i != notifiers.end();) {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyBlock(pindexNew)) {
            ++i;
        } else {
            notifier->Shutdown();
            i = notifiers.erase(i);
            delete notifier;
        }
    }
}

void CZMQNotificationInterface::InvalidTxMessageZMQ(std::string_view message)
{
    for (auto i = notifiers.begin(); i != notifiers.end();) 
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyTextMessage("invalidtx", message)) 
        {
            ++i;
        } 
        else 
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
            delete notifier;
        }
    }
}

void CZMQNotificationInterface::TransactionAddedToMempool(
    const CTransactionRef &ptx) {
    // Used by BlockConnected and BlockDisconnected as well, because they're all
    // the same external callback.
    const CTransaction &tx = *ptx;

    for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin();
         i != notifiers.end();) {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyTransaction(tx)) {
            ++i;
        } else {
            notifier->Shutdown();
            i = notifiers.erase(i);
            delete notifier;
        }
    }
}


void CZMQNotificationInterface::TransactionRemovedFromMempool(const uint256& txid, MemPoolRemovalReason reason, 
                                                            const CTransactionConflict& conflictedWith)
{

    for (auto i = notifiers.begin(); i != notifiers.end();)
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyRemovedFromMempool(txid, reason, conflictedWith))
        {
            ++i;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
            delete notifier;
        }
    }
}

void CZMQNotificationInterface::TransactionRemovedFromMempoolBlock(const uint256& txid, MemPoolRemovalReason reason) {

    for (auto i = notifiers.begin(); i != notifiers.end();)
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyRemovedFromMempoolBlock(txid, reason))
        {
            ++i;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
            delete notifier;
        }
    }
}

void CZMQNotificationInterface::TransactionAdded(const CTransactionRef& ptx)
{
    // Used by BlockConnected2 and BlockDisconnected2 as well
    const CTransaction& tx = *ptx;

    for (auto i = notifiers.begin(); i != notifiers.end();) 
    {
        CZMQAbstractNotifier* notifier = *i;
        if (notifier->NotifyTransaction2(tx)) 
        {
            ++i;
        } 
        else 
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
            delete notifier;
        }
    }
}

void CZMQNotificationInterface::BlockConnected(
    const std::shared_ptr<const CBlock> &pblock,
    const CBlockIndex *pindexConnected,
    const std::vector<CTransactionRef> &vtxConflicted) {
    for (const CTransactionRef &ptx : pblock->vtx) {
        // Do a normal notify for each transaction added in the block
        TransactionAddedToMempool(ptx);
    }
}

// Notify for every connected block, even on re-org
// Only notify for transactions in vtxNew (that are not already in mempool)
void CZMQNotificationInterface::BlockConnected2(
    const CBlockIndex* pindexConnected,
    const std::vector<CTransactionRef>& vtxNew)
{
    if (IsInitialBlockDownload()) 
    {
        return;
    }

    for (const CTransactionRef& ptx : vtxNew) 
    {
        TransactionAdded(ptx);
    }

    for (auto i = notifiers.begin(); i != notifiers.end();) 
    {
        CZMQAbstractNotifier* notifier = *i;
        if (notifier->NotifyBlock2(pindexConnected)) 
        {
            ++i;
        } 
        else 
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
            delete notifier;
        }
    }
}


void CZMQNotificationInterface::BlockDisconnected(
    const std::shared_ptr<const CBlock> &pblock) {
    for (const CTransactionRef &ptx : pblock->vtx) {
        // Do a normal notify for each transaction removed in block
        // disconnection
        TransactionAddedToMempool(ptx);
    }
}
