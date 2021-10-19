// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "frozentxo_db.h"

#include "serialize.h"

#include <boost/thread/locks.hpp>




CFrozenTXODB::CFrozenTXODB(std::size_t cache_size)
: db( GetDataDir() / "frozentxos", // fixed name of database directory
      cache_size, // use specified cache size
      false,      // do not use leveldb's memory environment
      false,      // do not remove all existing data
      false)      // do not store data obfuscated
, max_FrozenTXOData_enforceAtHeight_stop{std::numeric_limits<std::int32_t>::min()}
{
    // The following levelDB settings are needed and are assumed to be provided by wrapper class CDBWrapper:
    //  - create_if_missing = true;

    // Initialize value of member max_FrozenTXOData_enforceAtHeight_stop by iterating over all frozen TXO records in database
    for(auto it=QueryAllFrozenTXOs(); it.Valid(); it.Next())
    {
        Update_max_FrozenTXOData_enforceAtHeight_stopNL(it.GetFrozenTXO().second);
    }
}




namespace {

// Types needed to serialize/unserialize keys used to store data for frozen transactions.
// They provide support for storing data is several (logical) tables by prefixing key with
// record type.
// Templates are used to avoid code duplication.

// Used to distinguish between different types of records stored in DB.
// Semantically each type of record represents a separate logical table, but we store them all in one physical table.
// NOTE: Even though currently only one type of record is used, it must still be stored to allow adding another in the future.
enum class RecordType : std::uint8_t {
    TXO = 1
};

/**
 * Base class that provides serialization of keys prefixed with record type
 *
 * @param RT Record type that defines a logical table
 */
template<RecordType RT>
class OutKey
{
public:
    template<typename Stream>
    void Serialize(Stream &s) const
    {
        ::Serialize(s, static_cast<std::uint8_t>(RT));
    }
};

// Serializes key of a transaction output for frozen TXO that can be found by TXO
class OutKeyTXO : public OutKey<RecordType::TXO>
{
    const COutPoint& txo;
    using Base = OutKey<RecordType::TXO>;

public:
    explicit OutKeyTXO(const COutPoint& txo)
    : Base()
    , txo(txo)
    {}

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        Base::Serialize(s);
        s << this->txo;
    }
};


// Same as above, but used for unserialization.
// Serialization and unserialization functionality is split in separate classes to avoid having
// unneeded copies of objects that are only being serialized.


/**
* Base class that provides unserialization of keys prefixed with record type
*
* Value of record type is used to check if correct record was unserialized.
*
* @param RT Record type that defines a logical table
*/
template<RecordType RT>
class InKey
{
public:
    /**
    * This is set to true iff unserialization succeeded (i.e. if record type was as expected)
    *
    * Value is undefined until Unserialize() is called.
    */
    bool isValid;

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        std::uint8_t v;
        ::Unserialize(s, v);
        this->isValid = ( RecordType(v) == RT );
    }
};

class InKeyTXO : public InKey<RecordType::TXO>
{
    COutPoint& txo;
    using Base = InKey<RecordType::TXO>;

public:
    explicit InKeyTXO(COutPoint& txo)
    : txo(txo)
    {}

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        Base::Unserialize(s);
        if(!this->isValid)
        {
            return;
        }

        s >> this->txo;
    }
};


// Serialization of FrozenTXOData object to value stored in database
class OutValueFrozenTXOData
{
    const CFrozenTXODB::FrozenTXOData& frozenTXOData;

public:
    explicit OutValueFrozenTXOData(const CFrozenTXODB::FrozenTXOData& frozenTXOData)
    : frozenTXOData(frozenTXOData)
    {}

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        std::uint8_t black_list_and_policy_expires_flag = static_cast<std::uint8_t>(this->frozenTXOData.blacklist);
        assert(black_list_and_policy_expires_flag < 0x80);

