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

    // Check whitelisting confiscation transactions
    auto whitelistedTxData = CFrozenTXODB::WhitelistedTxData::Create_Uninitialized();

    // Frozen TXOs used by whitelisting tests
    BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::OK );
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{100,200}}, false)==CFrozenTXODB::FreezeTXOResult::OK );
    COutPoint txo3(uint256S("cbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcbcb"), 123);
    BOOST_CHECK( db.FreezeTXOConsensus(txo3, {{300,700}}, false)==CFrozenTXODB::FreezeTXOResult::OK );

    // Helper to create transactions used by whitelisting tests
    auto createCTX = [](const COutPoint& txo, std::uint8_t order_id=0){
        CMutableTransaction ctx;
        ctx.vin.resize(1);
        ctx.vin[0].prevout = txo;
        ctx.vin[0].scriptSig = CScript();
        ctx.vout.resize(2);
        ctx.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << std::vector<std::uint8_t>{'c','f','t','x'} << std::vector<std::uint8_t>{1, order_id,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        ctx.vout[0].nValue = Amount(0);
        ctx.vout[1].scriptPubKey = CScript() << OP_TRUE;
        ctx.vout[1].nValue = Amount(42);
        return CTransaction(ctx);
    };

    // Transactions used by whitelisting tests
    auto ctx1 = createCTX(txo2);
    BOOST_REQUIRE(CFrozenTXODB::IsConfiscationTx(ctx1));
    BOOST_REQUIRE(CFrozenTXODB::ValidateConfiscationTxContents(ctx1));
    auto ctx2 = createCTX(txo3);
    BOOST_REQUIRE(CFrozenTXODB::IsConfiscationTx(ctx2));
    BOOST_REQUIRE(CFrozenTXODB::ValidateConfiscationTxContents(ctx2));
    auto ctx3 = createCTX(txo3, 123); // spends the same input as ctx2
    BOOST_REQUIRE(ctx3.GetId() != ctx2.GetId());
    BOOST_REQUIRE(CFrozenTXODB::IsConfiscationTx(ctx2));
    BOOST_REQUIRE(CFrozenTXODB::ValidateConfiscationTxContents(ctx2));
    auto not_ctx1 = [&]{
        CMutableTransaction tx = createCTX(txo1);
        tx.vout[0].scriptPubKey = CScript() << OP_TRUE; // non OP_RETURN first input
        return CTransaction(tx);
    }();
    BOOST_REQUIRE(!CFrozenTXODB::IsConfiscationTx(not_ctx1));
    auto not_ctx2 = [&]{
        CMutableTransaction tx = createCTX(txo1);
        tx.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << std::vector<std::uint8_t>{'X','X','X','X'}; // invalid protocol id
        return CTransaction(tx);
    }();
    BOOST_REQUIRE(!CFrozenTXODB::IsConfiscationTx(not_ctx2));
    auto inv_ctx1 = [&]{
        CMutableTransaction tx = createCTX(txo1);
        tx.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << std::vector<std::uint8_t>{'c','f','t','x'}; // missing confiscation order hash
        return CTransaction(tx);
    }();
    BOOST_REQUIRE(CFrozenTXODB::IsConfiscationTx(inv_ctx1));
    BOOST_REQUIRE(!CFrozenTXODB::ValidateConfiscationTxContents(inv_ctx1));
    auto inv_ctx2 = [&]{
        CMutableTransaction tx = createCTX(txo1);
        tx.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << std::vector<std::uint8_t>{'c','f','t','x'} << std::vector<std::uint8_t>{0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; // 0 is invalid version
        return CTransaction(tx);
    }();
    BOOST_REQUIRE(CFrozenTXODB::IsConfiscationTx(inv_ctx2));
    BOOST_REQUIRE(!CFrozenTXODB::ValidateConfiscationTxContents(inv_ctx2));


    BOOST_CHECK( !db.QueryAllWhitelistedTxs().Valid() ); // Initially no txs are whitelisted txs
    BOOST_CHECK( !db.IsTxWhitelisted(ctx1.GetId(), whitelistedTxData) );

    BOOST_CHECK( db.WhitelistTx(50, not_ctx1) == CFrozenTXODB::WhitelistTxResult::ERROR_NOT_VALID );
    BOOST_CHECK( db.WhitelistTx(50, not_ctx2) == CFrozenTXODB::WhitelistTxResult::ERROR_NOT_VALID );
    BOOST_CHECK( db.WhitelistTx(50, inv_ctx1) == CFrozenTXODB::WhitelistTxResult::ERROR_NOT_VALID );
    BOOST_CHECK( db.WhitelistTx(50, inv_ctx2) == CFrozenTXODB::WhitelistTxResult::ERROR_NOT_VALID );

    BOOST_CHECK( db.WhitelistTx(50, createCTX(COutPoint(uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"), 0))) == CFrozenTXODB::WhitelistTxResult::ERROR_TXO_NOT_CONSENSUS_FROZEN ); // cannot whitelist a tx confiscating a TXO that is not frozen
    BOOST_CHECK( db.WhitelistTx(50, createCTX(txo1)) == CFrozenTXODB::WhitelistTxResult::ERROR_TXO_NOT_CONSENSUS_FROZEN ); // cannot whitelist a tx confiscating a TXO that is not consensus frozen
    BOOST_CHECK( db.WhitelistTx(99, createCTX(txo2)) == CFrozenTXODB::WhitelistTxResult::ERROR_TXO_NOT_CONSENSUS_FROZEN ); // cannot whitelist a tx confiscating a TXO that is not considered consensus frozen at enforceAtHeight
    BOOST_CHECK( db.WhitelistTx(322, createCTX(txo2)) == CFrozenTXODB::WhitelistTxResult::ERROR_TXO_NOT_CONSENSUS_FROZEN );

    // Update freeze interval
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{100,400}}, true)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );

    for(int i=0; i<3; ++i)
    {
        BOOST_CHECK( db.WhitelistTx(322, ctx1) == CFrozenTXODB::WhitelistTxResult::OK ); // Whitelisting a previously unknown tx confiscating a TXO that is considered consensus frozen at enforceAtHeight must succeed
        BOOST_CHECK( db.IsTxWhitelisted(ctx1.GetId(), whitelistedTxData) && whitelistedTxData.enforceAtHeight==322 && whitelistedTxData.confiscatedTXOs==std::vector<COutPoint>{txo2} ); // This tx must now be whitelisted
        BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.IsFrozenOnConsensus(50) && ftd.IsFrozenOnConsensus(450) && ftd.IsFrozenOnConsensus(200) ); // Confiscated TXO must be consensus frozen at all heights
        BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.IsFrozenOnPolicy(50) && ftd.IsFrozenOnPolicy(450) && ftd.IsFrozenOnPolicy(200) ); // Confiscated TXO must be policy frozen at all heights

        if(i==0)
        {
            // Check clearing all whitelisted transactions
            auto res = db.ClearWhitelist();
            BOOST_CHECK( res.numUnwhitelistedTxs==1 && res.numFrozenBackToConsensus==1 );
            BOOST_CHECK( !db.IsTxWhitelisted(ctx1.GetId(), whitelistedTxData) ); // This tx must not be whitelisted
            // Previously confiscated TXOs must again be consensus frozen according to specified interval
            BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && !ftd.IsFrozenOnConsensus(50) && !ftd.IsFrozenOnConsensus(450) && ftd.IsFrozenOnConsensus(200) );
            BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.IsFrozenOnPolicy(50) && !ftd.IsFrozenOnPolicy(450) && ftd.IsFrozenOnPolicy(200) );

            // Running it again should have no effect
            res = db.ClearWhitelist();
            BOOST_CHECK( res.numUnwhitelistedTxs==0 && res.numFrozenBackToConsensus==0 );
        }

        if(i==1)
        {
            // Check that consensus freeze intervals can be updated while TXOs are confiscated
            BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{50,150}}, true)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );
            auto res = db.ClearWhitelist();
            BOOST_CHECK( res.numUnwhitelistedTxs==1 && res.numFrozenBackToConsensus==1 );
            BOOST_CHECK( !db.IsTxWhitelisted(ctx1.GetId(), whitelistedTxData) ); // This tx must not be whitelisted
            // Previously confiscated TXOs must be consensus frozen according to updated interval
            BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.IsFrozenOnConsensus(50) && !ftd.IsFrozenOnConsensus(450) && !ftd.IsFrozenOnConsensus(200) && ftd.IsFrozenOnConsensus(149) && !ftd.IsFrozenOnConsensus(49) && !ftd.IsFrozenOnConsensus(150) );
            BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.IsFrozenOnPolicy(50) && !ftd.IsFrozenOnPolicy(450) && !ftd.IsFrozenOnPolicy(200) && ftd.IsFrozenOnPolicy(149) && ftd.IsFrozenOnPolicy(49) && !ftd.IsFrozenOnPolicy(150));

            // Restore freeze interval to previous value
            BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{100,400}}, true)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );
        }
    }

    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {}, true)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED ); // Confiscated TXO must remain frozen even if consensus freeze intervals are updated
    BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.IsFrozenOnConsensus(50) && ftd.IsFrozenOnConsensus(450) && ftd.IsFrozenOnConsensus(200) );
    BOOST_CHECK( db.GetFrozenTXOData(txo2, ftd) && ftd.IsFrozenOnPolicy(50) && ftd.IsFrozenOnPolicy(450) && ftd.IsFrozenOnPolicy(200) );

    BOOST_CHECK( db.WhitelistTx(321, ctx1) == CFrozenTXODB::WhitelistTxResult::OK_UPDATED ); // Whitelisting the tx again with lower enforceAtHeight must update the record in database
    BOOST_CHECK( db.IsTxWhitelisted(ctx1.GetId(), whitelistedTxData) && whitelistedTxData.enforceAtHeight==321 && whitelistedTxData.confiscatedTXOs==std::vector<COutPoint>{txo2} ); // This tx must now be whitelisted with lower enforceAtHeight

    BOOST_CHECK( db.WhitelistTx(321, ctx1) == CFrozenTXODB::WhitelistTxResult::OK ); // Whitelisting the tx again with the same data has no effect
    BOOST_CHECK( db.WhitelistTx(654, ctx1) == CFrozenTXODB::WhitelistTxResult::OK_ALREADY_WHITELISTED_AT_LOWER_HEIGHT ); // Whitelisting the tx again with higher enforceAtHeight has no effect
    BOOST_CHECK( db.IsTxWhitelisted(ctx1.GetId(), whitelistedTxData) && whitelistedTxData.enforceAtHeight==321 && whitelistedTxData.confiscatedTXOs==std::vector<COutPoint>{txo2} ); // Additional whitelisting with higher enforceAtHeight must have no effect on already whitelisted tx.

    BOOST_CHECK( db.WhitelistTx(654, ctx2) == CFrozenTXODB::WhitelistTxResult::OK ); // Whitelist another tx
    BOOST_CHECK( db.IsTxWhitelisted(ctx1.GetId(), whitelistedTxData) && whitelistedTxData.enforceAtHeight==321 && whitelistedTxData.confiscatedTXOs==std::vector<COutPoint>{txo2} ); // Both must now be whitelisted
    BOOST_CHECK( db.IsTxWhitelisted(ctx2.GetId(), whitelistedTxData) && whitelistedTxData.enforceAtHeight==654 && whitelistedTxData.confiscatedTXOs==std::vector<COutPoint>{txo3} );

    // Whitelisting the tx again with lower enforceAtHeight, which is before the TXO was initially considered consensus frozen,
    // must also update the record in db, because TXO is now on Confiscation blacklist and frozen at all heights.
    BOOST_CHECK( db.WhitelistTx(50, ctx1) == CFrozenTXODB::WhitelistTxResult::OK_UPDATED );

    // Whitelisting a tx that spends already confiscated input must succeed
    BOOST_CHECK( db.WhitelistTx(70, ctx3) == CFrozenTXODB::WhitelistTxResult::OK );

    cnt = 0;
    BOOST_CHECK( db.FreezeTXOConsensus(COutPoint(ctx2.GetId(), 0), {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK );// A TXO record in database must not interfere with iteration of whitelisted tx
    for(auto it=db.QueryAllWhitelistedTxs(); it.Valid(); it.Next(), ++cnt) // Check iteration
    {
        auto t = it.GetWhitelistedTx();
        if(t.first == ctx1.GetId() && t.second.enforceAtHeight == 50 && t.second.confiscatedTXOs == std::vector<COutPoint>{txo2})
        {
            continue;
        }
        else if(t.first == ctx2.GetId() && t.second.enforceAtHeight == 654 && t.second.confiscatedTXOs == std::vector<COutPoint>{txo3})
        {
            continue;
        }
        else if(t.first == ctx3.GetId() && t.second.enforceAtHeight == 70 && t.second.confiscatedTXOs == std::vector<COutPoint>{txo3})
        {
            continue;
        }
        else
        {
            BOOST_TEST_ERROR("Unexpected tx during Iteration over whitelisted txs!");
        }
    }
    BOOST_CHECK( cnt == 3 );

    // Check UnfreezeAll() method
    BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN );
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK_UPDATED );
    BOOST_CHECK( db.FreezeTXOConsensus(COutPoint(uint256S("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"), 0), {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK );
    auto res = db.UnfreezeAll();
    BOOST_CHECK( res.numUnfrozenPolicyOnly==1 );
    BOOST_CHECK( res.numUnfrozenConsensus==4 );
    BOOST_CHECK( res.numUnwhitelistedTxs==3 );
    BOOST_CHECK( !db.QueryAllFrozenTXOs().Valid() );
    BOOST_CHECK( !db.QueryAllWhitelistedTxs().Valid() );
    res = db.UnfreezeAll(); // running on empty db should do nothing
    BOOST_CHECK( res.numUnfrozenPolicyOnly==0 );
    BOOST_CHECK( res.numUnfrozenConsensus==0 );
    BOOST_CHECK( res.numUnwhitelistedTxs==0 );

    // Check UnfreezeAll(true) method
    BOOST_CHECK( db.FreezeTXOPolicyOnly(txo1)==CFrozenTXODB::FreezeTXOResult::OK );
    BOOST_CHECK( db.FreezeTXOConsensus(txo2, {{0}}, true)==CFrozenTXODB::FreezeTXOResult::OK );
    BOOST_CHECK( db.FreezeTXOConsensus(COutPoint(uint256S("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"), 0), {{0}}, false)==CFrozenTXODB::FreezeTXOResult::OK );
    BOOST_CHECK( db.WhitelistTx(322, ctx1) == CFrozenTXODB::WhitelistTxResult::OK );
    res = db.UnfreezeAll(true);
    BOOST_CHECK( res.numUnfrozenPolicyOnly==0 );
    BOOST_CHECK( res.numUnfrozenConsensus==2 );
    BOOST_CHECK( res.numUnwhitelistedTxs==1 );
    BOOST_CHECK( !db.QueryAllWhitelistedTxs().Valid() );
    BOOST_CHECK( db.GetFrozenTXOData(txo1, ftd) && ftd.IsFrozenOnPolicy(0) && !ftd.IsFrozenOnConsensus(0) ); // policy frozen TXO must be unaffected
    cnt=0;
    for(auto it=db.QueryAllFrozenTXOs(); it.Valid(); it.Next())
    {
        if(it.GetFrozenTXO().first == txo1)
        {
            ++cnt;
            continue;
        }

        BOOST_TEST_ERROR("Unexpected txo!");
    }
    BOOST_CHECK( cnt==1 );
    res = db.UnfreezeAll(true); // running again should do nothing
    BOOST_CHECK( res.numUnfrozenPolicyOnly==0 );
    BOOST_CHECK( res.numUnfrozenConsensus==0 );
    BOOST_CHECK( res.numUnwhitelistedTxs==0 );
    BOOST_CHECK( db.QueryAllFrozenTXOs().Valid() );
    BOOST_CHECK( !db.QueryAllWhitelistedTxs().Valid() );
    res = db.UnfreezeAll(); // running again without the keepPolicyEntries should remove the remaining record
    BOOST_CHECK( res.numUnfrozenPolicyOnly==1 );
    BOOST_CHECK( res.numUnfrozenConsensus==0 );
    BOOST_CHECK( res.numUnwhitelistedTxs==0 );
    BOOST_CHECK( !db.QueryAllFrozenTXOs().Valid() );
    BOOST_CHECK( !db.QueryAllWhitelistedTxs().Valid() );
}


BOOST_AUTO_TEST_CASE(IsConfiscationTx_test)
{
    using namespace std;
    using script = vector<uint8_t>;

    vector<pair<script, bool>> v{
        make_pair(script{}, false),
        make_pair(script{0x0}, false),
        make_pair(script{0x0, 0x6a}, false),
        make_pair(script{0x0, 0x6a, 0x4}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x63}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x63, 0x66}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x63, 0x66, 0x74}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x63, 0x66, 0x74, 0x78}, true),
        make_pair(script{0x0, 0x6a, 0x4, 0x63, 0x66, 0x74, 0x78, 0x00}, true),
        make_pair(script{0x9, 0x6a, 0x4, 0x63, 0x66, 0x74, 0x78}, false),
        make_pair(script{0x0, 0x99, 0x4, 0x63, 0x66, 0x74, 0x78}, false),
        make_pair(script{0x0, 0x6a, 0x9, 0x63, 0x66, 0x74, 0x78}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x99, 0x66, 0x74, 0x78}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x63, 0x99, 0x74, 0x78}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x63, 0x66, 0x99, 0x78}, false),
        make_pair(script{0x0, 0x6a, 0x4, 0x63, 0x66, 0x74, 0x99}, false)
    };

    for(const auto& [input, expected] : v)
    {
        const vector<uint8_t> scr{input};

        CMutableTransaction ctx;
        ctx.vout.resize(1);
        ctx.vout[0].scriptPubKey = CScript(scr.begin(), scr.end());

        BOOST_CHECK_EQUAL(expected, CFrozenTXODB::IsConfiscationTx( CTransaction(ctx) ));
    }
}

