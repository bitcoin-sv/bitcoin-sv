// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQABSTRACTNOTIFIER_H
#define BITCOIN_ZMQ_ZMQABSTRACTNOTIFIER_H

#include "zmqconfig.h"
#include "zmq_publisher.h"

#include "txmempool.h"

class CBlockIndex;
class CZMQAbstractNotifier;

typedef CZMQAbstractNotifier *(*CZMQNotifierFactory)();

class CZMQAbstractNotifier {
public:
    CZMQAbstractNotifier() : psocket(0) {}
    virtual ~CZMQAbstractNotifier();

    template <typename T> static CZMQAbstractNotifier *Create() {
        return new T();
    }

    std::string GetType() const { return type; }
    void SetType(const std::string &t) { type = t; }
    std::string GetAddress() const { return address; }
    void SetAddress(const std::string &a) { address = a; }

    virtual bool Initialize(void *pcontext, std::shared_ptr<CZMQPublisher>) = 0;
    virtual void Shutdown() = 0;

    virtual bool NotifyBlock(const CBlockIndex *pindex);
    virtual bool NotifyTransaction(const CTransaction &transaction);
    virtual bool NotifyTextMessage(const std::string& topic, std::string_view message);
    virtual bool NotifyRemovedFromMempool(const uint256& txid, const MemPoolRemovalReason reason,
                                          const CTransactionConflict& conflictedWith);
    virtual bool NotifyRemovedFromMempoolBlock(const uint256& txid, const MemPoolRemovalReason reason);

protected:
    void *psocket;
    std::string type;
    std::string address;
};

#endif // BITCOIN_ZMQ_ZMQABSTRACTNOTIFIER_H
