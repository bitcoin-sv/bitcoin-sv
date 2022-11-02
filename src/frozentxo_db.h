// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_FROZENTXO_DB_H
#define BITCOIN_FROZENTXO_DB_H

#include "primitives/transaction.h"
#include "dbwrapper.h"
#include "prevector.h"

#include <shared_mutex>
#include <utility>
#include <memory>
#include <limits>
#include <initializer_list>




/**
 * Provides access to persistent database of frozen transaction outputs.
 *
 * @note Unless explicitly stated otherwise, changes to database are immediately flushed to disk for consistency reasons.
 */
class CFrozenTXODB
{
private:
    /**
     * Mutex used to prevent simultaneous modification of database by several threads.
     *
     * Access to levelDB already is thread safe, but we also need atomicity and consistency
     * if there are several levelDB operations executed one after another (e.g. first a read and
     * then a write that depends on the read result).
     */
    std::shared_mutex mtx_db;

    // This levelDB database stores data for frozen TXOs
    CDBWrapper db;

    /**
     * Maximum stop value in valid HeightInterval (except MAXINT values) of all consensus frozen TXOs
     * currently stored in database for which policyExpiresWithConsensus=true.
     *
     * This value can be used to quickly determine if a valid transaction spending a TXO, which was at
     * some point considered to be consensus frozen, could become invalid at lower heights.
     *
     * One specific use-case for this value is during a reorg to lower block-chain height. A TXO that was
     * unfrozen at some height H, can be spent normally and a transaction spending it can be accepted into
     * mempool (as long as mempool height is H or larger).
     * If we now do a reorg below height H, this transaction must be removed from mempool. In order to do
     * that, the whole mempool must be checked, which could have a performance overhead.
     *
     * If, however, we know that the largest height at which any TXO has become unfrozen is below the current
     * mempool height during reorg, then any transaction in mempool spending a TXO that was frozen on lower
     * heights is guaranteed to still be valid. Consequently, a whole mempool check is not needed.
     *
     * It is expected that most of the time this value will be lower than mempool height during reorg so that
     * performance overhead can be avoided.
     *
     * The value is MININT if no TXOs have been unfrozen yet.
     *
     * Access to the value is protected by mtx_db.
     */
    std::int32_t max_FrozenTXOData_enforceAtHeight_stop;

    /**
     * Maximum enforceAtHeight of all whitelisted confiscation transactions currently stored in database.
     *
     * The value is MININT if no confiscation transactions have been whitelisted yet.
     *
     * Access to the value is protected by mtx_db.
     *
     * This member has similar purpose as max_FrozenTXOData_enforceAtHeight_stop.
     */
    std::int32_t max_WhitelistedTxData_enforceAtHeight;

    /**
     * Constructor opens an existing database creating it if it does not exist.
     *
     * Database is closed when object is destroyed.
     *
     * @param cache_size Size of cache (in bytes) used by underlying levelDB database
     */
    explicit CFrozenTXODB(std::size_t cache_size);
    CFrozenTXODB(CFrozenTXODB&&) = delete; // no copying or moving
    CFrozenTXODB(const CFrozenTXODB&) = delete;
    CFrozenTXODB& operator=(CFrozenTXODB&&) = delete;
    CFrozenTXODB& operator=(const CFrozenTXODB&) = delete;

    /**
     * Provides common functionality needed to iterate over records in database
     *
     * @tparam VRecordType Iterator will iterate only over records of this type.
     */
    template<std::uint8_t VRecordType>
    class IteratorBase
    {
    protected:
        static constexpr std::uint8_t record_type = VRecordType;

        std::unique_ptr<CDBIterator> db_iter;

        IteratorBase(std::unique_ptr<CDBIterator>&& db_iter)
        : db_iter(std::move(db_iter))
        {
            // All keys are prefixed with one byte containing the record type.
            // We're also assuming that all keys in database are ordered after an empty key that contains just the record type
            // so that the following seek will position the iterator to one of the following:
            //  - at first record of specified type
            //  - at the record of some other type (if there are no records of specified type in database, but there are others that are ordered after).
            //  - at the end of database (if there are also no records of other type that are ordered after).
            this->db_iter->Seek(record_type);
        }

        ~IteratorBase() = default;

    public:
        /**
         * Returns true if iterator is valid.
         *
         * Otherwise returns false, which also signals the end of the list.
         */
        bool Valid() const
        {
            std::uint8_t rt;
            return this->db_iter->Valid() && this->db_iter->GetKey(rt) && rt==record_type;
        }

