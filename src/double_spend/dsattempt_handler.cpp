// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <double_spend/dsattempt_handler.h>

#include <coins.h>
#include <config.h>
#include <logging.h>
#include <net/net_processing.h>
#include <rpc/client.h>
#include <rpc/http_protocol.h>
#include <rpc/http_request.h>
#include <rpc/http_response.h>
#include <txdb.h>
#include <validation.h>

namespace
{
    // Score at which we suspend processing non-standard transactions from a peer
    constexpr size_t SUSPENSION_SCORE_MAX { 100 };
    // Amount to increase suspension score by each time
    constexpr size_t SUSPENSION_SCORE_INCREASE { 10 };
    // How long (seconds) to suspend processing for from a peer if they exceed the suspension limit
    constexpr size_t SUSPENSION_DURATION { 60 * 10 };

    // How long (seconds) a bad endpoint gets placed on the blacklist for
    constexpr size_t ENDPOINT_BLACKLIST_DURATION { 60 * 60 };

    // Latest version of the double-spend notification protocol we support
    constexpr unsigned SUPPORTED_PROTOCOL_VERSION { 1 };

    // HTTP header field all responses should contain
    const char* DSNT_HTTP_HEADER { "x-bsv-dsnt" };

    // Maximum number of times we will try to submit a proof before giving up
    const unsigned MAX_PROOF_SUBMIT_ATTEMPTS {2};

    // Helper to determine number of threads to run
    size_t GetNumThreads(const Config& config, bool fast)
    {
        if(config.GetDoubleSpendNotificationLevel() == DSAttemptHandler::NotificationLevel::NONE)
        {
            // If we're not processing double-spends at all there's no point launching any threads
            return 0;
        }
        return (fast? config.GetDoubleSpendNumFastThreads() : config.GetDoubleSpendNumSlowThreads());
    }

    // Wrapper for HTTP response status details
    struct ResponseStatus
    {
        bool ok {false};
        bool wantsProof {false};
        int status {-1};
    };

    // Parse an HTTP response
    ResponseStatus GetHTTPStatusAndWantsProof(const rpc::client::HTTPResponse& response, const std::string& endpoint)
    {
        int status { response.GetStatus() };
        LogPrint(BCLog::DOUBLESPEND, "Got %d response from endpoint %s\n", status, endpoint);

        // Does endpoint want proof?
        const auto& headers { response.GetHeaders() };
        const auto& dsntheader { headers.find(DSNT_HTTP_HEADER) };
        if(dsntheader == headers.end())
        {
            LogPrint(BCLog::DOUBLESPEND, "Missing %s header in response from endpoint %s\n", DSNT_HTTP_HEADER, endpoint);
            return { false, false, status };
        }
        bool wantsProof { boost::lexical_cast<bool>(dsntheader->second) };

        return { true, wantsProof, status };
    }
}


DSAttemptHandler::DSAttemptHandler(const Config& config)
: mConfig { config },
  mTxnsNotified { config.GetDoubleSpendTxnRemember() },
  mServerBlacklist { config.GetDoubleSpendEndpointBlacklistSize() },
  mFastThreadPool { true, "DSAttemptHandlerFast", GetNumThreads(config, true) },
  mSlowThreadPool { true, "DSAttemptHandlerSlow", GetNumThreads(config, false) }
{
}

