// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <double_spend/dscallback_msg.h>
#include <double_spend/dstxn_serialiser.h>
#include <double_spend/time_limited_blacklist.h>
#include <invalid_txn_publisher.h>
#include <leaky_bucket.h>
#include <limitedmap.h>
#include <primitives/transaction.h>
#include <threadpool.h>

#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>

class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)

/**
 * Class to handle double-spend attempts.
 *
 * Contains a thread pool for background processing, to which it queues
 * tasks for processing double-spend notifications.
 *
 * Asynchronous processing of the tasks is desired because each involves
 * potentially slow communication with a remote endpoint.
 */
class DSAttemptHandler final
{
  public:

    // Notification levels
    enum class NotificationLevel : int
    {
        NONE = 0,
        STANDARD,
        ALL
    };

    // Default number of double-spent transactions we remember before evicting the oldest
    static constexpr size_t DEFAULT_TXN_REMEMBER_COUNT { 1000 };
    // Default maximum size of double-spend endpoint blacklist
    static constexpr size_t DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE { 1000 };
    // Default number of threads we reserve for processing double-spend notifications
    static constexpr size_t DEFAULT_NUM_FAST_THREADS { 2 };
    static constexpr size_t DEFAULT_NUM_SLOW_THREADS { 2 };
    // Default number of timeouts / hour before assuming an endpoint is slow
    static constexpr size_t DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR { 3 };
    // Default maximum number of endpoint IPs we will notify per transaction
    static constexpr size_t DEFAULT_DS_ENDPOINT_MAX_COUNT { 3 };

    // Maximum number of threads for each of the slow/fast submission queues
    static constexpr size_t MAX_NUM_THREADS { 64 };
    // Default submit queue size limit in MB
    static constexpr size_t DEFAULT_MAX_SUBMIT_MEMORY { 4096 };
    // Default notification level
    static constexpr NotificationLevel DEFAULT_NOTIFY_LEVEL { NotificationLevel::STANDARD };

    DSAttemptHandler(const Config& config);
    ~DSAttemptHandler() = default;

    // Copying & moving are unnecessary
    DSAttemptHandler(const DSAttemptHandler&) = delete;
    DSAttemptHandler(DSAttemptHandler&&) = delete;
    DSAttemptHandler& operator=(const DSAttemptHandler&) = delete;
    DSAttemptHandler& operator=(DSAttemptHandler&&) = delete;

    // Submit a newly detected double-spend for processing
    void HandleDoubleSpend(const InvalidTxnPublisher::InvalidTxnInfoWithTxn& txnInfo);

  private:

    // Background processing function for newly detected double-spend
    void ProcessDoubleSpend();

    // Wrapper type for input script details
    struct ScriptDetails
    {
        CScript scriptPubKey {};
        Amount amount {};
        int32_t coinHeight {0};
        int32_t spendHeight {0};
        bool isStandard {false};
    };

    // Fetch script details for the given transaction input
    ScriptDetails GetScriptDetails(const CTransactionRef& doubleSpend, size_t doubleSpendInput) const;

    // Wrapper type for conflicting inputs we need to notify about
    struct NotificationDetails
    {
        CTransactionRef dsEnabledTxn {};
        size_t dsEnabledTxnInput {0};
        CTransactionRef conflictingTxn {};
        size_t conflictingTxnInput {0};
        size_t doubleSpendTxnInput {0};
        DSCallbackMsg callbackMsg {};
        ScriptDetails scriptDetails {};
    };

    // Check if either of the given transactions are notification enabled, and if so whether there are any
    // conflicting inputs we need to notify about.
    std::pair<bool, NotificationDetails> GetNotificationDetails(
        const CTransactionRef& mempoolTxn,
        const CTransactionRef& doubleSpendTxn,
        bool stdInputOnly) const;

    // Query the endpoint to see if they want a proof for a double-spent transaction
    bool SubmitQuery(
        const std::string& txid,
        const std::string& endpointAddrStr,
        int httpTimeout,
        unsigned protocolVer);

    // Deal with sending an HTTP notification to a double-spend endpoint
    void SendNotification(const NotificationDetails& notificationDetails);

    // Deal with resubmitting an HTTP notification to a double-spend endpoint via the slow queue
    void SendNotificationSlow(
        const std::string& endpointAddrStr,
        unsigned retryCount,
        const NotificationDetails& notificationDetails,
        const DSTxnSerialiser::TxnHandleSPtr& handle);

    // Query an endpoint and submit a proof if they say they want it
    bool QueryAndSubmitProof(
        const std::string& endpointAddrStr,
        const NotificationDetails& notificationDetails,
        const DSTxnSerialiser::TxnHandleSPtr& handle,
        int httpTimeout,
        bool& wantsProof,
        bool& retry);

    // Wrapper type for the double spend transaction details
    struct DoubleSpendTxnDetails
    {
        CTransactionRef doubleSpendTxn {};
        bool scriptsChecked {false};
        NodeId sender {};
    };

    // Fetch and validate the double-spend input script
    std::optional<bool> ValidateDoubleSpend(
        const DoubleSpendTxnDetails& doubleSpendTxnDetails,
        const NotificationDetails& notificationDetails,
        CValidationState& state) const;

    // Add an endpoint to the temporary blacklist
    void AddToBlacklist(const std::string& addr);

    // Update (or add) statistics for a slow endpoint
    void UpdateSlowEndpoint(const std::string& endpoint);
    // Check to see whether an endpoint is currently considered slow
    bool IsEndpointSlow(const std::string& endpoint) const;

    // Remember a txn we've already notified about
    void RecordNotifiedTxn(const TxId& txid);

    // Reference to the global config
    const Config& mConfig;

    // Mutex for protecting our internal data. Should NOT be held while communicating
    // with an endpoint which could be a long running operation.
    mutable std::mutex mMtx {};

    // Keep track of previously notified txns, upto a limit
    limitedmap<TxId, uint64_t> mTxnsNotified;
    uint64_t mTxnsNotifiedIndex {0};

    // A queue of double-spends we are waiting to submit to the DS authority.
    // We're using a list here because we may want to drop items from the middle
    // of the queue in future if it becomes full.
    using DoubleSpend = std::tuple<DoubleSpendTxnDetails, std::set<CTransactionRef>, uint64_t>;
    std::list<DoubleSpend> mSubmitQueue {};
    // Size (in bytes) of things in the submit queue
    uint64_t mSubmitQueueSize {0};

    // Track temporary suspension scores for peers that are sending us double-spends
    // we time-out validating.
    using SuspensionScore = LeakyBucket<std::chrono::seconds>;
    std::map<NodeId, SuspensionScore> mSuspensionTracker {};

    // Track slow endpoints
    using SlowEndpoint = LeakyBucket<std::chrono::minutes>;
    std::map<std::string, SlowEndpoint> mSlowEndpoints {};

    // Limited temporary blacklist of bad callback servers
    TimeLimitedBlacklist<std::string> mServerBlacklist;

    // Txn serialiser
    DSTxnSerialiser mTxnSerialiser {};

    // Incoming messages are queued for handling by thread pools.
    // Leave these as the last members of this class so that they are destroyed first.
    CThreadPool<CQueueAdaptor> mFastThreadPool;
    CThreadPool<CQueueAdaptor> mSlowThreadPool;

};

