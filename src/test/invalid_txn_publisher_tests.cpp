// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <atomic>
#include <mutex>
#include <condition_variable>

#include <boost/test/unit_test.hpp>

#include "test/test_bitcoin.h"
#include "invalid_txn_publisher.h"

BOOST_FIXTURE_TEST_SUITE(invalid_txn_publisher_tests, TestChain100Setup)

namespace
{
    class TestSink : public InvalidTxnPublisher::CInvalidTxnSink
    {
    public:
        TestSink(
            std::mutex& mutex,
            std::condition_variable& processPublish,
            std::optional<InvalidTxnInfo>& received)
            : mMutex{ mutex }
            , mProcessPublish{ processPublish }
            , mReceived{ received }
        {
        }

        void Publish(const InvalidTxnInfo& invalidTxInfo) override
        {
            {
                std::unique_lock lock{ mMutex };
                mReceived = invalidTxInfo;
            }

            mProcessPublish.notify_one();
        }

    private:
        std::mutex& mMutex;
        std::condition_variable& mProcessPublish;
        std::optional<InvalidTxnInfo>& mReceived;
    };

    CMutableTransaction MakeLargeTxn(
        const std::vector<COutPoint>& outpoints,
        const CScript& scriptPubKey)
    {
        CMutableTransaction txn;
        txn.nVersion = 1;
        txn.vin.reserve(outpoints.size());

        for(const auto& out : outpoints)
        {
            txn.vin.emplace_back(out);
        }

        txn.vout.resize(1000);
        for(size_t j=0; j<1000; ++j) {
            txn.vout[j].nValue = 11 * CENT;
            txn.vout[j].scriptPubKey = scriptPubKey;
        }

        return txn;
    }

    CScript MakeScriptPubKey(const CKey& key)
    {
        return CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;
    }

    class DoubleSpendDetector
    {
    public:
        CValidationState Spend(const CMutableTransaction& txn, bool acceptExpected)
        {
            CValidationState state;
            bool accepted =
                mDetector.insertTxnInputs(
                    MakeDoubleSpendDetectorData(txn)->GetTxnPtr(),
                    mempool,
                    state,
                    true);

            BOOST_CHECK(accepted == acceptExpected);

            return state;
        }

    private:
        CTxnDoubleSpendDetector mDetector;

        std::shared_ptr<CTxInputData> MakeDoubleSpendDetectorData(
            const CMutableTransaction& txn)
        {
            return
                std::make_shared<CTxInputData>(
                    std::weak_ptr<CTxIdTracker>{},
                    MakeTransactionRef(txn),
                    TxSource::p2p,
                    TxValidationPriority::normal,
                    TxStorage::memory,
                    GetTime(),// nAcceptTime
                    Amount(0), // nAbsurdFee
                    std::weak_ptr<CNode>{});
        }
    };

    InvalidTxnPublisher::InvalidTxnInfoWithTxn MakeInvalidTxnInfoWithTxn(
        const CTransaction& inTxn,
        const CKey& inTxnKey)
    {
        CMutableTransaction spend_0{
            MakeLargeTxn(
                { COutPoint{ inTxn.GetId(), 0 } },
                MakeScriptPubKey(inTxnKey)) };
        CMutableTransaction spend_1_2{
            MakeLargeTxn(
                {
                    COutPoint{ inTxn.GetId(), 1 },
                    COutPoint{ inTxn.GetId(), 2 }
                },
                MakeScriptPubKey(inTxnKey)) };
        CMutableTransaction doublespend{
            MakeLargeTxn(
                {
                    COutPoint{ inTxn.GetId(), 0 },
                    COutPoint{ inTxn.GetId(), 1 },
                    COutPoint{ inTxn.GetId(), 2 }
                },
                MakeScriptPubKey(inTxnKey)) };

        DoubleSpendDetector detector;

        // no double spend
        detector.Spend(spend_0, true);
        detector.Spend(spend_1_2, true);

        // double spend
        CValidationState doublespendState = detector.Spend(doublespend, false);

        return
            {
                MakeTransactionRef(inTxn),
                InsecureRand256(), // dummy hash
                10, // dummy height
                std::time(nullptr),
                doublespendState};
    }

    std::string InvalidTxnInfoToJson(const InvalidTxnInfo& info)
    {
        CStringWriter tw;
        CJSONWriter jw(tw, false);
        info.ToJson(jw, true);

        return tw.MoveOutString();
    }
}

BOOST_AUTO_TEST_CASE(publish_no_sinks)
{
    CInvalidTxnPublisher publisher{ {}, {} };

    auto invalid = MakeInvalidTxnInfoWithTxn(coinbaseTxns[0], coinbaseKey);

    // publishing invalid transactions is still valid but they will just be
    // discarded
    publisher.Publish( std::move(invalid) );

    // ClearStored() is a no-op
    BOOST_CHECK_EQUAL(publisher.ClearStored(), 0);
}

BOOST_AUTO_TEST_CASE(publish_enough_space_for_info)
{
    std::optional<InvalidTxnInfo> received;
    std::mutex mutex;
    std::condition_variable processPublish;

    auto item = MakeInvalidTxnInfoWithTxn(coinbaseTxns[0], coinbaseKey);
    auto expectedJson = InvalidTxnInfoToJson(item.GetInvalidTxnInfo());

    std::vector<std::unique_ptr<InvalidTxnPublisher::CInvalidTxnSink>> sinks;
    sinks.push_back( std::make_unique<TestSink>(mutex, processPublish, received) );

    CInvalidTxnPublisher publisher{
        std::move(sinks),
        {},
        item.GetInvalidTxnInfo().DynamicMemoryUsage() }; // we want enough queue space for the whole transaction

    publisher.Publish( std::move(item) );

    std::unique_lock lock{ mutex };
    using namespace std::chrono_literals;
    BOOST_CHECK( processPublish.wait_for(lock, 200ms, [&received]{return received.has_value();}) );
    BOOST_CHECK_EQUAL( InvalidTxnInfoToJson(received.value()), expectedJson );
}

