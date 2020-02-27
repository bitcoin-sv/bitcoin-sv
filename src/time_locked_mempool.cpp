// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <clientversion.h>
#include <config.h>
#include <init.h>
#include <logging.h>
#include <memusage.h>
#include <mining/journal_change_set.h>
#include <net.h>
#include <policy/policy.h>
#include <scheduler.h>
#include <time_locked_mempool.h>
#include <txn_validator.h>

using namespace mining;

CTimeLockedMempool::CTimeLockedMempool()
{
    // Set some sane default values for config
    mMaxMemory = DEFAULT_MAX_NONFINAL_MEMPOOL_SIZE * 1024 * 1024;
    mPeriodRunFreq = DEFAULT_NONFINAL_CHECKS_FREQ;
    mPurgeAge = DEFAULT_NONFINAL_MEMPOOL_EXPIRY * 60 * 60;
}

// Add or update a time-locked transaction
void CTimeLockedMempool::addOrUpdateTransaction(const TxMempoolInfo& info,
                                                CValidationState& state)
{
    const CTransactionRef& txn { info.tx };

    std::unique_lock lock { mMtx };

    // Update or new addition?
    std::set<CTransactionRef> updated { getTransactionsUpdatedByNL(txn) };
    if(updated.empty())
    {
        if(state.IsNonFinal())
        {
            // New addition
            insertNL(info, state);
        }
        else
        {
            LogPrint(BCLog::MEMPOOL, "Non-final pool ignoring tx that doesn't finalise any we track: %s\n",
                txn->GetId().ToString());
        }
    }
    else if(updated.size() == 1)
    {
        // Validate update
        const CTransactionRef& oldTxn { *updated.begin() };
        bool finalised;
        if(validateUpdate(txn, oldTxn, state, finalised))
        {
            // Remove old txn this new one updates
            removeNL(oldTxn);

            // Do we want to update to another non-final or are we ready to finalise?
            if(finalised)
            {
                LogPrint(BCLog::MEMPOOL, "Finalising non-final tx: %s\n", txn->GetId().ToString());

                // For full belt-and-braces safety, resubmit newly final transaction for revalidation
                std::string reason {};
                bool standard { IsStandardTx(GlobalConfig::GetConfig(), *txn, chainActiveSharedData.GetChainActiveHeight() + 1, reason) };
                g_connman->ResubmitTxnForValidator(
                    std::make_shared<CTxInputData>(
                        TxSource::finalised,
                        standard ? TxValidationPriority::high : TxValidationPriority::low,
                        txn,
                        GetTime()
                    )
                );
            }
            else
            {
                insertNL(info, state);
            }
        }
        else
        {
            LogPrint(BCLog::MEMPOOL, "Rejecting non-final tx which failed checks: %s\n", txn->GetId().ToString());
        }
    }
    else
    {
        LogPrint(BCLog::MEMPOOL, "Rejecting non-final tx which wants to replace multiple txs: %s\n", txn->GetId().ToString());
        state.DoS(10, false, REJECT_INVALID, "bad-txn-update");
    }
}

// Get IDs of all held transactions
std::vector<TxId> CTimeLockedMempool::getTxnIDs() const
{
    std::vector<TxId> res {};

    std::shared_lock lock { mMtx };
    for(const auto& info : mTransactionMap.get<TagTxID>())
    {
        res.emplace_back(info.tx->GetId());
    }

    return res;
}

// Does this finalise an existing time-locked transaction?
bool CTimeLockedMempool::finalisesExistingTransaction(const CTransactionRef& txn) const
{
    std::set<CTransactionRef> updated {};

    {
        std::shared_lock lock { mMtx };

        if(mTransactionMap.empty())
        {
            // Can't be an update if we're not tracking any time-locked transactions
            return false;
        }

        // Check if this txn could update exactly 1 of our non-final txns and not anything else
        for(const CTxIn& in : txn->vin)
        {
            if(const auto& it { mUTXOMap.find(in.prevout) }; it != mUTXOMap.end())
            {
                updated.emplace(it->second);
            }
            else
            {
                return false;
            }
        }
    }

    if(updated.size() == 1)
    {
        // Check every input finalises
        for(const CTxIn& txin : txn->vin)
        {
            if(txin.nSequence != CTxIn::SEQUENCE_FINAL)
            {
                return false;
            }
        }

        return true;
    }

    return false;
}

// Check the given transaction doesn't try to double spend any of our locked UTXOs.
bool CTimeLockedMempool::checkForDoubleSpend(const CTransactionRef& txn) const
{
    std::shared_lock lock { mMtx };

    if(mUTXOMap.empty())
    {
        return false;
    }

    for(const CTxIn& txin : txn->vin)
    {
        if(mUTXOMap.find(txin.prevout) != mUTXOMap.end())
        {
            return true;
        }
    }

    return false;
}