// Submit a newly detected double-spend for processing
void DSAttemptHandler::HandleDoubleSpend(const InvalidTxnPublisher::InvalidTxnInfoWithTxn& txnInfo)
{
    // Are we processing double-spends?
    if(mConfig.GetDoubleSpendNotificationLevel() == DSAttemptHandler::NotificationLevel::NONE)
    {
        return;
    }

    const CTransactionRef& doubleSpendTxn { txnInfo.GetTransaction() };
    const std::set<CTransactionRef>& conflictedTxns { txnInfo.GetCollidedWithTransactions() };

    // Sanity check we have at least 1 txn in double-spend set
    if(conflictedTxns.empty())
    {
        LogPrint(BCLog::DOUBLESPEND, "Double-spend notification set is empty, ignoring\n");
        return;
    }

    // Check we know the sender of the double-spend
    auto pSender { std::get_if<InvalidTxnInfo::TxDetails>(&txnInfo.GetDetails()) };
    if(!pSender)
    {
        LogPrint(BCLog::DOUBLESPEND, "Double-spend notification doesn't have sender details, ignoring\n");
        return;
    }

    // Calculate memory usage for storing this new transaction list
    uint64_t txnsMemUsage { std::accumulate(conflictedTxns.begin(), conflictedTxns.end(), RecursiveDynamicUsage(doubleSpendTxn),
        [](uint64_t tot, const CTransactionRef& txn)
        {
            return tot += RecursiveDynamicUsage(txn);
        }
    ) };

    // Add to newly detected double-spend queue for asynchronous processing
    {
        std::lock_guard lock { mMtx };

        // Queue size already over limit?
        if(mSubmitQueueSize + txnsMemUsage > mConfig.GetDoubleSpendQueueMaxMemory())
        {
            LogPrint(BCLog::DOUBLESPEND,
                "Dropping new double-spend because the queue is full (current queue size %llu, new txns size %llu)\n",
                mSubmitQueueSize, txnsMemUsage);
            return;
        }

        // Add to queue
        mSubmitQueue.push_back(std::make_tuple(
            DoubleSpendTxnDetails { doubleSpendTxn, txnInfo.GetValidationState().ScriptsChecked(), pSender->nodeId },
            conflictedTxns,
            txnsMemUsage));
        mSubmitQueueSize += txnsMemUsage;
    }

    // Create task to asynchronously process queued double-spend
    make_task(mFastThreadPool, [this] { ProcessDoubleSpend(); });
}

// Fetch and validate the double-spend input script
std::optional<bool> DSAttemptHandler::ValidateDoubleSpend(
    const DoubleSpendTxnDetails& doubleSpendTxnDetails,
    const NotificationDetails& notificationDetails,
    CValidationState& state) const
{
    // Short-circuit if scripts already validated as part of PTV
    if(doubleSpendTxnDetails.scriptsChecked)
    {
        return true;
    }

    // Get script verification flags
    uint32_t scriptVerifyFlags { GetScriptVerifyFlags(mConfig, IsGenesisEnabled(mConfig, chainActive.Height() + 1)) };

    // Set verification timeout to the longest we'll allow
    task::CCancellationToken token { task::CTimedCancellationSource::Make(mConfig.GetMaxNonStdTxnValidationDuration()) };

    // Do script verification
    const ScriptDetails& scriptdetails { notificationDetails.scriptDetails };
    const CTransactionRef& doubleSpend { doubleSpendTxnDetails.doubleSpendTxn };
    PrecomputedTransactionData txdata { *doubleSpend };

    unsigned int input { static_cast<unsigned int>(notificationDetails.doubleSpendTxnInput) };
    LogPrint(BCLog::DOUBLESPEND, "Verifying script for txn %s, input %d\n", doubleSpend->GetId().ToString(), input);

    return CheckInputScripts(token, mConfig, false, scriptdetails.scriptPubKey, scriptdetails.amount,
        *doubleSpend, state, input, scriptdetails.coinHeight, scriptdetails.spendHeight, scriptVerifyFlags,
        false, txdata, nullptr);
}