BOOST_AUTO_TEST_CASE(ValidateConfiscationTxContents_test)
{
    using namespace std;
    using script = vector<uint8_t>;

    static constexpr auto version_len{1};
    static constexpr auto version{1};
    const script preamble{0x0, 0x6a, 0x4, 0x63, 0x66, 0x74, 0x78};
    static constexpr size_t order_hash_len{20};
    static constexpr size_t location_hint_len{54};
    static constexpr uint8_t op_push{version_len + order_hash_len + location_hint_len};

    // preamble, op_push, version, orderhash_length, locationhint_length, expected
    using value_type = tuple<script, uint8_t, uint8_t, size_t, size_t, bool>;
    // clang-format off
    vector<value_type> v
    {
        // happy case
        make_tuple(preamble, op_push, version, order_hash_len, location_hint_len, true),

        // wrong version
        make_tuple(preamble, op_push, 0, order_hash_len, location_hint_len, false),
        make_tuple(preamble, op_push, 2, order_hash_len, location_hint_len, false),

        // orderhash too short, no location hint
        make_tuple(preamble,
                   version_len + order_hash_len - 1,
                   version, order_hash_len -1, 0, false),

        // variable location hint
        make_tuple(preamble,
                   version_len + order_hash_len + 0,
                   version, order_hash_len, 0, true),
        make_tuple(preamble,
                   version_len + order_hash_len + 1,
                   version, order_hash_len, 1, true),
        make_tuple(preamble,
                   version_len + order_hash_len + location_hint_len,
                   version, order_hash_len, location_hint_len, true),
        make_tuple(preamble,
                   version_len + order_hash_len + location_hint_len + 1,
                   version, order_hash_len, location_hint_len + 1, false),

        // op_pushdata > 75 (only single byte OP_PUSHDATA is allowed)
        make_tuple(preamble, 76, version, order_hash_len, location_hint_len, false),

        // op_push > total script size
        make_tuple(preamble, op_push, version, order_hash_len, location_hint_len-1, false),

        // op_push < total script size
        make_tuple(preamble, op_push-1, version, order_hash_len, location_hint_len, false)
    };
    // clang-format on

    for(const auto& [preamble, op_push, version, order_hash_len, location_hint_len, exp] : v)
    {
        vector<uint8_t> scr{preamble};
        scr.push_back(op_push);
        scr.push_back(version);

        vector<uint8_t> order_hash(order_hash_len);
        iota(order_hash.begin(), order_hash.end(), 0);
        scr.insert(scr.end(), order_hash.begin(), order_hash.end());

        vector<uint8_t> location_hint(location_hint_len);
        iota(location_hint.begin(), location_hint.end(), 0);
        scr.insert(scr.end(), location_hint.begin(), location_hint.end());

        CMutableTransaction ctx;
        ctx.vin.resize(1);
        ctx.vout.resize(1);
        ctx.vout[0].scriptPubKey = CScript(scr.begin(), scr.end());

        BOOST_CHECK( CFrozenTXODB::IsConfiscationTx( CTransaction(ctx) ) );
        BOOST_CHECK_EQUAL(exp, CFrozenTXODB::ValidateConfiscationTxContents( CTransaction(ctx) ));

        if(exp)
        {
            // Otherwise valid confiscation transaction must also have no provable unspendable outputs
            ctx.vout.resize(2);
            ctx.vout[1].scriptPubKey = CScript() << OP_FALSE << OP_RETURN;
            BOOST_CHECK_EQUAL(false, CFrozenTXODB::ValidateConfiscationTxContents( CTransaction(ctx) ));
        }
    }
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
        for(;;) // NOTE: This loop typically finishes after only two iterations because writing thread takes most of the time.
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
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0U );
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
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0U );
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
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0U );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[1].ok, txo_array.size() );
    BOOST_CHECK_EQUAL( cnt[0].alt, 0U );
    BOOST_CHECK_EQUAL( cnt[1].alt, 0U );

    // Remove all frozen TXO records while checking if they are frozen
    start_frozen_txo_checker(20);
    run_in_two_threads(cnt, [&](Cnt* cnt){
        auto res = db.UnfreezeAll();
        cnt->ok = res.numUnfrozenPolicyOnly;
        cnt->alt = res.numUnfrozenConsensus;
    });
    stop_frozen_txo_checker();
    db.Sync();
    BOOST_CHECK_EQUAL( policy.num_frozen, 0U );
    BOOST_CHECK_EQUAL( consensus.num_frozen, 0U );
    BOOST_CHECK_EQUAL( cnt[0].ok + cnt[1].ok, txo_array.size());
    BOOST_CHECK_EQUAL( cnt[0].alt, 0U);
    BOOST_CHECK_EQUAL( cnt[1].alt, 0U);
}


BOOST_AUTO_TEST_SUITE_END()