BOOST_AUTO_TEST_CASE(publish_missing_some_space_for_info)
{
    std::optional<InvalidTxnInfo> received;
    std::mutex mutex;
    std::condition_variable processPublish;

    auto item = MakeInvalidTxnInfoWithTxn(coinbaseTxns[0], coinbaseKey);

    // we expect there won't be enough space for last transaction
    InvalidTxnInfo expected = item.GetInvalidTxnInfo();
    expected.GetCollidedWithTruncationRange().begin()->TruncateTransactionDetails();

    BOOST_CHECK(item.GetInvalidTxnInfo().DynamicMemoryUsage() > expected.DynamicMemoryUsage());

    std::vector<std::unique_ptr<InvalidTxnPublisher::CInvalidTxnSink>> sinks;
    sinks.push_back( std::make_unique<TestSink>(mutex, processPublish, received) );

    CInvalidTxnPublisher publisher{
        std::move(sinks),
        {},
        expected.DynamicMemoryUsage() }; // last collided item won't be able to go into cache

    publisher.Publish( std::move(item) );

    std::unique_lock lock{ mutex };
    using namespace std::chrono_literals;
    BOOST_CHECK( processPublish.wait_for(lock, 200ms, [&received]{return received.has_value();}) );
    BOOST_CHECK_EQUAL( InvalidTxnInfoToJson(received.value()), InvalidTxnInfoToJson(expected) );
}

BOOST_AUTO_TEST_CASE(publish_not_enough_space_for_info)
{
    std::optional<InvalidTxnInfo> received;
    std::mutex mutex;
    std::condition_variable processPublish;

    auto item = MakeInvalidTxnInfoWithTxn(coinbaseTxns[0], coinbaseKey);

    std::vector<std::unique_ptr<InvalidTxnPublisher::CInvalidTxnSink>> sinks;
    sinks.push_back( std::make_unique<TestSink>(mutex, processPublish, received) );

    CInvalidTxnPublisher publisher{
        std::move(sinks),
        {},
        1 }; // cache is to small to send anything


    publisher.Publish( std::move(item) );

    std::unique_lock lock{ mutex };
    using namespace std::chrono_literals;
    BOOST_CHECK( processPublish.wait_for(lock, 200ms, [&received]{return received.has_value();}) == false );
}

BOOST_AUTO_TEST_CASE(callback)
{
    auto invalid = MakeInvalidTxnInfoWithTxn(coinbaseTxns[0], coinbaseKey);
    bool triggered = false;

    auto check =
        [&invalid, &triggered]
        (const InvalidTxnPublisher::InvalidTxnInfoWithTxn& info)
        {
            const CTransactionRef& txn { info.GetTransaction() };
            const std::set<CTransactionRef>& doubleSpends { info.GetCollidedWithTransactions() };
            triggered = true;

            BOOST_CHECK_EQUAL( txn, invalid.GetTransaction() );
            BOOST_CHECK_EQUAL_COLLECTIONS(
                doubleSpends.begin(),
                doubleSpends.end(),
                invalid.GetCollidedWithTransactions().begin(),
                invalid.GetCollidedWithTransactions().end() );
        };

    CInvalidTxnPublisher publisher{ {}, check };

    publisher.Publish( InvalidTxnPublisher::InvalidTxnInfoWithTxn{invalid} );

    BOOST_CHECK(triggered);
}

BOOST_AUTO_TEST_CASE(callback_throw_exception)
{
    std::atomic_bool sinkTriggered = false;
    std::mutex mutex;
    std::condition_variable processPublish;

    bool callbackTriggered = false;

    auto check =
        [&callbackTriggered]
        (const InvalidTxnPublisher::InvalidTxnInfoWithTxn& info)
        {
            callbackTriggered = true;

            throw std::exception{};
        };

    class TestSink : public InvalidTxnPublisher::CInvalidTxnSink
    {
    public:
        TestSink(
            std::condition_variable& processPublish,
            std::atomic_bool& triggered)
            : mProcessPublish{ processPublish }
            , mTriggered{ triggered }
        {}

        void Publish(const InvalidTxnInfo& invalidTxInfo) override
        {
            mTriggered = true;
            mProcessPublish.notify_one();
        }

    private:
        std::condition_variable& mProcessPublish;
        std::atomic_bool& mTriggered;
    };

    std::vector<std::unique_ptr<InvalidTxnPublisher::CInvalidTxnSink>> sinks;
    sinks.push_back( std::make_unique<TestSink>(processPublish, sinkTriggered) );

    CInvalidTxnPublisher publisher{ std::move(sinks), check };

    publisher.Publish( MakeInvalidTxnInfoWithTxn(coinbaseTxns[0], coinbaseKey) );

    BOOST_CHECK(callbackTriggered);

    std::unique_lock lock{ mutex };

    // Sink processes the info even though callback threw an exception
    using namespace std::chrono_literals;
    BOOST_CHECK( processPublish.wait_for(lock, 200ms, [&sinkTriggered]{return sinkTriggered.load();}) );
}

BOOST_AUTO_TEST_SUITE_END()