// Is the given txn ID for one currently held?
bool CTimeLockedMempool::exists(const uint256& id) const
{
    std::shared_lock lock { mMtx };

    const auto& index { mTransactionMap.get<TagRawTxID>() };
    return index.find(id) != index.end();
}

// Is the given txn ID for one we held until recently?
bool CTimeLockedMempool::recentlyRemoved(const uint256& id) const
{
    std::shared_lock lock { mMtx };
    return mRecentlyRemoved.contains(id);
}

// Fetch the full entry we have for the given txn ID
TxMempoolInfo CTimeLockedMempool::getInfo(const uint256& id) const
{
    TxMempoolInfo info {};

    std::shared_lock lock { mMtx };

    const auto& index { mTransactionMap.get<TagRawTxID>() };
    if(const auto& it { index.find(id) }; it != index.end())
    {
        info = *it;
    }

    return info;
}

// Launch periodic checks for finalised txns
void CTimeLockedMempool::startPeriodicChecks(CScheduler& scheduler)
{
    scheduler.scheduleEvery(std::bind(&CTimeLockedMempool::periodicChecks, this), mPeriodRunFreq);
}

// Dump to disk
void CTimeLockedMempool::dumpMempool() const
{
    int64_t start { GetTimeMicros() };

    std::shared_lock lock { mMtx };

    try
    {
        FILE* filestr { fsbridge::fopen(GetDataDir() / "non-final-mempool.dat.new", "wb") };
        if(!filestr)
        {
            throw std::runtime_error("Failed to create new non-final mempool dump file");
        }

        CAutoFile file { filestr, SER_DISK, CLIENT_VERSION };
        file << DUMP_FILE_VERSION;

        const auto& index { mTransactionMap.get<TagTxID>() };
        uint64_t numTxns { index.size() };
        file << numTxns;

        for(const auto& details : index)
        {
            file << *(details.tx);
            file << details.nTime;
        }

        FileCommit(file.Get());
        file.fclose();
        RenameOver(GetDataDir() / "non-final-mempool.dat.new", GetDataDir() / "non-final-mempool.dat");
        int64_t last { GetTimeMicros() };
        LogPrintf("Dumped %d txns to non-final mempool: %gs to dump\n", numTxns, (last - start) * 0.000001);
    }
    catch(const std::exception& e)
    {
        LogPrintf("Failed to dump non-final mempool: %s. Continuing anyway.\n", e.what());
    }
}

// Load from disk
bool CTimeLockedMempool::loadMempool(const task::CCancellationToken& shutdownToken) const
{
    try
    {
        FILE* filestr { fsbridge::fopen(GetDataDir() / "non-final-mempool.dat", "rb") };
        CAutoFile file { filestr, SER_DISK, CLIENT_VERSION };
        if(file.IsNull())
        {
            throw std::runtime_error("Failed to open non-final mempool file from disk");
        }

        int64_t count {0};
        int64_t skipped {0};
        int64_t failed {0};
        int64_t nNow { GetTime() };

        uint64_t version {};
        file >> version;
        if(version != DUMP_FILE_VERSION)
        {
            throw std::runtime_error("Bad non-final mempool dump version");
        }

        // Number of saved txns
        uint64_t numTxns {0};
        file >> numTxns;

        // Take a reference to the validator.
        const auto& txValidator { g_connman->getTxnValidator() };
        while(numTxns--)
        {
            CTransactionRef tx {};
            int64_t nTime {};
            file >> tx;
            file >> nTime;

            if(nTime + mPurgeAge > nNow)
            {
                // Mempool Journal ChangeSet
                CJournalChangeSetPtr changeSet {
                    mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::INIT)
                };
                std::string reason {};
                bool standard { IsStandardTx(GlobalConfig::GetConfig(), *tx, chainActiveSharedData.GetChainActiveHeight() + 1, reason) };
                const CValidationState& state {
                    // Execute txn validation synchronously.
                    txValidator->processValidation(
                                        std::make_shared<CTxInputData>(
                                                            TxSource::file, // tx source
                                                            standard ? TxValidationPriority::high : TxValidationPriority::low,
                                                            tx,    // a pointer to the tx
                                                            nTime, // nAcceptTime
                                                            true),  // fLimitFree
                                        changeSet, // an instance of the mempool journal
                                        true) // fLimitMempoolSize
                };

                // Check results
                if(state.IsValid())
                {
                    ++count;
                }
                else
                {
                    ++failed;
                }
            }
            else
            {
                ++skipped;
            }

            if(shutdownToken.IsCanceled())
            {
                // Abort early
                return false;
            }
        }

        LogPrintf("Imported non-final mempool transactions from disk: %i successes, %i "
            "failed, %i expired\n", count, failed, skipped);
    }
    catch(const std::exception& e)
    {
        LogPrintf("Failed to deserialize non-final mempool data on disk: %s. Continuing anyway.\n", e.what());
        return false;
    }

    return true;
}

