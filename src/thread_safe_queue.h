// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once
#include <condition_variable>
#include <queue>
#include <atomic>
#include <optional>
#include <functional>

// Implementation of the thread safe FIFO queue. 
// Simultaneous pushes and pops from arbitrary number of threads are supported.
template <typename T> 
class CThreadSafeQueue
{
public:
    using value_type = T;
    using SizeCalculator = std::function<size_t(const T&)>;
    using OnBlockedCallback = std::function<void(const char* method, size_t requiredSize, size_t availableSize)>;

  private:
    std::deque<T> theQueue;
    std::mutex mtx;
    std::condition_variable onPush;
    std::condition_variable onPop;
    std::atomic_bool isClosed = false;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const SizeCalculator sizeCalculator;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const bool variableSizeObjects;
    size_t currentSize = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const size_t maximalSize;
    OnBlockedCallback blockedPushNotifier{nullptr};
    OnBlockedCallback blockedPopNotifier{nullptr};

    void PostPopNotify()
    {
        if (variableSizeObjects)
        {
            // objects have variable size
            // we have just popped one object, and we do not know how many objects can be fitted to its place (maybe more than one)
            // so we will notify all threads to try pushing their objects to the queue
            onPop.notify_all();
        }
        else 
        {
            // all objects are the same size
            // we have popped one object so we can push precisely one object
            onPop.notify_one();
        }
    }

    template <typename PREDICATE, typename NOTIFIER>
    auto WaitAndNotifyOnFirstFail( std::condition_variable& condVar, std::unique_lock<std::mutex>& lock, 
        PREDICATE& predicate, NOTIFIER& notifier, bool shouldNotify)
    {
        if(!shouldNotify)
        {
            condVar.wait(lock, predicate);
            return;
        }

        bool alreadyNotified = false;
        condVar.wait(lock, [&]() 
        {
            if(predicate())
            {
                return true;
            }
            if(!alreadyNotified)
            {
                notifier();
            }
            alreadyNotified = true;
            return false;
        });
    }

    // waits until there is required amount of space on the queue
    // if the required size is too big or the queue gets closed it returns false
    bool WaitForSpaceInQueue(std::unique_lock<std::mutex>& lock, size_t requiredSpace, const char* method)
    {
        if(requiredSpace > maximalSize)
        {
            return false;
        }

        auto predicate = [&](){ return isClosed || (currentSize + requiredSpace) <= maximalSize; };
        auto notifyBlocked = [&](){ return blockedPushNotifier(method, requiredSpace, currentSize); };

        // wait until something is popped or the queue is closed
        WaitAndNotifyOnFirstFail(onPop, lock, predicate, notifyBlocked, blockedPushNotifier != nullptr);
        
        return !isClosed;
    }

    // waits until the queue is not empty to the queue or the queue gets closed
    bool WaitForDataInQueue(std::unique_lock<std::mutex>& lock, const char* method)
    {
        auto predicate = [&](){ return isClosed || !theQueue.empty(); };
        auto notifyBlocked = [&](){ return blockedPopNotifier(method, 0, 0); };

        // wait until something is pushed or the queue is closed
        WaitAndNotifyOnFirstFail(onPush, lock, predicate, notifyBlocked, blockedPopNotifier != nullptr);

        return !theQueue.empty();
    }

    // calculate the size for the sequence
    template <typename C>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    size_t SizeNeededForSequence(C&& value_sequence)
    {
        size_t totalSize = 0;
        for (const auto& value : value_sequence)
        {
            totalSize += sizeCalculator(value);
        }
        return totalSize;
    }

    // appends the sequence to the end of the queue, 
    // copying because sequence is an reference l-value const reference
    template <typename C>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    void AppendSequence(const C& value_sequence)
    {
        for(const auto& value: value_sequence)
        {
            theQueue.emplace_back(value);
        }
    }

    // appends the sequence to the end of the queue, 
    // moving out because the sequence is an r-value reference to a container
    template <typename C>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    void AppendSequence(C&& value_sequence)
    {
        for(auto&& value: value_sequence)
        {
            theQueue.emplace_back(std::move(value));
        }
    }

  public:

    // Constructor for the fixed size objects that are not sizeof(value_type)
    // If one wants to limit this queue by number of elements set the objectSize to 1
    explicit CThreadSafeQueue(size_t maxSize = std::numeric_limits<size_t>::max(), size_t objectSize = sizeof(T))
        :sizeCalculator([objectSize](const T&){ return objectSize; })
        ,variableSizeObjects(false)
        ,maximalSize(maxSize)
    {}

    // Constructor for the object that do not have fixed size
    // Useful for complex and dynamically allocated objects
    // Warning: sizeCalc should always return same value for the same object. Undefined behavior otherwise
    explicit CThreadSafeQueue(size_t maxSize, const SizeCalculator& sizeCalc)
        :sizeCalculator(sizeCalc)
        ,variableSizeObjects(true)
        ,maximalSize(maxSize)
    {}

    // will be called if a blocking push would actually block
    void SetOnPushBlockedNotifier(const OnBlockedCallback& onBlockedPush)
    {
        std::unique_lock<std::mutex> lock(mtx);
        blockedPushNotifier = onBlockedPush;
    }

    // will be called if a blocking pop would actually block
    void SetOnPopBlockedNotifier(const OnBlockedCallback& onBlockedPop)
    {
        std::unique_lock<std::mutex> lock(mtx);
        blockedPopNotifier = onBlockedPop;
    }

    // struct for unit test purposes
    template<typename TT> struct UnitTestAccess;

    size_t MaximalSize() const
    {
        return maximalSize;
    }

    bool IsClosed() const
    {
        return isClosed;
    }