        /**
         * Move iterator forward
         */
        void Next()
        {
            this->db_iter->Next();
        }
    };

public:
    ~CFrozenTXODB() = default;

    /**
     * Initialize the connection to database
     *
     * Parameters are the same as in constructor.
     *
     * Afterwards, Instance() method can be called to access the database.
     *
     * Method is not thread-safe and can only be called if connection to database has not yet been initialized.
     * Typically it is only called during program initialization.
     */
    static void Init(std::size_t cache_size);

    /**
     * Access to a single object of this class in application
     *
     * Can only be called after Initialize() was called and until Shutdown() is called.
     *
     * Single instance is needed because underlying lebelDB does not allow multiple
     * connections to the same database on disk.
     */
    static CFrozenTXODB& Instance();

    /**
     * Shutdown the connection to database
     *
     * Afterwards, Instance() method must no longer be called.
     *
     * Method is not thread-safe. Typically it is only called during program shutdown.
     */
    static void Shutdown();

    /**
     * Provides additional data about a frozen TXO
     *
     * This data defines conditions under which TXO is considered frozen.
     *
     * Use methods IsFrozenOnPolicy() and IsFrozenOnConsensus() to check if TXO is actually considered frozen.
     */
    class FrozenTXOData
    {
    private:
        // Default constructor leaves all fields uninitialized and is private so that
        // Create* factory method must be used.
        explicit FrozenTXOData() = default;

    public:
        // NOTE: All data members are public so that un/serialization function can be implemented externally.

        /**
         * Blacklist on which frozen TXO is stored
         */
        enum class Blacklist : std::uint8_t
        {
            /**
             * TXO is frozen by consensus
             *
             * If node receives newly mined block, which contains transaction that spends this TXO,
             * the block should be rejected. That is in addition to rejecting new transactions that
             * is done for TXOs in PolicyOnly blacklist. In other words, TXOs frozen by consensus are
             * always also considered to be frozen by node policy.
             */
            Consensus = 1,

            /**
             * TXO is frozen only by node policy
             *
             * If a node receives new transaction, which tries to spend this TXO, the transaction should be rejected and is
             * not included in next block.
             * Already mined blocks are accepted, even if they contain such transactions.
             */
            PolicyOnly = 2,

            /**
             * TXO is confiscated by a whitelisted confiscation transaction
             *
             * Confiscated TXO is considered consensus frozen on all heights and, by extension, also policy frozen.
             */
            Confiscation = 3
        } blacklist;

        /**
         * Specifies interval of block heights.
         *
         * Interval is assumed to be half-open [start, stop).
         */
        struct HeightInterval
        {
            std::int32_t start;
            std::int32_t stop;

            HeightInterval() = default;

            HeightInterval(std::int32_t start, std::int32_t stop=std::numeric_limits<std::int32_t>::max())
            : start(start)
            , stop(stop)
            {}

            bool valid() const
            {
                return this->start < this->stop;
            }

            bool operator==(const HeightInterval& o) const
            {
                return this->start == o.start &&
                       this->stop  == o.stop;
            }

            bool operator!=(const HeightInterval& o) const
            {
                return !(*this == o);
            }

            template<class Stream>
            void Serialize(Stream& os) const
            {
                os << this->start;
                os << this->stop;
            }

            template<class Stream>
            void Unserialize(Stream& is)
            {
                is >> this->start;
                is >> this->stop;
            }
        };

        /**
         * Container for storing block height intervals
         */
        class EnforceAtHeightType : public prevector<1, HeightInterval> // It is assumed that most values will contain just one interval.
        {
        public:
            using prevector::prevector;
            EnforceAtHeightType(std::initializer_list<HeightInterval> l)
            : prevector(l.begin(), l.end())
            {}
        };

        /**
         * Array of block height intervals on which TXO is considered frozen on consensus blacklist.
         *
         * TXO is considered frozen at height h on consensus blacklist iff h is contained in at least one
         * half-open interval [start, stop) in this array. Order in which intervals are specified in array
         * is arbitrary (i.e. order does not affect checking if TXO is frozen).
         *
         * Intervals with start>=stop are considered invalid because they do not contain any height and are ignored.
         *
         * If this array is empty or contains only ignored intervals, TXO is considered not frozen at any
         * height on consensus blacklists. If policyExpiresWithConsensus=true, this is also true for policy
         * blacklist and result is the same as if record for TXO did not exist.
         *
         * Note that TXO is considered frozen on policy blacklist also on heights before start of a valid interval.
         * Consequently, TXO is policy frozen in any gaps between intervals.
         *
         * Only applicable if frozen TXO is stored on consensus or confiscation blacklist.
         */
        EnforceAtHeightType enforceAtHeight;