        if(this->frozenTXOData.blacklist==CFrozenTXODB::FrozenTXOData::Blacklist::Consensus)
        {
            if(frozenTXOData.policyExpiresWithConsensus)
            {
                black_list_and_policy_expires_flag |= 0x80; // bit-7 is used to store value of 'policyExpiresWithConsensus'
            }

            // If TXO is on consensus blacklist, serialized data contains blacklist and additional consensus specific data
            s << black_list_and_policy_expires_flag;
            s << static_cast<const CFrozenTXODB::FrozenTXOData::EnforceAtHeightType::prevector&>(this->frozenTXOData.enforceAtHeight);
        }
        else
        {
            // If TXO is on policy-only blacklist, serialized data only contains blacklist
            s << black_list_and_policy_expires_flag;
        }
    }
};

// Unserialization of FrozenTXOData object from value stored in database
class InValueFrozenTXOData
{
    CFrozenTXODB::FrozenTXOData& frozenTXOData;

public:
    explicit InValueFrozenTXOData(CFrozenTXODB::FrozenTXOData& frozenTXOData)
    : frozenTXOData(frozenTXOData)
    {}

    template<typename Stream>
    void Unserialize(Stream &s) const
    {
        std::uint8_t black_list_and_policy_expires_flag;
        s >> black_list_and_policy_expires_flag;

        this->frozenTXOData.blacklist = static_cast<decltype(this->frozenTXOData.blacklist)>(black_list_and_policy_expires_flag & 0x7f);
        if(this->frozenTXOData.blacklist==CFrozenTXODB::FrozenTXOData::Blacklist::Consensus)
        {
            // Consensus specific data is only unserialized if TXO is on consensus blacklist
            this->frozenTXOData.policyExpiresWithConsensus = (black_list_and_policy_expires_flag & 0x80)!=0;
            s >> static_cast<CFrozenTXODB::FrozenTXOData::EnforceAtHeightType::prevector&>(this->frozenTXOData.enforceAtHeight);
        }

        // NOTE: If blacklist is PolicyOnly, values of other members in frozenTXOData are left unspecified since they are not applicable.
    }
};

} // anonymous namespace




auto CFrozenTXODB::FreezeTXOPolicyOnly(const COutPoint& txo) -> FreezeTXOResult
{
    // Lock db mutex for exclusive access
    auto lck = boost::unique_lock<boost::shared_mutex>(this->mtx_db);

    // Check if TXO is already frozen on some blacklist
    FrozenTXOData frozenTXOData_db = FrozenTXOData::Create_Uninitialized();
    bool is_already_frozen = this->GetFrozenTXODataNL(txo, frozenTXOData_db);

    if(!is_already_frozen)
    {
        // If this TXO is not already frozen, add new record.
        FrozenTXOData ftd_pol = FrozenTXOData::Create_Uninitialized();
        ftd_pol.blacklist = FrozenTXOData::Blacklist::PolicyOnly;
        this->db.Write(OutKeyTXO(txo), OutValueFrozenTXOData(ftd_pol), false); // NOTE: Call to Write() always returns true or throws in case of error
        return FreezeTXOResult::OK;
    }

    if(frozenTXOData_db.blacklist == FrozenTXOData::Blacklist::PolicyOnly)
    {
        // If this TXO is already frozen on policy blacklist, do nothing and report success.
        return FreezeTXOResult::OK_ALREADY_FROZEN;
    }

    // Existing record in database is in Consensus blacklist.
    // Automatically changing to PolicyOnly blacklist is not allowed.
    return FreezeTXOResult::ERROR_ALREADY_IN_CONSENSUS_BLACKLIST;
}