// Check if either of the given transactions are notification enabled, and if so whether there are any
// conflicting inputs we need to notify about.
std::pair<bool, DSAttemptHandler::NotificationDetails> DSAttemptHandler::GetNotificationDetails(
    const CTransactionRef& mempoolTxn,
    const CTransactionRef& doubleSpendTxn,
    bool stdInputOnly) const
{
    // Are either txn notification enabled?
    auto [ mempoolTxnEnabled, mempoolTxnOutput ] { TxnHasDSNotificationOutput(*mempoolTxn) };
    auto [ doubleSpendTxnEnabled, doubleSpendTxnOutput ] { TxnHasDSNotificationOutput(*doubleSpendTxn) };
    CTransactionRef dsEnabledTxn {};
    CTransactionRef conflictingTxn {};
    size_t dsEnabledOutput {0};
    if(mempoolTxnEnabled)
    {
        dsEnabledTxn = mempoolTxn;
        dsEnabledOutput = mempoolTxnOutput;
        conflictingTxn = doubleSpendTxn;
    }
    else if(doubleSpendTxnEnabled)
    {
        dsEnabledTxn = doubleSpendTxn;
        dsEnabledOutput = doubleSpendTxnOutput;
        conflictingTxn = mempoolTxn;
    }
    else
    {
        return { false, {} };
    }
    LogPrint(BCLog::DOUBLESPEND, "Txn %s is DS notification enabled on output %d\n", dsEnabledTxn->GetId().ToString(), dsEnabledOutput);

    // Get DSCallbackMsg and check the version
    DSCallbackMsg callbackMsg { dsEnabledTxn->vout[dsEnabledOutput].scriptPubKey };
    if(callbackMsg.GetProtocolVersion() > SUPPORTED_PROTOCOL_VERSION)
    {
        LogPrint(BCLog::DOUBLESPEND, "Unsupported double-spend notification protocol version %d; ignoring\n",
            callbackMsg.GetProtocolVersion());
        return { false, {} };
    }

    // Which inputs from conflicted transaction do we need to check?
    std::vector<uint32_t> inputsToCheck { callbackMsg.GetInputs() };
    if(inputsToCheck.empty())
    {
        // Input count of 0 means to check them all
        for(uint32_t i = 0; i < dsEnabledTxn->vin.size(); ++i)
        {
            inputsToCheck.push_back(i);
        }
    }

    // Pick 1 conflicting input registered for notification
    for(uint32_t input : inputsToCheck)
    {
        // Sanity check input range
        if(input < dsEnabledTxn->vin.size())
        {
            for(uint32_t conflictingInput = 0; conflictingInput < conflictingTxn->vin.size(); ++conflictingInput)
            {
                if(conflictingTxn->vin[conflictingInput].prevout == dsEnabledTxn->vin[input].prevout)
                {
                    // Get script details and check whether we are configured to validate scripts of this type
                    uint32_t doubleSpendTxnInput { (doubleSpendTxn == dsEnabledTxn)? input : conflictingInput};
                    try
                    {
                        ScriptDetails scriptDetails { GetScriptDetails(doubleSpendTxn, doubleSpendTxnInput) };
                        if(stdInputOnly)
                        {
                            // We'll only validate standard input scripts
                            if(scriptDetails.isStandard)
                            {
                                return { true, { dsEnabledTxn, input, conflictingTxn, conflictingInput, doubleSpendTxnInput, callbackMsg, scriptDetails } };
                            }
                            else
                            {
                                LogPrint(BCLog::DOUBLESPEND, "Ignoring txn %s conflicting input %d because it is non-standard\n",
                                    doubleSpendTxn->GetId().ToString(), doubleSpendTxnInput);
                            }
                        }
                        else
                        {
                            // We'll validate any script, so this is a suitable input
                            return { true, { dsEnabledTxn, input, conflictingTxn, conflictingInput, doubleSpendTxnInput, callbackMsg, scriptDetails } };
                        }
                    }
                    catch(std::exception& e)
                    {
                        LogPrint(BCLog::DOUBLESPEND, "Error fetching script details for txn %s input %d: %s\n",
                            doubleSpendTxn->GetId().ToString(), doubleSpendTxnInput, e.what());
                    }
                }
            }
        }
    }

    // No suitable conflicting input found
    return { false, {} };
}