        /**
         * Specifies what happens with frozen TXO at block heights after all intervals in enforceAtHeight.
         *
         * These are all heights larger than or equal to h, where h is the maximum 'stop' value of all non-ignored intervals present in enforceAtHeight.
         *
         * If true, TXO is considered to be removed from policy blacklist too. I.e. TXO can be spent normally as
         * if the record for this frozen TXO did not exist.
         *
         * If false, TXO is considered to be removed only from consensus blacklist, but remains on policy blacklist.
         *
         * Only applicable if frozen TXO is stored on consensus or confiscation blacklist.
         */
        bool policyExpiresWithConsensus;

        /**
         * Create FrozenTXOData object where all data members are left uninitialized
         *
         * Should only be used when values for all data members will be set later (e.g. by call to GetFrozenTXOData() or by unserialization)
         */
        static FrozenTXOData Create_Uninitialized()
        {
            return FrozenTXOData();
        }

        /**
         * Returns true iff TXO with this data should be considered frozen on policy blacklist at given block height.
         */
        bool IsFrozenOnPolicy(std::int32_t nHeight) const
        {
            if(this->blacklist == Blacklist::PolicyOnly || this->blacklist == Blacklist::Confiscation)
            {
                // All TXOs on PolicyOnly or Confiscation blacklist are always considered frozen regardless of block height.
                return true;
            }

            if(!this->policyExpiresWithConsensus)
            {
                // If TXO is on Consensus blacklist and policy freeze does not expire with consensus, it is also always considered frozen.
                return true;
            }

            for(auto& i: this->enforceAtHeight)
            {
                // If TXO is on Consensus blacklist and policy freeze expires with consensus, it is considered
                // frozen before specified block height but only if interval is not ignored.
                if( i.valid() && nHeight < i.stop )
                {
                    return true;
                }
            }

            return false;
        }

        /**
         * Returns true iff TXO with this data should be considered frozen on consensus blacklist at given block height.
         */
        bool IsFrozenOnConsensus(std::int32_t nHeight) const
        {
            if(this->blacklist == Blacklist::Confiscation)
            {
                // If TXO is on Confiscation blacklist, it is considered consensus frozen on all heights
                return true;
            }

            if(this->blacklist != Blacklist::Consensus)
            {
                // If TXO is not on Consensus blacklist, it is not consensus frozen
                return false;
            }

            for(auto& i: this->enforceAtHeight)
            {
                if(nHeight >= i.start && nHeight < i.stop)
                {
                    // Frozen, if given block height is contained in any interval
                    return true;
                }
            }

            return false;
        }

        bool operator==(const FrozenTXOData& o) const
        {
            return
                this->blacklist == o.blacklist &&
                (
                    this->blacklist == Blacklist::PolicyOnly || // If TXO is on PolicyOnly blacklist, all other data is not applicable and does not need to be compared
                    ( this->enforceAtHeight            == o.enforceAtHeight &&
                      this->policyExpiresWithConsensus == o.policyExpiresWithConsensus )
                )
            ;
        }
    };

    enum class FreezeTXOResult
    {
        OK = 0,
        OK_ALREADY_FROZEN = 1,
        OK_UPDATED_TO_CONSENSUS_BLACKLIST = 2,
        OK_UPDATED = 3,
        ERROR_ALREADY_IN_CONSENSUS_BLACKLIST = 4
    };

    /**
     * Freeze specified TXO on policy-only blacklist
     *
     * If the TXO is currently not frozen, a new record is added to DB of frozen TXOs and method returns OK.
     *
     * If TXO is already frozen (i.e. if the record already exists) the method proceeds as follows:
     *  - if record in DB has frozenTXOData.blacklist=PolicyOnly, method does nothing and
     *    returns OK_ALREADY_FROZEN.
     *  - if record in DB has frozenTXOData.blacklist=Consensus or frozenTXOData.blacklist=Confiscation, method does nothing and
     *    returns ERROR_ALREADY_IN_CONSENSUS_BLACKLIST.
     *
     * For performance reasons changes in database may not be immediately flushed to disk. @see Sync
     *
     * @throws dbwrapper_error If writing to database fails.
     *
     * @param txo Id of a transaction output that will be frozen.
     */
    auto FreezeTXOPolicyOnly(const COutPoint& txo) -> FreezeTXOResult;