auto CFrozenTXODB::FreezeTXOConsensus(const COutPoint& txo, const FrozenTXOData::EnforceAtHeightType& enforceAtHeight, bool policyExpiresWithConsensus) -> FreezeTXOResult
{
    // Lock db mutex for exclusive access
    auto lck = boost::unique_lock<boost::shared_mutex>(this->mtx_db);

    // Check if TXO is already frozen on some blacklist
    FrozenTXOData frozenTXOData_db = FrozenTXOData::Create_Uninitialized();
    bool is_already_frozen = this->GetFrozenTXODataNL(txo, frozenTXOData_db);

    FrozenTXOData frozenTXOData = FrozenTXOData::Create_Uninitialized();
    frozenTXOData.blacklist = FrozenTXOData::Blacklist::Consensus;
    frozenTXOData.enforceAtHeight = enforceAtHeight;
    frozenTXOData.policyExpiresWithConsensus = policyExpiresWithConsensus;

    if(!is_already_frozen)
    {
        // If this TXO is not already frozen, add new record.
        this->db.Write(OutKeyTXO(txo), OutValueFrozenTXOData(frozenTXOData), false); // NOTE: Call to Write() always returns true or throws in case of error
        Update_max_FrozenTXOData_enforceAtHeight_stopNL(frozenTXOData);
        return FreezeTXOResult::OK;
    }

    if(frozenTXOData_db.blacklist == FrozenTXOData::Blacklist::PolicyOnly)
    {
        // Change blacklist on existing TXO record in database from policy to consensus.
        this->db.Write(OutKeyTXO(txo), OutValueFrozenTXOData(frozenTXOData), false); // write new value to the same key to update frozen TXO data
        Update_max_FrozenTXOData_enforceAtHeight_stopNL(frozenTXOData);
        return FreezeTXOResult::OK_UPDATED_TO_CONSENSUS_BLACKLIST;
    }

    // Existing record in database is already in Consensus blacklist.
    if(frozenTXOData_db.enforceAtHeight == enforceAtHeight &&
       frozenTXOData_db.policyExpiresWithConsensus == policyExpiresWithConsensus)
    {
        return FreezeTXOResult::OK_ALREADY_FROZEN;
    }

    // Update FrozenTXOData in database.
    frozenTXOData_db.enforceAtHeight = enforceAtHeight;
    frozenTXOData_db.policyExpiresWithConsensus = policyExpiresWithConsensus;
    this->db.Write(OutKeyTXO(txo), OutValueFrozenTXOData(frozenTXOData_db), false); // write new value to the same key to update frozen TXO data
    Update_max_FrozenTXOData_enforceAtHeight_stopNL(frozenTXOData);
    return FreezeTXOResult::OK_UPDATED;
}

auto CFrozenTXODB::UnfreezeTXOPolicyOnly(const COutPoint& txo) -> UnfreezeTXOResult
{
    // Lock db mutex for exclusive access
    auto lck = boost::unique_lock<boost::shared_mutex>(this->mtx_db);

    // Check if TXO is already frozen
    FrozenTXOData frozenTXOData_db = FrozenTXOData::Create_Uninitialized();
    bool is_already_frozen = this->GetFrozenTXODataNL(txo, frozenTXOData_db);

    if(!is_already_frozen)
    {
        return UnfreezeTXOResult::ERROR_TXO_NOT_FROZEN;
    }

    if(frozenTXOData_db.blacklist==FrozenTXOData::Blacklist::Consensus)
    {
        return UnfreezeTXOResult::ERROR_TXO_IS_IN_CONSENSUS_BLACKLIST;
    }

    // If TXO is currently frozen on policy-only blacklist, TXO record is removed
    this->db.Erase(OutKeyTXO(txo), false);

    return UnfreezeTXOResult::OK;
}

void CFrozenTXODB::Sync()
{
    this->db.Sync();
}

