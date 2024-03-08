// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
#define BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H

#include "validationinterface.h"
#include "zmq_publisher.h"

#include <list>
#include <map>

class CBlockIndex;
class CZMQAbstractNotifier;

struct ActiveZMQNotifier
{
    std::string notifierName;
    std::string notifierAddress;
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CZMQNotificationInterface final : public CValidationInterface {
public:
    virtual ~CZMQNotificationInterface();

    static CZMQNotificationInterface *Create();
    std::vector<ActiveZMQNotifier> ActiveZMQNotifiers();

    // Register/unregister validation interface
    void RegisterValidationInterface() override;
    void UnregisterValidationInterface() override;

protected:
    bool Initialize();
    void Shutdown();

    // CValidationInterface
    void TransactionAddedToMempool(const CTransactionRef &tx) override;
    void TransactionAdded(const CTransactionRef& tx) override;
    void TransactionRemovedFromMempool(const uint256& txid,
                                       MemPoolRemovalReason reason,
                                       const CTransactionConflict& conflictedWith) override;
    void TransactionRemovedFromMempoolBlock(const uint256& txid,
                                            MemPoolRemovalReason reason) override;
    void
    BlockConnected(const std::shared_ptr<const CBlock> &pblock,
                   const CBlockIndex *pindexConnected,
                   const std::vector<CTransactionRef> &vtxConflicted) override;
    void BlockConnected2(const CBlockIndex* pindexConnected,
                   const std::vector<CTransactionRef>& vtxNew) override;
    void
    BlockDisconnected(const std::shared_ptr<const CBlock> &pblock) override;
    void UpdatedBlockTip(const CBlockIndex *pindexNew,
                         const CBlockIndex *pindexFork,
                         bool fInitialDownload) override;

    void InvalidTxMessageZMQ(std::string_view message) override;

private:
    CZMQNotificationInterface();

    void *pcontext;
    std::list<CZMQAbstractNotifier *> notifiers;
    std::shared_ptr<CZMQPublisher> zmqPublisher;
    std::vector<boost::signals2::scoped_connection> slotConnections {};
};

#endif // BITCOIN_ZMQ_ZMQNOTIFICATIONINTERFACE_H