    /**
     * Freeze specified TXO on consensus blacklist on given block heights
     *
     * If the TXO is currently not frozen, a new record is added to DB of frozen TXOs and method returns OK.
     *
     * If TXO is already frozen (i.e. if the record already exists) the method proceeds as follows:
     *  - if record in DB has frozenTXOData.blacklist=PolicyOnly, frozenTXOData in DB is updated and method
     *    returns OK_UPDATED_TO_CONSENSUS_BLACKLIST.
     *  - otherwise, if all values in frozenTXOData are equal to given parameters, method does nothing and
     *    returns OK_ALREADY_FROZEN.
     *  - otherwise, method updates values in frozenTXOData and
     *    returns OK_UPDATED.
     *    Note that if frozenTXOData.blacklist=Confiscation, blacklist is unchanged and values in frozenTXOData
     *    are still updated for consistency reasons even though these values have no effect, because confiscated
     *    TXO is considered consensus frozen on all heights.
     *
     * This method is also used to unfreeze consensus frozen TXO by specifying enforceAtHeight accordingly.
     *
     * For performance reasons changes in database may not be immediately flushed to disk. @see Sync
     *
     * @throws dbwrapper_error If writing to database fails.
     *
     * @param txo Id of a transaction output that will be frozen.
     * @param enforceAtHeight @see FrozenTXOData::enforceAtHeight
     * @param policyExpiresWithConsensus @see FrozenTXOData::policyExpiresWithConsensus
     */
    auto FreezeTXOConsensus(const COutPoint& txo, const FrozenTXOData::EnforceAtHeightType& enforceAtHeight, bool policyExpiresWithConsensus) -> FreezeTXOResult;

    enum class UnfreezeTXOResult
    {
        OK = 0,
        ERROR_TXO_IS_IN_CONSENSUS_BLACKLIST = 1,
        ERROR_TXO_NOT_FROZEN = 2
    };

    /**
     * Unfreeze TXO that is currently frozen on policy-only blacklist.
     *
     * If a record for TXO does not exist, method does nothing and returns ERROR_TXO_NOT_FROZEN.
     * If TXO is currently in PolicyOnly blacklist, removes record for TXO and returns OK.
     * If TXO is currently in Consensus blacklist, does nothing and returns ERROR_TXO_IS_IN_CONSENSUS_BLACKLIST.
     *
     * For performance reasons changes in database may not be immediately flushed to disk. @see Sync
     *
     * @throws dbwrapper_error If writing to database fails.
     *
     * @param txo Id of a transaction output that will be unfrozen.
     */
    auto UnfreezeTXOPolicyOnly(const COutPoint& txo) -> UnfreezeTXOResult;

    /**
     * Flush all changes in database to disk.
     */
    void Sync();

    struct UnfreezeAllResult
    {
        unsigned int numUnfrozenPolicyOnly;
        unsigned int numUnfrozenConsensus;
        unsigned int numUnwhitelistedTxs;
    };

    /**
     * Unfreeze all currently frozen TXOs and un-whitelist all whitelisted transactions
     *
     * This effectively removes all records from DB if keepPolicyEntries=false.
     *
     * @param keepPolicyEntries If true, TXOs only frozen on PolicyOnly blacklist are not unfrozen.
     *                          This option does not apply to expired consensus frozen TXOs that are now considered policy frozen
     *                          because of policyExpiresWithConsensus=false (i.e. these are also unfrozen if this option is true).
     * @return number of TXOs that were unfrozen and number of un-whitelisted transactions
     */
    auto UnfreezeAll(bool keepPolicyEntries=false) -> UnfreezeAllResult;

    struct CleanExpiredRecordsResult
    {
        unsigned int numConsensusRemoved;
        unsigned int numConsensusUpdatedToPolicyOnly;
    };

