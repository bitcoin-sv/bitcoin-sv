// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "zmqpublishnotifier.h"

#include "config.h"
#include "core_io.h"
#include "rpc/server.h"
#include "rpc/jsonwriter.h"
#include "rpc/text_writer.h"
#include "streams.h"
#include "util.h"
#include "validation.h"
#include "zmq_publisher.h"
#include <string>

static std::multimap<std::string, CZMQAbstractPublishNotifier *>
    mapPublishNotifiers;

static const char *MSG_HASHBLOCK = "hashblock";
static const char *MSG_HASHTX = "hashtx";
static const char *MSG_RAWBLOCK = "rawblock";
static const char *MSG_RAWTX = "rawtx";
/*
 * using slightly different topic prefix to avoid being subscribed to RemovedFromMempool and RemovedFromMempoolBlock
 * at same time.
*/
static const char *MSG_DISCARDEDFROMMEMPOOL = "discardedfrommempool";
static const char *MSG_REMOVEDFROMMEMPOOLBLOCK = "removedfrommempoolblock";


bool CZMQAbstractPublishNotifier::Initialize(void *pcontext, std::shared_ptr<CZMQPublisher> tspublisher) {
    assert(!psocket);
    zmqPublisher = tspublisher;

    // check if address is being used by other publish notifier
    std::multimap<std::string, CZMQAbstractPublishNotifier *>::iterator i =
        mapPublishNotifiers.find(address);

    if (i == mapPublishNotifiers.end()) {
        psocket = zmq_socket(pcontext, ZMQ_PUB);
        if (!psocket) {
            zmqError("Failed to create socket");
            return false;
        }

        int rc = zmq_bind(psocket, address.c_str());
        if (rc != 0) {
            zmqError("Failed to bind address");
            zmq_close(psocket);
            return false;
        }

        // register this notifier for the address, so it can be reused for other
        // publish notifier
        mapPublishNotifiers.insert(std::make_pair(address, this));
        return true;
    } else {
        LogPrint(BCLog::ZMQ, "zmq: Reusing socket for address %s\n", address);

        psocket = i->second->psocket;
        mapPublishNotifiers.insert(std::make_pair(address, this));

        return true;
    }
}