    // Closes the queue, after this call it is not possible to Push new values.
    // All threads that are waiting to Push will fail and return false immediately.
    // Subsequent calls to Pop will return a value, if there are values present.
    void Close(bool dropValues = false)
    {
        std::unique_lock<std::mutex> lock(mtx);
        isClosed = true;
        if(dropValues)
        {
            theQueue.clear();
            currentSize = 0;
        }
        onPop.notify_all();
        onPush.notify_all();
    }


    // Pushes a new value to the back of the queue. 
    // If maximum capacity is reached function will block until: there is enough room to push the value or the queue is closed.
    // If the queue is closed will not push a value and will return false immediately.
    template <typename TT> // This is needed in order to treat value as forwarding reference
    bool PushWait(TT&& value)
    {
        auto objectSize = sizeCalculator(value);

        std::unique_lock<std::mutex> lock(mtx);
        if(!WaitForSpaceInQueue(lock, objectSize, "PushWait"))
        {
            return false;
        }

        theQueue.emplace_back(std::forward<TT>(value));
        currentSize += objectSize;

        onPush.notify_one();

        return true;
    }
    // Non-blocking version of the PushWait() function. Will not wait until there is enough room on the queue.
    template <typename TT> // This is needed in order to treat value as forwarding reference
    bool PushNoWait(TT&& value)
    {
        std::unique_lock<std::mutex> lock(mtx);

        if (isClosed)
        {
            return false;
        }

        size_t objectSize = sizeCalculator(value);
                
        if (currentSize + objectSize > maximalSize)
        {
            return false; // no room in the queue
        }

        theQueue.emplace_back(std::forward<TT>(value));
        currentSize += objectSize;

        onPush.notify_one();

        return true;
    }

    // Atomically appends a sequence of new values to the queue.
    // Will block until there is enough space in the queue or the queue is closed.
    // If the queue is closed or cumulative size of objects in the sequence is too big
    // will not push anything and will return false immediately.
    template <typename C>
    bool PushManyWait(C&& value_sequence)
    {

        auto sizeNeeded = SizeNeededForSequence(value_sequence);
    
        std::unique_lock<std::mutex> lock(mtx);

        if(!WaitForSpaceInQueue(lock, sizeNeeded, "PushManyWait"))
        {
            return false;
        }

        AppendSequence(std::forward<C>(value_sequence));
        currentSize += sizeNeeded;

        onPush.notify_all();        
        
        return true;
    }

    // Atomically appends a sequence of new values to the queue.
    // Will not block until there is enough space in the queue.
    // If the queue is closed will not push anything and will return false immediately.
    template <typename C>
    bool PushManyNoWait(C&& value_sequence)
    {

        auto sizeNeeded = SizeNeededForSequence(value_sequence);
    
        std::unique_lock<std::mutex> lock(mtx);

        if(isClosed || (currentSize + sizeNeeded) > maximalSize)
        {
            return false;
        }

        AppendSequence(std::forward<C>(value_sequence));
        currentSize += sizeNeeded;

        onPush.notify_all();        
        
        return true;
    }

    // Atomically replace the contents of the queue with a sequence of new values.
    // If the queue is closed will not push anything and will return false immediately.
    // If any thread is waiting to push it will be notified to try to push after we finish pushing to the queue
    template <typename C>
    bool ReplaceContent(C&& value_sequence)
    {
        auto sizeNeeded = SizeNeededForSequence(value_sequence);
    
        std::unique_lock<std::mutex> lock(mtx);

        if(isClosed || sizeNeeded > maximalSize)
        {
            return false;
        }

        theQueue.clear();
        AppendSequence(std::forward<C>(value_sequence));
        currentSize = sizeNeeded;

        onPush.notify_all();
        onPop.notify_all();

        return true;
    }

    // Pops from the front of the queue. If the queue is empty this function
    // will block until something is pushed to the queue or the queue is closed.
    // If there is nothing to pop and the queue is closed, this function will return std::nullopt.
    std::optional<T> PopWait()
    {
        std::unique_lock<std::mutex> lock(mtx);

        if(!WaitForDataInQueue(lock, "PopWait"))
        {
            return std::nullopt;
        }

        auto objectSize = sizeCalculator(theQueue.front());
        
        T out = std::move(theQueue.front());
        theQueue.pop_front();

        currentSize -= objectSize;

        PostPopNotify();

        return {std::move(out)};
    }

    // Non blocking implementation of the PopWait(). Will not wait until there is something to Pop().
    std::optional<T> PopNoWait()
    {
        std::unique_lock<std::mutex> lock(mtx);

        if (theQueue.empty())
        {
            return std::nullopt;
        }

        auto objectSize = sizeCalculator(theQueue.front());
        
        T out = std::move(theQueue.front());
        theQueue.pop_front();

        currentSize -= objectSize;
        
        PostPopNotify();
        
        return {std::move(out)};
    }

    // Returns the whole queue. If the queue is empty this function
    // will block until something is pushed to the queue or the queue is closed.
    // If there is nothing to pop and the queue is closed, this function will return std::nullopt.
    std::optional<std::deque<T>> PopAllWait()
    {
        std::unique_lock<std::mutex> lock(mtx);

        if(!WaitForDataInQueue(lock, "PopAllWait"))
        {
            return {};
        }

        currentSize = 0;
        onPop.notify_all();

        return {std::move(theQueue)};
    }

    // Non blocking implementation of the PopAllWait(). Will not wait until
    // there is something in the queue. If the queue is empty will return std::nullopt.
    std::optional<std::deque<T>> PopAllNoWait()
    {
        std::unique_lock<std::mutex> lock(mtx);

        if (theQueue.empty())
        {
            return {};
        }

        currentSize = 0;
        onPop.notify_all();

        return {std::move(theQueue)};
    }
};