// Fetch script details for the given transaction input
DSAttemptHandler::ScriptDetails DSAttemptHandler::GetScriptDetails(
    const CTransactionRef& doubleSpend,
    size_t doubleSpendInput) const
{
    const COutPoint& prevout = { doubleSpend->vin[doubleSpendInput].prevout };

    CScript scriptPubKey {};
    Amount amount {};
    int32_t coinHeight {0};
    int32_t spendHeight {0};
    {
        LOCK(cs_main);
        CoinsDBView tipView { *pcoinsTip };
        CCoinsViewMemPool viewMemPool { tipView, mempool };
        CCoinsViewCache view { viewMemPool };

        const auto& coin { view.GetCoinWithScript(prevout) };
        if(coin)
        {
            scriptPubKey = coin->GetTxOut().scriptPubKey;
            amount = coin->GetTxOut().nValue;
            coinHeight = GetInputScriptBlockHeight(coin->GetHeight());
            spendHeight = GetSpendHeightAndMTP(view).first;
        }
        else
        {
            throw std::runtime_error("Failed to lookup coin & script for " + prevout.ToString());
        }
    }

    txnouttype outType {};
    bool isStandard { IsStandard(mConfig, scriptPubKey, coinHeight, outType) };

    return { std::move(scriptPubKey), amount, coinHeight, spendHeight, isStandard };
}

// Add an endpoint to the temporary blacklist
void DSAttemptHandler::AddToBlacklist(const std::string& addr)
{
    const auto& blacklistUntil { std::chrono::system_clock::now() + std::chrono::seconds {ENDPOINT_BLACKLIST_DURATION} };
    mServerBlacklist.Add(addr, blacklistUntil);
}

