// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ZMQ_ZMQPUBLISHNOTIFIER_H
#define BITCOIN_ZMQ_ZMQPUBLISHNOTIFIER_H

#include "zmqabstractnotifier.h"

class CBlockIndex;

class CZMQAbstractPublishNotifier : public CZMQAbstractNotifier {
private:
    //!< upcounting per message sequence number
    std::atomic<uint32_t> nSequence{0};

    std::shared_ptr<CZMQPublisher> zmqPublisher;

public:
    /* send zmq multipart message
       parts:
          * command
          * data
          * message sequence number
    */
    bool SendZMQMessage(const char *command, const void *data, size_t size);

    bool Initialize(void *pcontext, std::shared_ptr<CZMQPublisher>) override;
    void Shutdown() override;
};

class CZMQPublishHashBlockNotifier : public CZMQAbstractPublishNotifier {
public:
    bool NotifyBlock(const CBlockIndex *pindex) override;
};

class CZMQPublishHashTransactionNotifier : public CZMQAbstractPublishNotifier {
public:
    bool NotifyTransaction(const CTransaction &transaction) override;
};

class CZMQPublishRemovedFromMempoolNotifier : public CZMQAbstractPublishNotifier
{
public:
    bool NotifyRemovedFromMempool(const uint256& txid, const MemPoolRemovalReason reason,
                                  const CTransactionConflict& conflictedWith) override;
};

class CZMQPublishRemovedFromMempoolBlockNotifier : public CZMQAbstractPublishNotifier
{
public:
    bool NotifyRemovedFromMempoolBlock(const uint256& txid, const MemPoolRemovalReason reason) override;
};

class CZMQPublishRawBlockNotifier : public CZMQAbstractPublishNotifier {
public:
    bool NotifyBlock(const CBlockIndex *pindex) override;
};

class CZMQPublishRawTransactionNotifier : public CZMQAbstractPublishNotifier {
public:
    bool NotifyTransaction(const CTransaction &transaction) override;
};

class CZMQPublishTextNotifier : public CZMQAbstractPublishNotifier {
public:
    bool NotifyTextMessage(const std::string& topic, std::string_view message) override;
};

#endif // BITCOIN_ZMQ_ZMQPUBLISHNOTIFIER_H
