// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "thread_safe_queue.h"

#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>

#include <future>
#include <initializer_list>
#include <vector>


namespace { class Unique; }
template<class T>
template<class U>
struct CThreadSafeQueue<T>::UnitTestAccess
{
    UnitTestAccess() = delete;
    
    static auto Count(const CThreadSafeQueue& q)
    {
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(q.mtx));
        return q.theQueue.size();
    }

    static auto Size(const CThreadSafeQueue& q)
    { 
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(q.mtx));
        return q.currentSize;
    }
};

BOOST_AUTO_TEST_SUITE(thread_safe_queue_tests)

bool WaitFor(std::function<bool()> f)
{
    for(int i = 0; i < 100; i++)
    {
        if (f())
        {
            return true;
        }
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

template<typename T>
bool is_ready(std::future<T> const& f)
{
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; 
}


template<typename T>
bool CheckNumberOfRunningThreads(const std::vector<std::future<T>>& threads, int expectedNumberOfThreads)
{
    auto checkIfNumberOfRunningThreadsIsAsExpected = [&threads, expectedNumberOfThreads]()
    {
        auto numberOfThreads = 
        std::count_if(threads.begin(), threads.end(), 
            [](const std::future<void>& f){
                return !is_ready(f);
            }
        );

        return numberOfThreads == expectedNumberOfThreads;
    };

    return WaitFor(checkIfNumberOfRunningThreadsIsAsExpected);
}

template<class T>
auto get_Count(const CThreadSafeQueue<T>& theQueue)
{
    return CThreadSafeQueue<T>::template UnitTestAccess<Unique>::Count(theQueue);;
}

template<class T>
auto get_Size(const CThreadSafeQueue<T>& theQueue)
{
    return CThreadSafeQueue<T>::template UnitTestAccess<Unique>::Size(theQueue);;
}


BOOST_AUTO_TEST_CASE(multiple_inputs_full_queue) {
    
    CThreadSafeQueue<int> theQueue(5, 1);

    BOOST_CHECK(theQueue.MaximalSize() == 5);

    std::vector<std::future<void>> pushers;
    std::set<int> outValues;

    // adding 7 integers in queue of capacity of 5
    for(int i = 0; i < 7; i++)
    {
        pushers.push_back(std::async(std::launch::async, 
            [&theQueue, i](){ 
                theQueue.PushWait(i);
            }));
    }

    // queue is full to capacity
    BOOST_CHECK(WaitFor(
        [&theQueue](){ 
            return get_Size(theQueue) == 5;
        }));

    // two more threads are waiting to push
    BOOST_CHECK(CheckNumberOfRunningThreads(pushers, 2));

    // popping one value
    outValues.insert(theQueue.PopWait().value());

    // the queue is still full
    BOOST_CHECK(WaitFor(
        [&theQueue](){ 
            return get_Size(theQueue) == 5;
        }));

    // one more thread is trying to push value
    BOOST_CHECK(CheckNumberOfRunningThreads(pushers, 1)); 

    // close the queue
    BOOST_CHECK(!theQueue.IsClosed());
    theQueue.Close();
    BOOST_CHECK(theQueue.IsClosed());

    // thread that was waiting to push value waits no more
    BOOST_CHECK(CheckNumberOfRunningThreads(pushers, 0));

    // take all values from queue, there should be 6 different integers
    const auto contents = theQueue.PopAllWait();
    BOOST_REQUIRE(contents.has_value());
    for (const auto& v : contents.value())
    {
        outValues.insert(v);
    }
    BOOST_CHECK(outValues.size() == 6);
    
    // the queue is empty now
    BOOST_CHECK(get_Size(theQueue) == 0);
}

BOOST_AUTO_TEST_CASE(fill_replace)
{
    CThreadSafeQueue<int> theQueue(5, 1);

    // Fill the queue
    BOOST_CHECK(theQueue.PushManyWait(std::initializer_list<int>{0, 1, 2, 3, 4}));
    BOOST_CHECK(get_Size(theQueue) == 5);
    BOOST_CHECK(!theQueue.PushNoWait(99));

    // Replace the contents of the queue
    BOOST_CHECK(theQueue.ReplaceContent(std::initializer_list<int>{5, 6, 7, 8, 9}));
    BOOST_CHECK(get_Size(theQueue) == 5);
    BOOST_CHECK(!theQueue.PushNoWait(99));

    // Check that fill is atomic
    std::vector<std::future<void>> pushers;
    pushers.push_back(
        std::async(std::launch::async,
                   [&theQueue]()
                   {
                       theQueue.PushManyWait(std::initializer_list<int>{10, 11, 12});
                   }));
    BOOST_CHECK(CheckNumberOfRunningThreads(pushers, 1));
    BOOST_CHECK(get_Size(theQueue) == 5);

    // ... pop values and check that the queue fills up as soon as there's space
    BOOST_CHECK(theQueue.PopWait());
    BOOST_CHECK(get_Size(theQueue) == 4);
    BOOST_CHECK(theQueue.PopWait());
    BOOST_CHECK(get_Size(theQueue) == 3);
    BOOST_CHECK(theQueue.PopWait());
    {
        // ... give the pusher time to wake up and acquire the lock
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
    }
    BOOST_CHECK(get_Size(theQueue) == 5);
    BOOST_CHECK(CheckNumberOfRunningThreads(pushers, 0));

    // Close the queue and that we can stil get all the values out
    theQueue.Close();
    BOOST_CHECK(theQueue.IsClosed());
    const auto contents = theQueue.PopAllNoWait();
    BOOST_REQUIRE(contents.has_value());
    BOOST_CHECK(contents.value().size() == 5);
}

BOOST_AUTO_TEST_CASE(fill_replace_dynamic)
{
    CThreadSafeQueue<int> theQueue(10, [](const int& i) { return static_cast<size_t>(i); });

    // Fill the queue
    BOOST_CHECK(theQueue.PushManyWait(std::initializer_list<int>{0, 1, 2, 3, 4}));
    BOOST_CHECK(get_Count(theQueue) == 5);
    BOOST_CHECK(get_Size(theQueue) == 10);
    BOOST_CHECK(!theQueue.PushNoWait(99));

    // Replace the contents of the queue
    BOOST_CHECK(theQueue.ReplaceContent(std::initializer_list<int>{7, 3}));
    BOOST_CHECK(get_Count(theQueue) == 2);
    BOOST_CHECK(get_Size(theQueue) == 10);
    BOOST_CHECK(!theQueue.PushNoWait(99));

    // Check that fill is atomic
    std::vector<std::future<void>> pushers;
    pushers.push_back(
        std::async(std::launch::async,
                   [&theQueue]()
                   {
                       theQueue.PushManyWait(std::initializer_list<int>{2, 3, 5});
                   }));
    BOOST_CHECK(CheckNumberOfRunningThreads(pushers, 1));
    BOOST_CHECK(get_Size(theQueue) == 10);

    // ... pop values and check that the queue fills up as soon as there's space
    BOOST_CHECK(theQueue.PopWait());
    BOOST_CHECK(get_Count(theQueue) == 1);
    BOOST_CHECK(get_Size(theQueue) == 3);
    BOOST_CHECK(theQueue.PopWait());
    {
        // ... give the pusher time to wake up and acquire the lock
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
    }
    BOOST_CHECK(get_Count(theQueue) == 3);
    BOOST_CHECK(get_Size(theQueue) == 10);

    // Close the queue and that we can stil get all the values out
    theQueue.Close();
    BOOST_CHECK(theQueue.IsClosed());
    const auto contents = theQueue.PopAllNoWait();
    BOOST_REQUIRE(contents.has_value());
    BOOST_CHECK(contents.value().size() == 3);
}

BOOST_AUTO_TEST_CASE(multiple_outputs)
{
    using namespace std::chrono_literals;
   
    constexpr unsigned nElements{5};
    CThreadSafeQueue<int> theQueue(nElements, 1);
    theQueue.ReplaceContent(std::initializer_list<int>{0, 1, 2, 3, 4});
    BOOST_CHECK(get_Size(theQueue) == nElements);

    CThreadSafeQueue<int> collectingQueue;
    std::vector<std::future<void>> outs;

    constexpr unsigned nThreads{nElements + 3}; 
    std::array<std::promise<void>, nThreads> ready;
    std::promise<void> go;
    std::shared_future<void> sf{go.get_future()};

    // getting one value from eight threads and pushing them to the collectingQueue
    for(unsigned i = 0; i < nThreads; ++i)
    {
        auto f = std::async(std::launch::async, 
            [&theQueue, &collectingQueue, &sf](std::promise<void>* ready){ 

                ready->set_value();
                sf.wait();                

                std::optional<int> popped = theQueue.PopWait();
                if(popped.has_value())
                {
                    collectingQueue.PushWait(popped.value());
                }
            }, &ready[i]);
        outs.push_back(std::move(f));
    }

    // wait until all threads are ready then set them running
    for(auto& p : ready)
        p.get_future().wait();
    go.set_value();

    // wait until enough threads to move all elements have finished
    while(true)
    {
        const auto n = count_if(begin(outs), end(outs), [](const auto& f) {
            return f.wait_for(0s) == std::future_status::ready;
        });
        if(n >= nElements)
            break;
    }

    // queue is emptied
    BOOST_CHECK(WaitFor([&theQueue]() { return get_Size(theQueue) == 0; }));

    // values are transferred to the collecting queue
    BOOST_CHECK_EQUAL(nElements, get_Count(collectingQueue));

    // three more threads are waiting to pop next value
    BOOST_CHECK(CheckNumberOfRunningThreads(outs, 3));

    // pushing one more value
    theQueue.PushWait(5);

    // the queue is still empty
    BOOST_CHECK(WaitFor(
        [&theQueue](){ 
            return get_Size(theQueue) == 0;
        }));

    // two threads are trying to pop value
    BOOST_CHECK(CheckNumberOfRunningThreads(outs, 2)); 

    // close the queue
    BOOST_CHECK(!theQueue.IsClosed());
    theQueue.Close();
    BOOST_CHECK(theQueue.IsClosed());

    // threads that were waiting to pop value are waiting no more
    BOOST_CHECK(CheckNumberOfRunningThreads(outs, 0));

    BOOST_CHECK(get_Count(collectingQueue) == 6);

    // take all values from queue, there should be 6 different integers
    std::set<int> values;
    while(true)
    {
        auto maybeInt = collectingQueue.PopNoWait();
        if(!maybeInt.has_value())
        {
            break;
        }
        values.insert(maybeInt.value());
    }
    BOOST_CHECK(values.size() == 6);
    
    // the queue is empty now
    BOOST_CHECK(get_Size(collectingQueue) == 0);
}


 void StressTest(CThreadSafeQueue<int>& theQueue)
{
    // create 20 inputs an 20 output threads which are concurrently pushing and popping values into the queue
    constexpr int numThreads = 20;
    static constexpr int entriesPerThread = 500000;

    CThreadSafeQueue<int> collectingQueue(entriesPerThread, 1);

    std::vector<std::future<void>> ins;
    std::vector<std::future<void>> outs;
    
    for(int i = 0; i < numThreads; i++)
    {
        ins.push_back(std::async(std::launch::async, 
            [&theQueue, i](){
                for (int j = 0; j < entriesPerThread; j++)
                {
                    theQueue.PushWait(i * entriesPerThread + j);
                }                
            }));

        outs.push_back(std::async(std::launch::async, 
            [&theQueue, &collectingQueue](){
                while(true)
                {
                    std::optional<int> popped = theQueue.PopWait();
                    if(popped.has_value())
                    {
                        collectingQueue.PushWait(popped.value());
                    }
                    else
                    {
                        break;
                    }
                }
            }));
    }

    std::set<int> values;
    for(int i = 0; i < numThreads*entriesPerThread; i++)
    {
        auto maybeInt = collectingQueue.PopWait();
        if(!maybeInt.has_value())
        {
            break;
        }
        values.insert(maybeInt.value());
    }
    // every number between 0 and (numThreads * entriesPerThread - 1) is in set
    BOOST_CHECK(values.size() == numThreads * entriesPerThread);
    BOOST_CHECK(*values.rbegin() == numThreads * entriesPerThread - 1);
    
    BOOST_CHECK(get_Count(collectingQueue) == 0);

    theQueue.Close();

    BOOST_CHECK(CheckNumberOfRunningThreads(ins, 0));
    BOOST_CHECK(CheckNumberOfRunningThreads(outs, 0));
}

BOOST_AUTO_TEST_CASE(stress_test_fixed_element_size)
{
    unsigned long blockedOnPush = 0;
    const auto logBlockedPush = [&blockedOnPush](const char*, size_t, size_t) {
        ++blockedOnPush;
    };

    unsigned long blockedOnPop = 0;
    const auto logBlockedPop = [&blockedOnPop](const char*, size_t, size_t) {
        ++blockedOnPop;
    };

    CThreadSafeQueue<int> theQueue(100, 1);
    theQueue.SetOnPopBlockedNotifier(logBlockedPop);
    theQueue.SetOnPushBlockedNotifier(logBlockedPush);
    
    StressTest(theQueue);
    BOOST_TEST_MESSAGE("Blocked in fixed-size stress test:"
                       " push " << blockedOnPush << " pop " << blockedOnPop);
}

BOOST_AUTO_TEST_CASE(stress_test_dynamic_element_size)
{
    // testing with dynamic element size also because of slightly
    // different way of notifying condition variables
    const auto sizeCalculator = [](const int& value){
        return size_t(value % 70 + 1);
    };

    unsigned long blockedOnPush = 0;
    const auto logBlockedPush = [&blockedOnPush](const char*, size_t, size_t) {
        ++blockedOnPush;
    };

    unsigned long blockedOnPop = 0;
    const auto logBlockedPop = [&blockedOnPop](const char*, size_t, size_t) {
        ++blockedOnPop;
    };

    CThreadSafeQueue<int> theQueue(100, sizeCalculator);
    theQueue.SetOnPopBlockedNotifier(logBlockedPop);
    theQueue.SetOnPushBlockedNotifier(logBlockedPush);

    StressTest(theQueue);
    BOOST_TEST_MESSAGE("Blocked in dynamic-size stress test:"
                       " push " << blockedOnPush << " pop " << blockedOnPop);
}

BOOST_AUTO_TEST_CASE(nowait) {
    
    CThreadSafeQueue<int> theQueue(3 * sizeof(int));
    
    // can push three values
    BOOST_CHECK(theQueue.PushNoWait(1));
    BOOST_CHECK(theQueue.PushNoWait(2));
    BOOST_CHECK(theQueue.PushNoWait(3));
    
    // fourth will fail
    BOOST_CHECK(!theQueue.PushNoWait(4));

    // can pop three values
    BOOST_CHECK(theQueue.PopNoWait().has_value());
    const auto contents = theQueue.PopAllNoWait();
    BOOST_CHECK(contents.has_value());
    BOOST_CHECK(contents.value().size() == 2);

    // nothing to pop, doesn't have value
    BOOST_CHECK(!theQueue.PopNoWait().has_value());

    BOOST_CHECK(theQueue.PushNoWait(1));
    theQueue.Close();
    
    // push after closing will fail
    BOOST_CHECK(!theQueue.PushNoWait(2));

    // can pop whatever is inside queue
    BOOST_CHECK(theQueue.PopNoWait().has_value());
    
}

BOOST_AUTO_TEST_CASE(dynamic_size) {
    
    // pretend that object size is equal to its value
    CThreadSafeQueue<int> theQueue(10, [](const int& i) { return static_cast<size_t>(i); } );
    
    //object is bigger than the whole queue
    BOOST_CHECK(!theQueue.PushWait(11));

    BOOST_CHECK(theQueue.PushWait(10));
    BOOST_CHECK(!theQueue.PushNoWait(1));
    
    std::vector<std::future<void>> pushThreads;
    for(int i : {1, 2, 3, 4})
    {
        pushThreads.push_back(std::async(std::launch::async, 
            [&theQueue, i](){
                theQueue.PushWait(i);               
            }));
    }
    
    BOOST_CHECK(get_Size(theQueue) == 10);
    BOOST_CHECK(get_Count(theQueue) == 1);

    BOOST_CHECK(theQueue.PopWait() == 10);

    auto check = [&theQueue](){
        return (get_Size(theQueue) == 10) && (get_Count(theQueue) == 4);
    };
    BOOST_CHECK(WaitFor(check));
}

BOOST_AUTO_TEST_SUITE_END()