    /**
     * Remove/update all TXO records that are considered expired at given block height and higher.
     *
     * Specifically, method searches for records matching all of the following criteria:
     *  - blacklist = consensus
     *  - Maximum enforceAtHeight.stop over all valid intervals <= nHeight
     *
     * If policyExpiresWithConsensus = true, record is removed, otherwise record is updated to PolicyOnly blacklist.
     */
    auto CleanExpiredRecords(std::int32_t nHeight) -> CleanExpiredRecordsResult;

    /**
     * Get data for given TXO
     *
     * @return true, if data for given TXO was found and false otherwise (e.g. TXO does not exist in database).
     *
     * @param txo Id of a transaction output
     * @param[out] frozenTXOData If TXO is found in DB, data for frozen transaction output is set in this variable.
     *                           If TXO is not found in DB, value of this variable is left unchanged.
     *
     * @note The implementation always accesses the underlying database, which is assumed to provide suitable caching to increase performance.
     */
    [[nodiscard]] bool GetFrozenTXOData(const COutPoint& txo, FrozenTXOData& frozenTXOData);

    /**
     * Provides ability to iterate over frozen TXOs
     *
     * Example:
     * @code
     * for(auto it=db.QueryAllFrozenTXOs(); it.Valid(); it.Next()) { ... }
     * @endcode
     *
     * @note Iterator object should be destroyed as soon as it is no longer needed so that any resources
     *       needed to connect to the underlying database on disk and freed.
     */
    class FrozenTXOIterator : public IteratorBase<1>
    {
    private:
        FrozenTXOIterator(std::unique_ptr<CDBIterator>&& db_iter)
        : IteratorBase(std::move(db_iter))
        {}
        friend class CFrozenTXODB;

    public:
        /**
         * Return id and other data about frozen TXO to which the iterator currently points.
         *
         * Method returns a std::pair object with the following contents:
         *  - first: id of frozen transaction output
         *  - second: additional data about a frozen TXO
         *
         * @note This method must not be called if Valid() returns false.
         */
        auto GetFrozenTXO() const -> std::pair<COutPoint, FrozenTXOData>;
    };

    /**
     * Return iterator that can be used to get all frozen TXOs currently stored in DB
     */
    auto QueryAllFrozenTXOs() -> FrozenTXOIterator;

    /**
     * Return value of max_FrozenTXOData_enforceAtHeight_stop
     */
    std::int32_t Get_max_FrozenTXOData_enforceAtHeight_stop();


    /**
     * Returns true if tx is considered a confiscation transaction and false if not.
     *
     * Validity of a confiscation transaction is not checked.
     */
    static bool IsConfiscationTx(const CTransaction& tx);

    /**
     * Returns true if contents of given confiscation transaction is valid and false if not.
     *
     * It is not checked if confiscation transaction is whitelisted.
     * Only validation checks specific to confiscation transactions are performed. Even if this method
     * returns true, transaction may still be invalid (e.g. missing inputs, invalid amounts in outputs,
     * spending coinbase output too soon...).
     *
     * @param confiscationTx Confiscation transaction. IsConfiscationTx(confiscationTx) must return true (assert).
     */
    static bool ValidateConfiscationTxContents(const CTransaction& confiscationTx);

    /**
     * Provides data about a whitelisted confiscation transaction
     */
    class WhitelistedTxData
    {
    private:
        // Default constructor leaves all fields uninitialized and is private so that
        // Create* factory method must be used.
        explicit WhitelistedTxData() = default;

    public:
        /**
         * Create WhitelistedTXData object where all data members are left uninitialized
         *
         * Should only be used when values for all data members will be set later (e.g. by call to IsTxWhitelisted() or by unserialization)
         */
        static WhitelistedTxData Create_Uninitialized()
        {
            return WhitelistedTxData();
        }

        /**
         * Minimum block height at which confiscation transaction can be spent
         */
        std::int32_t enforceAtHeight;

        /**
         * List of TXOs confiscated by this confiscation transaction
         */
        std::vector<COutPoint> confiscatedTXOs;
    };

    enum class WhitelistTxResult
    {
        OK = 0,
        OK_ALREADY_WHITELISTED_AT_LOWER_HEIGHT = 1,
        OK_UPDATED = 2,
        ERROR_TXO_NOT_CONSENSUS_FROZEN = 3,
        ERROR_NOT_VALID = 4
    };