auto CFrozenTXODB::UnfreezeAll() -> UnfreezeAllResult
{
    UnfreezeAllResult res;
    res.numUnfrozenPolicyOnly = 0;
    res.numUnfrozenConsensus = 0;

    // Lock db mutex for exclusive access
    auto lck = boost::unique_lock<boost::shared_mutex>(this->mtx_db);

    // Use batch so that all frozen TXOs records are removed in one transaction
    CDBBatch batch(this->db);

    // Iterate over all frozen TXOs
    for(auto it=this->QueryAllFrozenTXOs(); it.Valid(); it.Next())
    {
        auto txo = it.GetFrozenTXO();
        if(txo.second.blacklist == FrozenTXOData::Blacklist::PolicyOnly)
        {
            ++res.numUnfrozenPolicyOnly;
        }
        else
        {
            ++res.numUnfrozenConsensus;
        }
        batch.Erase(OutKeyTXO(txo.first));
    }

    // Commit batch
    this->db.WriteBatch(batch, true); // NOTE: Call to WriteBatch() always returns true or throws in case of error

    max_FrozenTXOData_enforceAtHeight_stop = std::numeric_limits<std::int32_t>::min();

    return res;
}

auto CFrozenTXODB::CleanExpiredRecords(std::int32_t nHeight) -> CleanExpiredRecordsResult
{
    CleanExpiredRecordsResult res;
    res.numConsensusRemoved = 0;
    res.numConsensusUpdatedToPolicyOnly = 0;

    // Lock db mutex for exclusive access
    auto lck = boost::unique_lock<boost::shared_mutex>(this->mtx_db);

    // Use batch so that all records are removed/updated in one transaction
    CDBBatch batch(this->db);

    std::int32_t maxOverallStopHeight = std::numeric_limits<std::int32_t>::min();

    // Iterate over all frozen TXOs
    for(auto it=this->QueryAllFrozenTXOs(); it.Valid(); it.Next())
    {
        auto txo = it.GetFrozenTXO();
        if(txo.second.blacklist == FrozenTXOData::Blacklist::PolicyOnly)
        {
            // TXOs frozen on PolicyOnly blacklist never expire.
            continue;
        }

        // Find maximum value of stop in valid intervals
        std::int32_t max_valid_stop = std::numeric_limits<std::int32_t>::min();
        for(auto& i: txo.second.enforceAtHeight)
        {
            if(i.valid() && max_valid_stop<i.stop)
            {
                max_valid_stop = i.stop;
            }
        }

        if(max_valid_stop <= nHeight)
        {
            // This frozen TXO has expired
            if(txo.second.policyExpiresWithConsensus)
            {
                // If policy expires together with consensus, record can be removed
                batch.Erase(OutKeyTXO(txo.first));
                ++res.numConsensusRemoved;
            }
            else
            {
                // Otherwise TXO is updated to Policy
                FrozenTXOData ftd_pol = FrozenTXOData::Create_Uninitialized();
                ftd_pol.blacklist = FrozenTXOData::Blacklist::PolicyOnly;
                batch.Write(OutKeyTXO(txo.first), OutValueFrozenTXOData(ftd_pol));
                ++res.numConsensusUpdatedToPolicyOnly;
            }
        }
        else if(txo.second.policyExpiresWithConsensus &&
                max_valid_stop != std::numeric_limits<std::int32_t>::max() &&
                maxOverallStopHeight < max_valid_stop)
        {
            // If record was not erased, it still affects the value of max_FrozenTXOData_enforceAtHeight_stop
            // as long as policy blacklist expires with consensus and stop value is actually provided.
            maxOverallStopHeight = max_valid_stop;
        }
    }

    // Commit batch
    this->db.WriteBatch(batch, true); // NOTE: Call to WriteBatch() always returns true or throws in case of error

    // Reset max_FrozenTXOData_enforceAtHeight_stop to true max stop value we have calculated above
    // when we iterated over all records in database.
    max_FrozenTXOData_enforceAtHeight_stop = maxOverallStopHeight;

    return res;
}

bool CFrozenTXODB::GetFrozenTXODataNL(const COutPoint& txo, FrozenTXOData& frozenTXOData)
{
    InValueFrozenTXOData data{frozenTXOData};
    auto res = this->db.Read(OutKeyTXO(txo), data);
    return res;
}

