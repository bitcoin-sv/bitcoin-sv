// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "frozentxo_db.h"

#include "uint256.h"
#include "arith_uint256.h"

#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>

#include <random>
#include <set>
#include <thread>




BOOST_FIXTURE_TEST_SUITE(frozentxo, TestingSetup)


namespace {

class RandomTXOGenerator
{
    std::mt19937 engine;
    std::uniform_int_distribution<unsigned int> uniform_dist{0, 255};

public:
    void ResetSeed(decltype(engine)::result_type seed)
    {
        this->engine.seed(seed);
    }

    COutPoint GenerateRandomTXOId()
    {
        uint256 txid;
        for(auto& b: txid)
        {
            b = static_cast<std::uint8_t>(this->uniform_dist(this->engine));
        }
        return COutPoint(txid, this->uniform_dist(engine));
    };
};

} // anonymous namespace


//
// Performs tests on CFrozenTXODB class
//
BOOST_AUTO_TEST_CASE(db_tests)
{
    // Double initialization should throw an exception
    // Note that database was already initialized by TestingSetup constructor
    BOOST_REQUIRE_THROW( CFrozenTXODB::Init(0), std::logic_error );

    auto& db = CFrozenTXODB::Instance();

    COutPoint txo1(uint256S("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"), 123);
    const auto ftd_po = []{ // Convenient value of FrozenTXOData for TXOs frozen on PolicyOnly blacklist
        CFrozenTXODB::FrozenTXOData ftd = CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
        ftd.blacklist = CFrozenTXODB::FrozenTXOData::Blacklist::PolicyOnly;
        return ftd;
    }();

    COutPoint txo2(uint256S("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"), 456);
    const auto ftd_con = [](std::int32_t nHeight){ // Convenient value of FrozenTXOData for TXOs frozen on Consensus blacklist from given height
        CFrozenTXODB::FrozenTXOData ftd = CFrozenTXODB::FrozenTXOData::Create_Uninitialized();;
        ftd.blacklist = CFrozenTXODB::FrozenTXOData::Blacklist::Consensus;
        ftd.enforceAtHeight = { { nHeight } };
        ftd.policyExpiresWithConsensus = false;
        return ftd;
    };

    // FrozenTXOData with invalid value
    const auto ftd0 = []{
        CFrozenTXODB::FrozenTXOData ftd = CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
        ftd.blacklist = static_cast<CFrozenTXODB::FrozenTXOData::Blacklist>(0);
        return ftd;
    }();

    // Used to store FrozenTXOData object set by methods
    CFrozenTXODB::FrozenTXOData ftd = ftd0;


    // Check that FrozenTXOData is correctly considered frozen/unfrozen at specific heights
    auto test_heights = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 123, -1, -2, -3, -4, -5, -6, -7, -8, -9, -123};
    for(std::int32_t h: test_heights )
    {
        // TXO frozen on PolicyOnly blacklist must be considered frozen at any height
        BOOST_CHECK( ftd_po.IsFrozenOnPolicy(h) );

        // Check TXOs frozen on Consensus blacklist 
        for(std::int32_t h2: test_heights )
        {
            // Must be considered frozen on PolicyOnly at any height
            BOOST_CHECK( ftd_con(h).IsFrozenOnPolicy(h2) );

            if(h2<h)
            {
                // Must not be considered frozen on Consensus for heights before h
                BOOST_CHECK( !ftd_con(h).IsFrozenOnConsensus(h2) );
            }
            else
            {
                // Must be considered frozen on Consensus for heights h or after
                BOOST_CHECK( ftd_con(h).IsFrozenOnConsensus(h2) );
            }

            // If start >= stop, TXO must not be considered frozen on consensus at any height
            auto ftd = ftd_con(h);
            ftd.enforceAtHeight[0].stop = ftd.enforceAtHeight[0].start;
            BOOST_CHECK( !ftd.IsFrozenOnConsensus(h2) );
            BOOST_CHECK( ftd.IsFrozenOnPolicy(h2) ); // must still be considered frozen on policy ...
            ftd.policyExpiresWithConsensus = true;
            BOOST_CHECK( !ftd.IsFrozenOnPolicy(h2) ); // ... unless policy expires together with consensus

            // If start < stop, TXO must be considered frozen only at heights between start and stop
            ftd = ftd_con(h);
            ftd.enforceAtHeight[0].stop = ftd.enforceAtHeight[0].start+2;
            if(h2<h || h2>=ftd.enforceAtHeight[0].stop)
            {
                BOOST_CHECK( !ftd.IsFrozenOnConsensus(h2) );
            }
            else
            {
                BOOST_CHECK( ftd.IsFrozenOnConsensus(h2) );
            }

            if(h2>=ftd.enforceAtHeight[0].stop)
            {
                BOOST_CHECK( ftd.IsFrozenOnPolicy(h2) ); // must be considered frozen on policy after stop height ...
                ftd.policyExpiresWithConsensus = true;
                BOOST_CHECK( !ftd.IsFrozenOnPolicy(h2) ); // ... unless policy expires together with consensus
            }
            else
            {
                BOOST_CHECK( ftd.IsFrozenOnPolicy(h2) ); // must be considered frozen on policy before stop height ...
                ftd.policyExpiresWithConsensus = true;
                BOOST_CHECK( ftd.IsFrozenOnPolicy(h2) ); // ... even if policy expires together with consensus
            }

            // Check multiple consensus freeze intervals
            ftd = ftd_con(h);
            ftd.enforceAtHeight = {{h, h+2}, {h+4, h+6}, {h+5, h+7}, {h+8, h+8}}; // three valid intervals (two overlapping) and one ignored interval

            // Must be considered frozen on consensus only at heights [h,h+2) and [h+4,h+7).
            if( (h2>=h && h2<h+2) || (h2>=h+4 && h2<h+7) )
            {
                BOOST_CHECK( ftd.IsFrozenOnConsensus(h2) );
            }
            else
            {
                BOOST_CHECK( !ftd.IsFrozenOnConsensus(h2) );
            }

            if(h2>=h+7)
            {
                BOOST_CHECK( ftd.IsFrozenOnPolicy(h2) ); // must be considered frozen on policy after the end of last valid interval ...
                ftd.policyExpiresWithConsensus = true;
                BOOST_CHECK( !ftd.IsFrozenOnPolicy(h2) ); // ... unless policy expires together with consensus
            }
            else
            {
                BOOST_CHECK( ftd.IsFrozenOnPolicy(h2) ); // must be considered frozen on policy before the end of last valid interval ...
                ftd.policyExpiresWithConsensus = true;
                BOOST_CHECK( ftd.IsFrozenOnPolicy(h2) ); // ... even if policy expires together with consensus. note that this includes any gaps between intervals
            }
        }
    }

    // In empty DB txo1 must not be frozen and ftd must not be changed
    BOOST_CHECK( !db.GetFrozenTXOData(txo1, ftd) && ftd==ftd0 );


    // Add a new frozen TXO txo1 to DB
    BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::OK );

    // txo1 must now be frozen and correct data must be returned
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_po );

    // Freezing the same TXO again must do nothing
    BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN );
    ftd = ftd0;
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_po );

    // txo2 must not be frozen and ftd must remain unchanged if call to GetFrozenTXOData() returns false
    ftd = ftd0;
    BOOST_CHECK( !db.GetFrozenTXOData(txo2, ftd) && ftd==ftd0 );
    
    // Add a new consensus frozen TXO txo2 to DB
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK );

    // txo2 must now be frozen
    ftd=ftd0;
    BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd==ftd_con(0) );

    // txo1 must still be frozen
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_po );

    // Freezing the same TXO again must do nothing
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN );
    ftd = ftd0;
    BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd==ftd_con(0) );

    // Update blacklist on txo1 to consensus
    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED_TO_CONSENSUS_BLACKLIST );

    // txo1 must still be frozen and updated data must be returned
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_con(0) );

    // Updating from consensus to policyOnly is not allowed
    BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::ERROR_ALREADY_IN_CONSENSUS_BLACKLIST );

    // txo1 must still be frozen and original data must be returned
    ftd=ftd0;
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_con(0) );

    // Update start height on txo1
    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{2}}, false)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_con(2) );

    // Change enforceAtHeight to several intervals to check that it is correctly serialized
    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{2,3}, {4,5}}, false)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) );
    {
        auto ftd_chk=ftd_con(2);
        ftd_chk.enforceAtHeight = {{2,3}, {4,5}};
        BOOST_CHECK( ftd==ftd_chk );
    }
    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{4,5}, {-1,-1}, {2,3}}, true)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) );
    {
        auto ftd_chk=ftd_con(2);
        ftd_chk.enforceAtHeight = {{4,5}, {-1,-1},  {2,3}};
        ftd_chk.policyExpiresWithConsensus = true;
        BOOST_CHECK( ftd==ftd_chk );
    }

    // Update start height on txo1 back to 0 as is expected for next steps
    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_con(0) );




    // Unfreezing TXO that is in Consensus from PolicyOnly is not allowed
    BOOST_CHECK( db.UnfreezeTXOPolicyOnly(txo1)==CFrozenTXODB::UnfreezeTXOResult::ERROR_TXO_IS_IN_CONSENSUS_BLACKLIST );
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_con(0) );

    // Unfreeze txo1 from Consensus at height 2 but keep it frozen on PolicyOnly
    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{0,2}}, false)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );

    // txo1 must still be stored on consensus blacklist with stop height set accordingly
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd.blacklist == CFrozenTXODB::FrozenTXOData::Blacklist::Consensus );
    BOOST_CHECK( ftd.enforceAtHeight[0].stop==2 );
    BOOST_CHECK( ftd.policyExpiresWithConsensus==false );

    // Unfreeze txo2 from consensus and policy at height 3
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{0,3}}, true)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );

    // txo2 must still be stored on consensus blacklist with stop height set accordingly
    BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.blacklist == CFrozenTXODB::FrozenTXOData::Blacklist::Consensus );
    BOOST_CHECK( ftd.enforceAtHeight[0].stop==3 );
    BOOST_CHECK( ftd.policyExpiresWithConsensus==true );

    {
        // Remove all TXO records and add a new one needed for next check
        auto res = db.UnfreezeAll();
        BOOST_CHECK( res.numUnfrozenPolicyOnly==0 );
        BOOST_CHECK( res.numUnfrozenConsensus==2 );
        BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::OK );
    }

    // Unfreezing TXO that is currently in PolicyOnly removes record from DB
    BOOST_CHECK( db.UnfreezeTXOPolicyOnly(txo1)==CFrozenTXODB::UnfreezeTXOResult::OK );
    BOOST_CHECK( !db.GetFrozenTXOData(txo1, ftd) );

    // Trying to unfreeze TXO that is not frozen is not allowed
    BOOST_CHECK( db.UnfreezeTXOPolicyOnly(txo1)==CFrozenTXODB::UnfreezeTXOResult::ERROR_TXO_NOT_FROZEN );




    // Check cleaning expired records from DB.
    // Note that at this point, there must be no records in DB.
    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{1,2}}, true)==CFrozenTXODB::FreezeTXOResult::OK );
    {
        // Must not be considered expired at height 0.
        auto res = db.CleanExpiredRecords(0); 
        BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && res.numConsensusRemoved==0 && res.numConsensusUpdatedToPolicyOnly==0 );
        // Must not be considered expired at height 1.
        res = db.CleanExpiredRecords(1);
        BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && res.numConsensusRemoved==0 && res.numConsensusUpdatedToPolicyOnly==0 );
        // Must be considered expired and removed at height 2.
        res = db.CleanExpiredRecords(2);
        BOOST_CHECK( !db.GetFrozenTXOData(txo1, ftd) && res.numConsensusRemoved==1 && res.numConsensusUpdatedToPolicyOnly==0 );
    }

    BOOST_CHECK( db.FreezeTXOConsensus(txo1, {{1,2}}, false)==CFrozenTXODB::FreezeTXOResult::OK );
    {
        // Must not be considered expired at height 0.
        auto res = db.CleanExpiredRecords(0); 
        BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && res.numConsensusRemoved==0 && res.numConsensusUpdatedToPolicyOnly==0 );
        // Must not be considered expired at height 1.
        res = db.CleanExpiredRecords(1);
        BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && res.numConsensusRemoved==0 && res.numConsensusUpdatedToPolicyOnly==0 );
        // Must be considered expired and updated to policy at height 2.
        res = db.CleanExpiredRecords(2);
        BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd==ftd_po && res.numConsensusRemoved==0 && res.numConsensusUpdatedToPolicyOnly==1 );
    }

    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{1,1}}, true)==CFrozenTXODB::FreezeTXOResult::OK );
    {
        // Must be considered expired and removed at any height.
        auto res = db.CleanExpiredRecords(1);
        BOOST_CHECK( !db.GetFrozenTXOData(txo2, ftd) && res.numConsensusRemoved==1 && res.numConsensusUpdatedToPolicyOnly==0 );
    }

    // Remove remaining txo1 record because next step expects empty DB
    BOOST_CHECK( db.UnfreezeTXOPolicyOnly(txo1)==CFrozenTXODB::UnfreezeTXOResult::OK );




    // Iterator must not be valid if there are no frozen TXOs
    BOOST_CHECK( !db.QueryAllFrozenTXOs().Valid() );

    RandomTXOGenerator rtg;

    using clock = std::chrono::steady_clock;

    // Add some TXOs to check iteration and bulk operations
    rtg.ResetSeed(1); // fixed seed is used so that results are always the same
    const unsigned int N = 10000;
    const unsigned int N2 = N/2;
    auto tp0=clock::now();
    const arith_uint256 txid_sum = [&]{
        arith_uint256 ts;
        for(unsigned int i=0; i<N; ++i)
        {
            auto txoid = rtg.GenerateRandomTXOId();
            BOOST_CHECK( db.FreezeTXOPolicyOnly(txoid)==CFrozenTXODB::FreezeTXOResult::OK );

            // update sum that will later be used to check if all TXOs are returned by iteration
            ts += UintToArith256(txoid.GetTxId());
            ts += txoid.GetN();
        }
        return ts;
    }();
    db.Sync();
    BOOST_TEST_MESSAGE( "Freezing "+std::to_string(N)+ " TXOs took "+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now()-tp0 ).count())+"ms" );

    // Check that all added TXO are frozen and time the result
    rtg.ResetSeed(1); // setting the same seed as above so that random generator will once again generate same sequence of COutPoint's as in the previous run
    tp0=clock::now();
    for(unsigned int i=0; i<N; ++i)
    {
        auto txoid = rtg.GenerateRandomTXOId();
        BOOST_CHECK( db.GetFrozenTXOData(txoid, ftd) && ftd.IsFrozenOnPolicy(0) );
    }
    BOOST_TEST_MESSAGE( "Checking that TXO is frozen "+std::to_string(N)+ " times took "+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now()-tp0 ).count())+"ms" );

    tp0=clock::now();
    for(unsigned int i=0; i<N; ++i)
    {
        auto txoid = rtg.GenerateRandomTXOId(); // since seed was not reset, new COutPoint's are generated that are not frozen
        BOOST_CHECK( !db.GetFrozenTXOData(txoid, ftd) );
    }
    BOOST_TEST_MESSAGE( "Checking that TXO is not frozen "+std::to_string(N)+ " times took "+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now()-tp0 ).count())+"ms" );

    // Next random TXO should not be frozen
    BOOST_CHECK( !db.GetFrozenTXOData(rtg.GenerateRandomTXOId(), ftd) );

    // Check iteration over all frozen TXOs
    std::set<COutPoint> txoid_front;
    COutPoint txoid_last;
    unsigned int cnt=0;
    arith_uint256 txid_sum_chk = 0;
    for(auto it=db.QueryAllFrozenTXOs(); it.Valid(); it.Next())
    {
        auto t = it.GetFrozenTXO();
        BOOST_CHECK( t.second == ftd_po );

        // Remember first N2 and the last TXO id as they are stored in DB so that can be used in next test
        if(cnt<N2)
        {
            txoid_front.insert(t.first);
        }
        else if(cnt+1==N)
        {
            txoid_last = t.first;
        }

        ++cnt;
        txid_sum_chk += UintToArith256(t.first.GetTxId());
        txid_sum_chk += t.first.GetN();
    }
    BOOST_CHECK( cnt==N );
    BOOST_CHECK( txid_sum_chk==txid_sum );
    BOOST_CHECK( txoid_front.size()==N2 );

    // Check that modifying DB while iterating produces predictable results
    cnt=0;
    txid_sum_chk = 0;
    for(auto it=db.QueryAllFrozenTXOs(); it.Valid(); it.Next())
    {
        if(cnt==2)
        {
            // Modify DB by unfreezing some TXOs before and after current position
            // In total N2+1 TXOs are unfrozen.
            // NOTE: This is done from another thread because this is a typical scenario
            std::thread thd([&db, &txoid_front, &txoid_last]{
                for(auto& txoid: txoid_front)
                {
                    BOOST_CHECK( db.UnfreezeTXOPolicyOnly(txoid)==CFrozenTXODB::UnfreezeTXOResult::OK );
                }
                BOOST_CHECK( db.UnfreezeTXOPolicyOnly(txoid_last)==CFrozenTXODB::UnfreezeTXOResult::OK );

                // Check that records are actually removed from DB

                CFrozenTXODB::FrozenTXOData ftd = CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
                for(auto& txoid: txoid_front)
                {
                    BOOST_CHECK( !db.GetFrozenTXOData(txoid, ftd) );
                }
                BOOST_CHECK( !db.GetFrozenTXOData(txoid_last, ftd) );
            });
            thd.join();
        }

        // Even if some records were removed above in a separate thread, we should still get all of them when using iterator that was created before
        auto t = it.GetFrozenTXO();
        BOOST_CHECK( t.second == ftd_po );

        ++cnt;
        txid_sum_chk += UintToArith256(t.first.GetTxId());
        txid_sum_chk += t.first.GetN();
    }
    db.Sync();
    BOOST_CHECK( cnt==N );
    BOOST_CHECK( txid_sum_chk==txid_sum );

    // Unfreeze all TXOs that are still frozen
    rtg.ResetSeed(1);
    unsigned int N_removed = 0;
    for(unsigned int i=0; i<N; ++i)
    {
        auto txoid = rtg.GenerateRandomTXOId();
        auto expected_result = CFrozenTXODB::UnfreezeTXOResult::OK;
        if(txoid_front.count(txoid)!=0 || txoid==txoid_last)
        {
            expected_result = CFrozenTXODB::UnfreezeTXOResult::ERROR_TXO_NOT_FROZEN;
        }
        else
        {
            ++N_removed;
        }
        BOOST_CHECK( db.UnfreezeTXOPolicyOnly(txoid)==expected_result );
    }
    db.Sync();
    BOOST_CHECK( N2 + N_removed + 1 == N );
    BOOST_CHECK( !db.QueryAllFrozenTXOs().Valid() );

    // Check UnfreezeAll() method
    BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::OK );
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK );
    BOOST_CHECK( db.FreezeTXOConsensus(COutPoint(uint256S("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"), 0), {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK );
    auto res = db.UnfreezeAll();
    BOOST_CHECK( res.numUnfrozenPolicyOnly==1 );
    BOOST_CHECK( res.numUnfrozenConsensus==2 );
    BOOST_CHECK( !db.QueryAllFrozenTXOs().Valid() );
    res = db.UnfreezeAll(); // running on empty db should do nothing
    BOOST_CHECK( res.numUnfrozenPolicyOnly==0 );
    BOOST_CHECK( res.numUnfrozenConsensus==0 );
}


namespace {
// Helper to run function in two threads.
template<typename Cnt, typename F>
void run_in_two_threads(Cnt (&cnt)[2], F f)
{
    cnt[0] = {};
    std::thread thd(f, &cnt[0]);

    cnt[1] = {};
    f(&cnt[1]);

    thd.join();
}
}

//
// Performs thread-safety tests on CFrozenTXODB class
//
// Checking if TXO is frozen must work correctly even if TXO is being frozen/unfrozen at the same time.
// Freezing/unfreezing TXO must work correctly even if it done from several threads at the same time.
//
BOOST_AUTO_TEST_CASE(db_thread_safety_tests)
{
    auto& db = CFrozenTXODB::Instance();

    // Generate array with TXO ids that will be used in the test
    const auto txo_array = []{
        const std::size_t N = 10000;
        std::vector<COutPoint> txo_array(N);

        RandomTXOGenerator rtg;
        rtg.ResetSeed(1); // fixed seed is used so that results are always the same
        for(auto& txo: txo_array)
        {
            txo = rtg.GenerateRandomTXOId();
        }

        return txo_array;
    }();

    std::atomic_bool should_quit(false); // if set to true, thread should quit
    struct CheckerData
    {
        std::size_t num_frozen = -1; // number of frozen TXOs after final loop
    };
    CheckerData policy;
    CheckerData consensus;

    // Helper that continuously checks all TXOs in array and returns when should_quit is true.
    auto frozen_txo_checker = [&](std::int32_t height){
        for(std::size_t i = 0; ; ++i) // NOTE: This loop typically finishes after only two iterations because writing thread takes most of the time.
        {
            // Check quit signal so that we can do one more loop before quitting
            bool should_quit_after_loop = should_quit;

            std::size_t num_policy = 0;
            std::size_t num_consensus = 0;
            for(auto& txo: txo_array)
            {
                CFrozenTXODB::FrozenTXOData ftd = CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
                if(!db.GetFrozenTXOData(txo, ftd))
                {
                    continue;
                }
                if(ftd.IsFrozenOnPolicy(height))
                {
                    ++num_policy;
                }
                if(ftd.IsFrozenOnConsensus(height))
                {
                    ++num_consensus;
                }
            }

            if(should_quit_after_loop)
            {
                // Report number of TXOs that were found frozen in final loop.
                policy.num_frozen = num_policy;
                consensus.num_frozen = num_consensus;
                break;
            }
        }
    };

    // Thread that will check for frozen TXOs
    std::thread thd_checker;
    auto start_frozen_txo_checker = [&](std::int32_t height) {
        policy = CheckerData();
        consensus = CheckerData();
        should_quit = false;
        thd_checker = std::thread(frozen_txo_checker, height);
    };
    auto stop_frozen_txo_checker = [&]{
        should_quit = true;
        thd_checker.join();
    };

    // Provides counters that are used to check if function completed successfully
    struct Cnt 
    {
        std::size_t ok = 0;
        std::size_t alt = 0;
    } cnt[2];

    // Freeze all TXOs on policy-only blacklist while checking if they are frozen
    start_frozen_txo_checker(0);
    run_in_two_threads(cnt, [&](Cnt* cnt){
        for(auto& txo: txo_array)
        {
            auto res = db.FreezeTXOPolicyOnly(txo);
            if(res == CFrozenTXODB::FreezeTXOResult::OK)
            {
                ++cnt->ok;
            }
            else if(res == CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN)
            {
                ++cnt->alt;
            }
        }
    });
    stop_frozen_txo_checker();
    db.Sync();
    BOOST_CHECK_EQUAL( policy.num_frozen, txo_array.size() );
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0 );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[0].alt, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[1].ok + cnt[1].alt, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[1].ok, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].alt + cnt[1].alt, txo_array.size() );

    // Freeze all TXOs on consensus blacklist at height 10 while checking if they are frozen
    start_frozen_txo_checker(10);
    run_in_two_threads(cnt, [&](Cnt* cnt){
        for(auto& txo: txo_array)
        {
            auto res = db.FreezeTXOConsensus(txo, {{10}}, false);
            if(res == CFrozenTXODB::FreezeTXOResult::OK_UPDATED_TO_CONSENSUS_BLACKLIST)
            {
                ++cnt->ok;
            }
            else if(res == CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN)
            {
                ++cnt->alt;
            }
        }
    });
    stop_frozen_txo_checker();
    db.Sync();
    BOOST_CHECK_EQUAL( policy.num_frozen, txo_array.size() );
    BOOST_CHECK_EQUAL( consensus.num_frozen, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[0].alt, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[1].ok + cnt[1].alt, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[1].ok, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].alt + cnt[1].alt, txo_array.size() );

    // Unfreeze all TXOs on consensus blacklist at height 20 while checking if they are frozen
    start_frozen_txo_checker(20);
    run_in_two_threads(cnt, [&](Cnt* cnt){
        for(auto& txo: txo_array)
        {
            auto res = db.FreezeTXOConsensus(txo, {{10,20}}, false);
            if(res == CFrozenTXODB::FreezeTXOResult::OK_UPDATED)
            {
                ++cnt->ok;
            }
            else if(res == CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN)
            {
                ++cnt->alt;
            }
        }
    });
    stop_frozen_txo_checker();
    db.Sync();
    BOOST_CHECK_EQUAL( policy.num_frozen, txo_array.size() );
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0 );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[0].alt, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[1].ok + cnt[1].alt, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[1].ok, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].alt + cnt[1].alt, txo_array.size() );

    // Clear expired records (update to policy) while checking if they are frozen
    start_frozen_txo_checker(20);
    run_in_two_threads(cnt, [&](Cnt* cnt){
        auto res = db.CleanExpiredRecords(20);
        cnt->ok = res.numConsensusUpdatedToPolicyOnly;
        cnt->alt = res.numConsensusRemoved;
    });
    stop_frozen_txo_checker();
    db.Sync();
    BOOST_CHECK_EQUAL( policy.num_frozen, txo_array.size() );
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0 );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[1].ok, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].alt, 0 );
    BOOST_CHECK_EQUAL( cnt[1].alt, 0 );

    // Remove all frozen TXO records while checking if they are frozen
    start_frozen_txo_checker(20);
    run_in_two_threads(cnt, [&](Cnt* cnt){
        auto res = db.UnfreezeAll();
        cnt->ok = res.numUnfrozenPolicyOnly;
        cnt->alt = res.numUnfrozenConsensus;
    });
    stop_frozen_txo_checker();
    db.Sync();
    BOOST_CHECK_EQUAL( policy.num_frozen, 0 );
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0 );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[1].ok, txo_array.size());
    BOOST_CHECK_EQUAL( cnt[0].alt, 0);
    BOOST_CHECK_EQUAL( cnt[1].alt, 0);
}


BOOST_AUTO_TEST_SUITE_END()