    /**
     * Whitelist a confiscation transaction.
     *
     * If the transaction is not yet whitelisted:
     *   If specified transaction is not a valid confiscation transaction, method returns ERROR_NOT_VALID and does nothing.
     *   If its inputs are not all considered consensus frozen at enforceAtHeight, method returns ERROR_TXO_NOT_CONSENSUS_FROZEN and does nothing. Note that it is allowed for an input to be already confiscated by a previously whitelisted confiscation transaction.
     *   Otherwise new record is added to DB, specified TXOs are moved to Confiscation blacklist and method returns OK.
     *
     * If the transaction is already whitelisted:
     *   If specified value of enforceAtHeight is larger than the before, method returns OK_ALREADY_WHITELISTED_AT_LOWER_HEIGHT and does nothing.
     *   If specified value of enforceAtHeight is the same as before, method returns OK and does nothing.
     *   Otherwise, value of enforceAtHeight in database is updated and method returns OK_UPDATED.
     *
     * For performance reasons changes in database may not be immediately flushed to disk. @see Sync
     *
     * @throws dbwrapper_error If writing to database fails.
     *
     * @param confiscationTx Confiscation transaction
     * @param enforceAtHeight Minimum block height at which confiscation transaction can be spent.
     */
    auto WhitelistTx(std::int32_t enforceAtHeight, const CTransaction& confiscationTx) -> WhitelistTxResult;

    /**
     * Check if transaction with given id is whitelisted.
     *
     * @return true, if transaction is whitelisted and false otherwise.
     *
     * @param txid Id (hash) of confiscation transaction
     * @param[out] whitelistedTxData If transaction is whitelisted, data for whitelisted confiscation transaction is set in this variable.
     *                               If transaction is not whitelisted, value of this variable is left unchanged.
     *
     * @note The implementation always accesses the underlying database, which is assumed to provide suitable caching to increase performance.
     */
    [[nodiscard]] bool IsTxWhitelisted(const TxId& txid, WhitelistedTxData& whitelistedTxData);

    /**
     * Provides ability to iterate over whitelisted confiscation transactions
     */
    class WhitelistedTxIterator : public IteratorBase<2>
    {
    private:
        WhitelistedTxIterator(std::unique_ptr<CDBIterator>&& db_iter)
        : IteratorBase(std::move(db_iter))
        {}
        friend class CFrozenTXODB;

    public:
        /**
         * Return id and other data about whitelisted transaction to which the iterator currently points.
         *
         * Method returns a std::pair object with the following contents:
         *  - first: id of transaction
         *  - second: WhitelistedTxData
         *
         * @note This method must not be called if Valid() returns false.
         */
        auto GetWhitelistedTx() const -> std::pair<TxId, WhitelistedTxData>;
    };

    /**
     * Return iterator that can be used to get data about all whitelisted transactions currently stored in DB
     */
    auto QueryAllWhitelistedTxs() -> WhitelistedTxIterator;

    struct ClearWhitelistResult
    {
        unsigned int numFrozenBackToConsensus;
        unsigned int numUnwhitelistedTxs;
    };

    /**
     * Remove all confiscation transactions from whitelist and move all confiscated TXOs back to consensus blacklist.
     *
     * After the method completes, previously confiscated TXOs are again considered consensus frozen according to
     * consensus freeze intervals.
     *
     * @return number of TXOs that were moved back to consensus blacklist and number of un-whitelisted transactions
     */
    auto ClearWhitelist() -> ClearWhitelistResult;

    /**
     * Return value of max_WhitelistedTxData_enforceAtHeight
     */
    std::int32_t Get_max_WhitelistedTxData_enforceAtHeight();

private:
    // non-locking version of GetFrozenTXOData() used internally
    bool GetFrozenTXODataNL(const COutPoint& txo, FrozenTXOData& frozenTXOData);

    // non-locking version of IsTxWhitelisted() used internally
    bool IsTxWhitelistedNL(const TxId& txid, WhitelistedTxData& whitelistedTxData);

    // Update value of max_FrozenTXOData_enforceAtHeight_stop if max(frozenTXOData.enforceAtHeight.stop) is larger.
    void Update_max_FrozenTXOData_enforceAtHeight_stopNL(const FrozenTXOData& frozenTXOData);

    // Update value of max_WhitelistedTxData_enforceAtHeight if whitelistedTxData.enforceAtHeight is larger.
    void Update_max_WhitelistedTxData_enforceAtHeightNL(const WhitelistedTxData& whitelistedTxData);
};




#endif // BITCOIN_FROZENTXO_DB_H