void CZMQAbstractPublishNotifier::Shutdown() {
    int count = mapPublishNotifiers.count(address);

    // remove this notifier from the list of publishers using this address
    typedef std::multimap<std::string, CZMQAbstractPublishNotifier *>::iterator
        iterator;
    std::pair<iterator, iterator> iterpair =
        mapPublishNotifiers.equal_range(address);

    for (iterator it = iterpair.first; it != iterpair.second; ++it) {
        if (it->second == this) {
            mapPublishNotifiers.erase(it);
            break;
        }
    }

    // release reference to threadSafePublisher
    zmqPublisher = nullptr;

    if (count == 1 && psocket) {
        LogPrint(BCLog::ZMQ, "Close socket at address %s\n", address);
        int linger = 0;
        zmq_setsockopt(psocket, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_close(psocket);
    }

    psocket = 0;
}

bool CZMQAbstractPublishNotifier::SendZMQMessage(const char *command,
                                              const void *data, size_t size) {
    assert(psocket);
    assert(zmqPublisher);

    /* SendZMQMessage can be called by multiple threads. Increment memory only sequence number here to ensure its uniqueness in sent messages */
    uint32_t sequence = nSequence++;

    bool rc = zmqPublisher->SendZMQMessage(psocket, command, data, size, sequence);
    if (rc == false) return false;

    return true;
}

bool CZMQPublishHashBlockNotifier::NotifyBlock(const CBlockIndex *pindex) {
    uint256 hash = pindex->GetBlockHash();
    LogPrint(BCLog::ZMQ, "zmq: Publish hashblock %s\n", hash.GetHex());
    char data[32];
    for (unsigned int i = 0; i < 32; i++)
        data[31 - i] = hash.begin()[i];
    return SendZMQMessage(MSG_HASHBLOCK, data, 32);
}

bool CZMQPublishHashTransactionNotifier::NotifyTransaction(
    const CTransaction &transaction) {
    uint256 txid = transaction.GetId();
    LogPrint(BCLog::ZMQ, "zmq: Publish hashtx %s\n", txid.GetHex());
    char data[32];
    for (unsigned int i = 0; i < 32; i++)
        data[31 - i] = txid.begin()[i];
    return SendZMQMessage(MSG_HASHTX, data, 32);
}

bool CZMQPublishRemovedFromMempoolNotifier::NotifyRemovedFromMempool(const uint256& txid,
                                                                     MemPoolRemovalReason reason,
                                                                     const CTransactionConflict& conflictedWith)
{

    CStringWriter tw;
    CJSONWriter jw(tw, false);

    jw.writeBeginObject();
    jw.pushKV("txid", txid.GetHex());

    switch (reason)
    {
        case MemPoolRemovalReason::EXPIRY:
            jw.pushKV("reason", "expired");
            break;
        case MemPoolRemovalReason::SIZELIMIT:
            jw.pushKV("reason", "mempool-sizelimit-exceeded");
            break;
        case MemPoolRemovalReason::CONFLICT:
            if (conflictedWith)
            {
                const auto& conflictedTransaction = conflictedWith.value().conflictedWith;
                const auto& blockhash = conflictedWith.value().blockhash;
                jw.pushKV("reason", "collision-in-block-tx");
                jw.writeBeginObject("collidedWith");
                jw.pushKV("txid", conflictedTransaction->GetId().GetHex());
                jw.pushKV("size", int64_t(conflictedTransaction->GetTotalSize()));
                jw.pushK("hex");
                jw.pushQuote();
                EncodeHexTx(*conflictedTransaction, jw.getWriter(), 0);
                jw.pushQuote();
                // push hash of block in which transaction we "collided with" arrived.
                if (blockhash != nullptr)
                {  
                    jw.writeEndObject();
                    jw.pushKV("blockhash", blockhash->GetHex());
                }
                else
                {
                    jw.writeEndObject();
                } 
            }
            else
            {
                jw.pushKV("reason", "collision-in-block-tx");
            }
            break;
        default:
            jw.pushKV("reason", "unknown-reason");
    }

    jw.writeEndObject();

    std::string message = tw.MoveOutString();

    return SendZMQMessage(MSG_DISCARDEDFROMMEMPOOL, message.data(), message.size());
}

bool CZMQPublishRemovedFromMempoolBlockNotifier::NotifyRemovedFromMempoolBlock(const uint256& txid,
                                                                               MemPoolRemovalReason reason)
{

    CStringWriter tw;
    CJSONWriter jw(tw, false);

    jw.writeBeginObject();

    switch (reason)
    {
        case MemPoolRemovalReason::REORG:
            jw.pushKV("reason", "reorg");
            break;
        case MemPoolRemovalReason::BLOCK:
            jw.pushKV("reason", "included-in-block");
            break;
        default:
            jw.pushKV("reason", "unknown-reason");
    }
    
    jw.pushKV("txid", txid.GetHex());
    jw.writeEndObject();

    std::string message = tw.MoveOutString();

    return SendZMQMessage(MSG_REMOVEDFROMMEMPOOLBLOCK, message.data(), message.size());
}

bool CZMQPublishRawBlockNotifier::NotifyBlock(const CBlockIndex *pindex) {
    LogPrint(BCLog::ZMQ, "zmq: Publish rawblock %s\n",
             pindex->GetBlockHash().GetHex());

    const Config &config = GlobalConfig::GetConfig();
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    {
        LOCK(cs_main);
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, config)) {
            zmqError("Can't read block from disk");
            return false;
        }

        ss << block;
    }

    return SendZMQMessage(MSG_RAWBLOCK, &(*ss.begin()), ss.size());
}

bool CZMQPublishRawTransactionNotifier::NotifyTransaction(
    const CTransaction &transaction) {
    uint256 txid = transaction.GetId();
    LogPrint(BCLog::ZMQ, "zmq: Publish rawtx %s\n", txid.GetHex());
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    ss << transaction;
    return SendZMQMessage(MSG_RAWTX, &(*ss.begin()), ss.size());
}

bool CZMQPublishTextNotifier::NotifyTextMessage(const std::string& topic, std::string_view message)
{
    LogPrint(BCLog::ZMQ, "zmq: Publish text with topic: %s\n", topic.c_str());
    return SendZMQMessage(topic.c_str(), message.data(), message.size());
}