bool CFrozenTXODB::GetFrozenTXOData(const COutPoint& txo, FrozenTXOData& frozenTXOData)
{
    // Lock db mutex for shared (readonly) access
    auto lck = boost::shared_lock<boost::shared_mutex>(this->mtx_db);

    return this->GetFrozenTXODataNL(txo, frozenTXOData);
}

auto CFrozenTXODB::FrozenTXOIterator::GetFrozenTXO() const -> std::pair<COutPoint, FrozenTXOData>
{
    // Check that value of record type corresponds to its enumerator.
    // RecordType is an implementation detail and should not be visible in public interface.
    // But to allow better inlining, its value must be known at compile-time. This check guarantees that
    // correct value is used without exposing the record type concept in public interface.
    static_assert(record_type == static_cast<std::uint8_t>(RecordType::TXO), "Invalid record type!");

    std::pair<COutPoint, FrozenTXOData> r({}, FrozenTXOData::Create_Uninitialized());

    InKeyTXO key(r.first);
    this->db_iter->GetKey(key);

    InValueFrozenTXOData value(r.second);
    this->db_iter->GetValue(value);

    return r;
}

auto CFrozenTXODB::QueryAllFrozenTXOs() -> FrozenTXOIterator
{
    auto db_iter = std::unique_ptr<CDBIterator>(this->db.NewIterator());
    return FrozenTXOIterator( std::move(db_iter) );
}

std::int32_t CFrozenTXODB::Get_max_FrozenTXOData_enforceAtHeight_stop()
{
    // Lock db mutex for shared (readonly) access
    auto lck = boost::shared_lock<boost::shared_mutex>(this->mtx_db);

    return this->max_FrozenTXOData_enforceAtHeight_stop;
}

void CFrozenTXODB::Update_max_FrozenTXOData_enforceAtHeight_stopNL(const FrozenTXOData& frozenTXOData)
{
    if(frozenTXOData.blacklist == FrozenTXOData::Blacklist::PolicyOnly)
    {
        // TXOs frozen on PolicyOnly blacklist never expire and have no effect on the value.
        return;
    }

    if(!frozenTXOData.policyExpiresWithConsensus)
    {
        // TXOs with policyExpiresWithConsensus=false are always frozen on policy blacklist and also have no effect on the value.
        return;
    }

    // Update previous max stop value if new max stop value is larger and applicable
    // (i.e. valid interval and stop height set).
    for(auto& hi: frozenTXOData.enforceAtHeight)
    {
        if(hi.valid() &&
           hi.stop != std::numeric_limits<std::int32_t>::max() &&
           hi.stop > max_FrozenTXOData_enforceAtHeight_stop)
        {
            // NOTE: Value is never decreased here.
            //       If an existing record for frozen TXO is updated by lowering the maximum value of
            //       stop height, this must be handled elsewhere, because a scan over all TXO records
            //       in database is required in general (this is done in CleanExpiredRecords()).
            //       Note that if the value stays too high, intended usage (i.e. updating mempool after
            //       reorg) still works correctly, just less optimal (i.e. we may iterate over whole
            //       mempool even if we didn't need to).
            max_FrozenTXOData_enforceAtHeight_stop = hi.stop;
        }
    }
}



namespace {

// Single instance of CFrozenTXODB object that is created by Init() method
std::unique_ptr<CFrozenTXODB> frozenTXODB;

}

void CFrozenTXODB::Init(std::size_t cache_size)
{
    if(frozenTXODB)
    {
        throw std::logic_error("Connection to FrozenTXODB has already been initialized!");
    }

    frozenTXODB.reset( new CFrozenTXODB(cache_size) );
}

CFrozenTXODB& CFrozenTXODB::Instance()
{
    return *frozenTXODB;
}

void CFrozenTXODB::Shutdown()
{
    frozenTXODB.reset();
}
