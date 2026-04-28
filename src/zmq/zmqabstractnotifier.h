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

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CZMQAbstractNotifier
{
public:
    CZMQAbstractNotifier() = default;

    CZMQAbstractNotifier(const CZMQAbstractNotifier&) = delete;
    CZMQAbstractNotifier& operator=(const CZMQAbstractNotifier&) = delete;

    virtual ~CZMQAbstractNotifier();

    template<typename T>
    static CZMQAbstractNotifier* Create()
    {
        return new T(); // NOLINT(cppcoreguidelines-owning-memory)
    }

    const std::string& GetType() const { return type; }
    void SetType(std::string t) { type = std::move(t); }

    const std::string& GetAddress() const { return address; }
    void SetAddress(std::string a) { address = std::move(a); }

    virtual bool Initialize(void* pcontext, std::shared_ptr<CZMQPublisher>) = 0;
    virtual void Shutdown() = 0;

    virtual bool NotifyBlock(const CBlockIndex*);
    virtual bool NotifyBlock2(const CBlockIndex*);
    virtual bool NotifyTransaction(const CTransaction&);
    virtual bool NotifyTransaction2(const CTransaction&);
    virtual bool NotifyTextMessage(const std::string& topic, std::string_view message);
    virtual bool NotifyRemovedFromMempool(const uint256& txid,
                                          const MemPoolRemovalReason,
                                          const CTransactionConflict&);
    virtual bool NotifyRemovedFromMempoolBlock(const uint256& txid,
                                               const MemPoolRemovalReason);

protected:
    void* psocket{};
    std::string type;
    std::string address;
};

#endif // BITCOIN_ZMQ_ZMQABSTRACTNOTIFIER_H