// Background processing function for newly detected double-spend
void DSAttemptHandler::ProcessDoubleSpend()
{
    try
    {
        // Pop first double-spend from queue
        DoubleSpend doubleSpendDetails {};
        {
            std::lock_guard lock { mMtx };
            if(mSubmitQueue.empty())
            {
                return;
            }

            // Pop double-spend details
            doubleSpendDetails = std::move(mSubmitQueue.front());
            mSubmitQueue.pop_front();

            // And reduce tracked memory usage
            uint64_t itemSize { std::get<2>(doubleSpendDetails) };
            if(mSubmitQueueSize >= itemSize)
            {
                mSubmitQueueSize -= itemSize;
            }
            else
            {
                // Something's gone wrong with our tracking, but ensure we never go -ve
                LogPrint(BCLog::DOUBLESPEND,
                    "Warning: DSAttemptHandler submit queue was about to go negative. Queue size %llu, item size %llu\n",
                    mSubmitQueueSize, itemSize);
                mSubmitQueueSize = 0;
            }
        }

        const DoubleSpendTxnDetails& doubleSpendTxnDetails { std::get<0>(doubleSpendDetails) };
        const CTransactionRef& doubleSpendTxn { doubleSpendTxnDetails.doubleSpendTxn };
        const std::set<CTransactionRef>& conflictedTxns { std::get<1>(doubleSpendDetails) };

        LogPrint(BCLog::DOUBLESPEND, "Processing double-spend txn %s (checked %d) from peer=%d\n",
            doubleSpendTxn->GetId().ToString(), doubleSpendTxnDetails.scriptsChecked, doubleSpendTxnDetails.sender);

        // What level of validation are we prepared to perform for double-spends?
        bool stdValidationOnly { mConfig.GetDoubleSpendNotificationLevel() == NotificationLevel::STANDARD };
        if(!stdValidationOnly)
        {
            // Check for temporary non-standard suspension for this peer
            std::lock_guard lock { mMtx };
            if(auto it { mSuspensionTracker.find(doubleSpendTxnDetails.sender) }; it != mSuspensionTracker.end())
            {
                stdValidationOnly = it->second.Overflowing();
                if(stdValidationOnly)
                {
                    LogPrint(BCLog::DOUBLESPEND, "Non-standard txn validation is suspended from peer=%d\n",
                        doubleSpendTxnDetails.sender);
                }
            }
        }

        // Find inputs from double-spend notification enabled transactions we should notify for
        std::vector<NotificationDetails> inputsToNotify {};
        for(const auto& txn : conflictedTxns)
        {
            if(auto [ gotInput, inputToNotify ] { GetNotificationDetails(txn, doubleSpendTxn, stdValidationOnly) }; gotInput)
            {
                const TxId& dsEnabledTxnId { inputToNotify.dsEnabledTxn->GetId() };
                LogPrint(BCLog::DOUBLESPEND, "Found conflicting inputs: Notification enabled txn %s : %d, and conflicting txn %s : %d\n",
                    dsEnabledTxnId.ToString(), inputToNotify.dsEnabledTxnInput,
                    inputToNotify.conflictingTxn->GetId().ToString(), inputToNotify.conflictingTxnInput);

                inputsToNotify.push_back(std::move(inputToNotify));
            }
        }

        // Verify all inputs we've decided to notify about
        for(const auto& input : inputsToNotify)
        {
            CValidationState state {};
            if(const auto& res { ValidateDoubleSpend(doubleSpendTxnDetails, input, state) }; res.has_value() && res.value())
            {
                LogPrint(BCLog::DOUBLESPEND, "Script verification for double-spend passed\n");

                // Check if we've already posted a notification for this conflicted txn
                const TxId& dsEnabledTxnId { input.dsEnabledTxn->GetId() };
                bool sendNotification {true};
                {
                    std::lock_guard lock { mMtx };
                    if(mTxnsNotified.contains(dsEnabledTxnId))
                    {
                        LogPrint(BCLog::DOUBLESPEND, "Already notified about txn %s, skipping\n", dsEnabledTxnId.ToString());
                        sendNotification = false;
                    }
                }

                if(sendNotification)
                {
                    // Send a notification for this double-spent input
                    SendNotification(input);
                }
            }
            else
            {
                if(state.IsInvalid())
                {
                    // Someone sent us an invalid double-spend; ban them
                    LogPrint(BCLog::DOUBLESPEND, "Script verification for double-spend failed\n");
                    Misbehaving(doubleSpendTxnDetails.sender, mConfig.GetBanScoreThreshold(), "double-spend-validation-failed");
                    return;
                }
                else
                {
                    // We timed out validating this double-spend or it violated a policy limit.
                    // The peer that sent it to us may just have more processing power than us,
                    // or they may be trying to DOS us. Increase their suspension score and if
                    // they exceed the limit then suspend double-spend processing from this peer
                    // for a little while.
                    LogPrint(BCLog::DOUBLESPEND, "Script verification for double-spend was cancelled\n");

                    std::lock_guard lock { mMtx };
                    if(auto it { mSuspensionTracker.find(doubleSpendTxnDetails.sender) }; it != mSuspensionTracker.end())
                    {
                        if(it->second += SUSPENSION_SCORE_INCREASE)
                        {
                            LogPrint(BCLog::DOUBLESPEND, "Suspension score exceeded for peer=%d\n", doubleSpendTxnDetails.sender);
                            it->second += SUSPENSION_DURATION;
                        }
                    }
                    else
                    {
                        using namespace std::chrono_literals;
                        SuspensionScore score { SUSPENSION_SCORE_MAX, SUSPENSION_SCORE_INCREASE, 1s };
                        mSuspensionTracker.insert( { doubleSpendTxnDetails.sender, std::move(score) } );
                    }
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::DOUBLESPEND, "Failed to process double-spend: %s\n", e.what());
    }
}

// Query the endpoint to see if they want a proof for a double-spend enabled transaction
bool DSAttemptHandler::SubmitQuery(
    const std::string& txid,
    const std::string& endpointAddrStr,
    int httpTimeout,
    unsigned protocolVer)
{
    rpc::client::RPCClientConfig clientConfig { rpc::client::RPCClientConfig::CreateForDoubleSpendEndpoint(
        mConfig, endpointAddrStr, httpTimeout, protocolVer) };
    rpc::client::HTTPRequest request { rpc::client::HTTPRequest::CreateDSEndpointQueryRequest(clientConfig, txid) };
    rpc::client::StringHTTPResponse response { {DSNT_HTTP_HEADER} };
    rpc::client::RPCClient client { clientConfig };
    LogPrint(BCLog::DOUBLESPEND, "Sending query to %s for double-spend enabled txn %s\n", endpointAddrStr, txid);
    client.SubmitRequest(request, &response);

    // Check and parse query response
    ResponseStatus rs { GetHTTPStatusAndWantsProof(response, endpointAddrStr) };
    if(!rs.ok)
    {
        // Bad response, add endpoint address to blacklist
        AddToBlacklist(endpointAddrStr);
        return false;
    }
    if(rs.status == HTTP_BAD_REQUEST)
    {
        // Hmm strange, but let's move on
        return false;
    }
    else if(rs.status != HTTP_OK)
    {
        // Bad response, add endpoint address to blacklist
        AddToBlacklist(endpointAddrStr);
        return false;
    }

    // Does endpoint want proof?
    if(!rs.wantsProof)
    {
        LogPrint(BCLog::DOUBLESPEND, "Endpoint %s doesn't want proof for %s\n", endpointAddrStr, txid);
        return false;
    }

    // Endpoint wants the proof
    return true;
}

// Update (or add) statistics for a slow endpoint
void DSAttemptHandler::UpdateSlowEndpoint(const std::string& endpoint)
{
    std::lock_guard lock { mMtx };

    // New or already tracked endpoint?
    if(const auto& it { mSlowEndpoints.find(endpoint) }; it != mSlowEndpoints.end())
    {
        // Update existing count
        it->second += 1;
        LogPrint(BCLog::DOUBLESPEND, "Updated stats for potentially slow endpoint %s, is slow: %u\n",
            endpoint, it->second.Overflowing());
    }
    else
    {
        // Calculate the leaky bucket drain interval based on the configured number of
        // timeouts / hour we allow from an endpoint.
        constexpr auto MINS_PER_HOUR {60};
        const auto slowRatePerHour { mConfig.GetDoubleSpendEndpointSlowRatePerHour() };
        const std::chrono::minutes drainInterval { MINS_PER_HOUR / slowRatePerHour };

        // Add new entry with initial count of 1 (because we've already had 1 timeout)
        mSlowEndpoints.insert(std::make_pair(endpoint, SlowEndpoint { slowRatePerHour, 1, drainInterval }));
        LogPrint(BCLog::DOUBLESPEND, "Started tracking stats for a new potentially slow endpoint %s\n", endpoint);
    }
}

// Check to see whether an endpoint is currently considered slow
bool DSAttemptHandler::IsEndpointSlow(const std::string& endpoint) const
{
    std::lock_guard lock { mMtx };

    // Do we have any statistics for this endpoint?
    if(const auto& it { mSlowEndpoints.find(endpoint) }; it != mSlowEndpoints.end())
    {
        // Has this endpoint exceeded the allowable rate of timeouts / hour ?
        return it->second.Overflowing();
    }

    return false;
}

// Deal with sending an HTTP notification to a double-spend endpoint
void DSAttemptHandler::SendNotification(const NotificationDetails& notificationDetails)
{
    try
    {
        // Serialise conflicting txn to disk as proof
        DSTxnSerialiser::TxnHandleSPtr handle { mTxnSerialiser.Serialise(*(notificationDetails.conflictingTxn)) };

        // Get fast submission timeout
        int timeout { mConfig.GetDoubleSpendEndpointFastTimeout() };

        // Get IP address skip list
        std::set<std::string> ipSkipList { mConfig.GetDoubleSpendEndpointSkipList() };

        // Notify every address listed in callback msg, upto a limit
        uint64_t endpointCount {0};
        std::unordered_set<std::string> ipsSeen {};
        for(const DSCallbackMsg::IPAddr& endpointAddr : notificationDetails.callbackMsg.GetAddresses())
        {
            // Apply configured limit for the number of IPs we will notify for a single txn
            if(++endpointCount > mConfig.GetDoubleSpendEndpointMaxCount())
            {
                LogPrint(BCLog::DOUBLESPEND, "Maximum number of notification endpoints reached, skipping the rest\n");
                return;
            }

            // Get IP address string and do the comm's to the endpoint
            const std::string& endpointAddrStr { DSCallbackMsg::IPAddrToString(endpointAddr) };

            // Lambda to perform submission via the slow queue, if required
            auto slowSubmitLambda = [this, endpointAddrStr, notificationDetails, handle] (unsigned retryCount)
            {
                SendNotificationSlow(endpointAddrStr, retryCount, notificationDetails, handle);
            };

            // Check for duplicate IP
            if(ipsSeen.count(endpointAddrStr) != 0)
            {
                LogPrint(BCLog::DOUBLESPEND, "Skipping notification to duplicate endpoint %s\n", endpointAddrStr);
                continue;
            }
            else
            {
                ipsSeen.insert(endpointAddrStr);
            }

            // Check blacklist, skiplist and slow endpoint tracking
            if(mServerBlacklist.IsBlacklisted(endpointAddrStr))
            {
                LogPrint(BCLog::DOUBLESPEND, "Skipping notification to blacklisted endpoint %s\n", endpointAddrStr);
            }
            else if(ipSkipList.count(endpointAddrStr) != 0)
            {
                LogPrint(BCLog::DOUBLESPEND, "Skipping notification to endpoint in skiplist %s\n", endpointAddrStr);
            }
            else if(IsEndpointSlow(endpointAddrStr))
            {
                LogPrint(BCLog::DOUBLESPEND, "Endpoint %s is currently slow, submitting via the slow queue\n", endpointAddrStr);
                make_task(mSlowThreadPool, std::move(slowSubmitLambda), MAX_PROOF_SUBMIT_ATTEMPTS);
            }
            else
            {
                // Try to query endpoint and submit proof
                bool submitSuccess {false};
                bool wantsProof {false};
                bool retry {true};
                unsigned retryCount { MAX_PROOF_SUBMIT_ATTEMPTS };
                while(!submitSuccess && retry && retryCount > 0)
                {
                    try
                    {
                        submitSuccess = QueryAndSubmitProof(endpointAddrStr, notificationDetails, handle, timeout, wantsProof, retry);
                    }
                    catch(CConnectionTimeout& e)
                    {
                        // Timeout; move to the slow processing queue to retry
                        LogPrint(BCLog::DOUBLESPEND, "Timeout sending notification to endpoint %s, resubmitting to the slow queue\n", endpointAddrStr);
                        retry = false;
                        UpdateSlowEndpoint(endpointAddrStr);
                        make_task(mSlowThreadPool, std::move(slowSubmitLambda), retryCount);
                    }
                    catch(std::exception& e)
                    {
                        LogPrint(BCLog::DOUBLESPEND, "Error sending notification to endpoint %s: %s\n", endpointAddrStr, e.what());
                    }

                    --retryCount;
                }

                // Did we send a notification, or is the endpoint not interested?
                if(submitSuccess || !wantsProof)
                {
                    // Remember this transaction, we won't need to notify about it again
                    RecordNotifiedTxn(notificationDetails.dsEnabledTxn->GetId());
                }
            }
        }
    }
    catch(std::exception& e)
    {
        LogPrint(BCLog::DOUBLESPEND, "Error producing and sending double-spend notification %s\n", e.what());
    }
}


// Deal with resubmitting an HTTP notification to a double-spend endpoint via the slow queue
void DSAttemptHandler::SendNotificationSlow(
    const std::string& endpointAddrStr,
    unsigned retryCount,
    const NotificationDetails& notificationDetails,
    const DSTxnSerialiser::TxnHandleSPtr& handle)
{
    try
    {
        // Get slow submission timeout
        int timeout { mConfig.GetDoubleSpendEndpointSlowTimeout() };

        bool submitSuccess {false};
        bool wantsProof {false};
        bool retry {true};
        while(!submitSuccess && retry && retryCount-- > 0)
        {
            submitSuccess = QueryAndSubmitProof(endpointAddrStr, notificationDetails, handle, timeout, wantsProof, retry);
        }

        // Did we send a notification, or is the endpoint not interested?
        if(submitSuccess || !wantsProof)
        {
            // Remember this transaction, we won't need to notify about it again
            RecordNotifiedTxn(notificationDetails.dsEnabledTxn->GetId());
        }
    }
    catch(CConnectionTimeout& e)
    {
        LogPrint(BCLog::DOUBLESPEND, "Timeout sending slow-queue notification to endpoint %s\n", endpointAddrStr);
        UpdateSlowEndpoint(endpointAddrStr);
    }
    catch(std::exception& e)
    {
        LogPrint(BCLog::DOUBLESPEND, "Error sending slow-queue notification to endpoint %s: %s\n", endpointAddrStr, e.what());
    }
}

// Query an endpoint and submit a proof if they say they want it
bool DSAttemptHandler::QueryAndSubmitProof(
    const std::string& endpointAddrStr,
    const NotificationDetails& notificationDetails,
    const DSTxnSerialiser::TxnHandleSPtr& handle,
    int httpTimeout,
    bool& wantsProof,
    bool& retry)
{
    bool submitSuccess {false};
    wantsProof = true;
    retry = false;
    const std::string& dsEnabledTxnId { notificationDetails.dsEnabledTxn->GetId().ToString() };
    unsigned protocolVer { notificationDetails.callbackMsg.GetProtocolVersion() };

    // Query endpoint to see if it wants this notification
    if(SubmitQuery(dsEnabledTxnId, endpointAddrStr, httpTimeout, protocolVer))
    {
        // Submit proof
        LogPrint(BCLog::DOUBLESPEND, "Submitting %lu bytes proof to %s for double-spend enabled txn %s\n",
            handle->GetFileSize(), endpointAddrStr, dsEnabledTxnId);
        rpc::client::RPCClientConfig config { rpc::client::RPCClientConfig::CreateForDoubleSpendEndpoint(mConfig, endpointAddrStr, httpTimeout, protocolVer) };
        rpc::client::HTTPRequest request { rpc::client::HTTPRequest::CreateDSEndpointSubmitRequest(config, handle->OpenFile(), handle->GetFileSize(),
            std::make_pair("txid", dsEnabledTxnId),
            std::make_pair("n", notificationDetails.dsEnabledTxnInput),
            std::make_pair("ctxid", notificationDetails.conflictingTxn->GetId().ToString()),
            std::make_pair("cn", notificationDetails.conflictingTxnInput))
        };
        rpc::client::StringHTTPResponse response { {DSNT_HTTP_HEADER} };
        rpc::client::RPCClient client { config };
        client.SubmitRequest(request, &response);

        // Check and parse submit response
        ResponseStatus rs { GetHTTPStatusAndWantsProof(response, endpointAddrStr) };
        if(!rs.ok)
        {
            // Very strange, server responded ok to our initial query but now it seems to be misbehaving.
            // Blacklist & give up
            AddToBlacklist(endpointAddrStr);
            throw std::runtime_error("Bad response for double-spend proof submission");
        }
        else if(rs.status != HTTP_OK)
        {
            // Something went wrong, but do they want us to retry?
            if(!rs.wantsProof)
            {
                LogPrint(BCLog::DOUBLESPEND, "Endpoint %s returned error and they don't want us to retry\n", endpointAddrStr);
                wantsProof = false;
            }
            else
            {
                LogPrint(BCLog::DOUBLESPEND, "Endpoint %s returned error but they do want proof\n", endpointAddrStr);
                retry = true;
            }
        }
        else
        {
            // Success
            LogPrint(BCLog::DOUBLESPEND, "Submitted proof ok to %s for double-spend enabled txn %s\n", endpointAddrStr, dsEnabledTxnId);
            submitSuccess = true;
        }
    }
    else
    {
        wantsProof = false;
    }

    return submitSuccess;
}

// Remember a txn we've already notified about
void DSAttemptHandler::RecordNotifiedTxn(const TxId& txid)
{
    std::lock_guard lock { mMtx };
    if(!mTxnsNotified.contains(txid))
    {
        mTxnsNotified.insert(std::make_pair(txid, mTxnsNotifiedIndex++));
    }
}