// Get number of txns we hold
size_t CTimeLockedMempool::getNumTxns() const
{
    std::shared_lock lock { mMtx };
    return mTransactionMap.get<TagTxID>().size();
}

// Estimate total memory usage
size_t CTimeLockedMempool::estimateMemoryUsage() const
{
    std::shared_lock lock { mMtx };
    return estimateMemoryUsageNL();
}

// Load or reload our config
void CTimeLockedMempool::loadConfig()
{
    std::unique_lock lock { mMtx };

    // Get max memory size in bytes
    mMaxMemory = gArgs.GetArg("-maxmempoolnonfinal", DEFAULT_MAX_NONFINAL_MEMPOOL_SIZE) * 1024 * 1024;
    // Get periodic checks run frequency
    mPeriodRunFreq = gArgs.GetArg("-checknonfinalfreq", DEFAULT_NONFINAL_CHECKS_FREQ);
    // Get configured purge age (convert hours to seconds)
    mPurgeAge = gArgs.GetArg("-mempoolexpirynonfinal", DEFAULT_NONFINAL_MEMPOOL_EXPIRY) * 60 * 60;
}

// Fetch all transactions updated by the given new transaction.
// Caller holds mutex.
std::set<CTransactionRef> CTimeLockedMempool::getTransactionsUpdatedByNL(const CTransactionRef& txn) const
{
    std::set<CTransactionRef> txns {};

    // Find all transactions we're tracking that have any of the same outpoints as this transaction
    for(const CTxIn& in : txn->vin)
    {
        if(const auto& it { mUTXOMap.find(in.prevout) }; it != mUTXOMap.end())
        {
            txns.emplace(it->second);
        }
    }

    return txns;
}

// Insert a new transaction
void CTimeLockedMempool::insertNL(const TxMempoolInfo& info, CValidationState& state)
{
    const CTransactionRef& txn { info.tx };

    // Put new txn in the main index
    auto& index { mTransactionMap.get<TagTxID>() };
    index.emplace(info);

    // Record UTXOs locked by this transaction
    for(const CTxIn& input : txn->vin)
    {
        mUTXOMap[input.prevout] = txn;
    }

    // Track memory used by this txn
    mTxnMemoryUsage += txn->GetTotalSize();

    // Check we haven't exceeded max memory
    size_t memUsage { estimateMemoryUsageNL() };
    if(memUsage > mMaxMemory)
    {
        LogPrint(BCLog::MEMPOOL, "Dropping non-final tx %s because mempool is full\n",
            txn->GetId().ToString());
        state.Invalid(false, REJECT_MEMPOOL_FULL, "non-final-pool-full");
        removeNL(txn);
    }
    else
    {
        LogPrint(BCLog::MEMPOOL, "Added non-final tx: %s, mem: %d\n", txn->GetId().ToString(),
            memUsage);
    }
}

// Remove an old transaction
void CTimeLockedMempool::removeNL(const CTransactionRef& txn)
{
    // Remove from main index
    auto& index { mTransactionMap.get<TagTxID>() };
    index.erase(txn);

    // Track removal in bloom filter
    mRecentlyRemoved.insert(txn->GetId());

    // Remove UTXOs locked by that transacrion
    for(const CTxIn& input : txn->vin)
    {
        if(mUTXOMap.erase(input.prevout) != 1)
        {
            LogPrint(BCLog::MEMPOOL, "Warning: Failed to find and remove UTXO from old non-final tx %s\n",
                txn->GetId().ToString());
        }
    }

    // Update memory used
    auto txnSize { txn->GetTotalSize() };
    if(mTxnMemoryUsage <= txnSize)
    {
        mTxnMemoryUsage = 0;
    }
    else
    {
        mTxnMemoryUsage -= txnSize;
    }

    LogPrint(BCLog::MEMPOOL, "Removed old non-final tx: %s, mem: %d\n", txn->GetId().ToString(),
        estimateMemoryUsageNL());
}

// Perform checks on a transaction before allowing an update
bool CTimeLockedMempool::validateUpdate(const CTransactionRef& newTxn,
                                        const CTransactionRef& oldTxn,
                                        CValidationState& state,
                                        bool& finalised) const
{
    // Must have same number of inputs
    if(newTxn->vin.size() != oldTxn->vin.size())
    {
        LogPrint(BCLog::MEMPOOL, "Update to non-final txn has different number of inputs\n");
        state.DoS(10, false, REJECT_INVALID, "bad-txn-update");
        return false;
    }

    bool seenIncrease {false};
    finalised = true;

    // Check corresponding inputs on new and old
    for(unsigned i = 0; i < newTxn->vin.size(); ++i)
    {
        const CTxIn& newInput { newTxn->vin[i] };
        const CTxIn& oldInput { oldTxn->vin[i] };

        // Check each input spends the same outpoint
        if(newInput.prevout != oldInput.prevout)
        {
            LogPrint(BCLog::MEMPOOL, "Update to non-final txn has different inputs\n");
            state.DoS(10, false, REJECT_INVALID, "bad-txn-update");
            return false;
        }

        // Check sequence numbers are only ever going forward
        if(newInput.nSequence < oldInput.nSequence)
        {
            LogPrint(BCLog::MEMPOOL, "Update to non-final txn would decrease nSequence\n");
            state.DoS(10, false, REJECT_INVALID, "bad-txn-update");
            return false;
        }
        else if(newInput.nSequence > oldInput.nSequence)
        {
            seenIncrease = true;
            if(newInput.nSequence != CTxIn::SEQUENCE_FINAL)
            {
                // Still not finalised
                finalised = false;
            }
        }
    }

    // Finally, must have seen at least 1 increase in an nSequence number
    if(!seenIncrease)
    {
        LogPrint(BCLog::MEMPOOL, "Update to non-final txn didn't increase any nSequence\n");
        state.DoS(10, false, REJECT_INVALID, "bad-txn-update");
        return false;
    }

    return true;
}

// Estimate our memory usage
size_t CTimeLockedMempool::estimateMemoryUsageNL() const
{
    size_t numElements { mTransactionMap.size() };

    // Experiment shows that the memory usage of the multi-index container can be
    // approximated as:
    // 24 bytes overhead (3 pointers) per index per (number of elements + 1)
    // + (sizeof(element) * (number of elements + 1))
    constexpr size_t numIndexes {3};
    constexpr size_t overhead { 3 * numIndexes * sizeof(void*) };
    size_t multiIndexUsage { (overhead * (numElements+1)) + (sizeof(TxnMultiIndex::value_type) * (numElements+1)) };
    multiIndexUsage += mTxnMemoryUsage;

    return memusage::MallocUsage(multiIndexUsage) +
           memusage::DynamicUsage(mUTXOMap);
}

// Do periodic checks for finalised txns and txns to purge
void CTimeLockedMempool::periodicChecks()
{
    // Get current time
    int64_t now { GetTime() };

    // Get next block height and MTP
    int nextBlockHeight {};
    int medianTimePast {};
    {
        LOCK(cs_main);
        nextBlockHeight = chainActive.Height() + 1;
        medianTimePast = chainActive.Tip()->GetMedianTimePast();
    }

    std::unique_lock lock { mMtx };

    // Iterate over transactions in unlocking time order
    auto& index { mTransactionMap.get<TagUnlockingTime>() };
    auto it { index.begin() };
    while(it != index.end())
    {
        CTransactionRef txn { it->tx };
        int64_t insertionTime { it->nTime };
        int64_t timeInPool { now - insertionTime };

        // Move iterator on so we don't have to care whether this txn gets removed
        ++it;

        // Lock time passed?
        if(IsFinalTx(*txn, nextBlockHeight, medianTimePast))
        {
            LogPrint(BCLog::MEMPOOL, "Finalising non-final transaction %s at block height %d, mtp %d\n",
                txn->GetId().ToString(), nextBlockHeight, medianTimePast);

            removeNL(txn);

            // For full belt-and-braces safety, resubmit newly final transaction for revalidation
            std::string reason {};
            bool standard { IsStandardTx(GlobalConfig::GetConfig(), *txn, nextBlockHeight, reason) };
            g_connman->ResubmitTxnForValidator(
                std::make_shared<CTxInputData>(
                    TxSource::finalised,
                    standard ? TxValidationPriority::high : TxValidationPriority::low,
                    txn,
                    GetTime()
                )
            );
        }
        // Purge age passed?
        else if(timeInPool >= mPurgeAge)
        {
            LogPrint(BCLog::MEMPOOL, "Purging expired non-final transaction: %s\n",
                txn->GetId().ToString());
            removeNL(txn);
        }
    }
}

