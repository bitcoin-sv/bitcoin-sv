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
    using BlockedLogger = std::function<void(const char* method)>;

  private:
    std::deque<T> theQueue;
    std::mutex mtx;
    std::condition_variable onPush;
    std::condition_variable onPop;
    std::atomic_bool isClosed = false;
    const SizeCalculator sizeCalculator;
    const bool variableSizeObjects;
    size_t currentSize = 0;
    const size_t maximalSize;
    const BlockedLogger blockedPushLogger;
    const BlockedLogger blockedPopLogger;

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

    // Wait for enough space to become available in the queue to insert
    // insertSize data. Optionally notify when actually blocked.
    void WaitForPush(std::unique_lock<std::mutex>& lock,
                     size_t insertSize, const char* method)
    {
        const auto predicate = [this, insertSize]() -> bool
        {
            return isClosed || (currentSize + insertSize <= maximalSize);
        };

        if (blockedPushLogger == nullptr)
        {
            onPop.wait(lock, predicate);
        }
        else
        {
            bool checked = false;
            bool blocked = false;
            onPop.wait(lock, [&]() {
                if (!checked && blocked && !isClosed)
                {
                    blockedPushLogger(method);
                    checked = true;
                }
                blocked = true;
                return predicate();
            });
        }
    }

    // Wait for data to become available to extract from the queue.
    // Optionally notify when actually blocked.
    void WaitForPop(std::unique_lock<std::mutex>& lock, const char* method)
    {
        const auto predicate = [this]() -> bool
        {
            return !theQueue.empty() || isClosed;
        };

        if (blockedPopLogger == nullptr)
        {
            onPush.wait(lock, predicate);
        }
        else
        {
            bool checked = false;
            bool blocked = false;
            onPush.wait(lock, [&]() {
                if (!checked && blocked && !isClosed)
                {
                    blockedPopLogger(method);
                    checked = true;
                }
                blocked = true;
                return predicate();
            });
        }
    }

  public:
    // Constructor for the fixed size objects
    // logBlockedPush will be called if a blocking push would actually block
    // logBlockedPop will be called if a blocking pop would actually block
    explicit CThreadSafeQueue(size_t maxSize = std::numeric_limits<size_t>::max(),
                              const BlockedLogger& logBlockedPush = nullptr,
                              const BlockedLogger& logBlockedPop = nullptr)
        :sizeCalculator([](const T&){ return sizeof(T); })
        ,variableSizeObjects(false)
        ,maximalSize(maxSize)
        ,blockedPushLogger{logBlockedPush}
        ,blockedPopLogger{logBlockedPop}
    {}

    // Constructor for the fixed size objects that are not sizeof(value_type)
    // If one wants to limit this queue by number of elements set the objectSize to 1
    // logBlockedPush will be called if a blocking push would actually block
    // logBlockedPop will be called if a blocking pop would actually block
    explicit CThreadSafeQueue(size_t maxSize, size_t objectSize,
                              const BlockedLogger& logBlockedPush = nullptr,
                              const BlockedLogger& logBlockedPop = nullptr)
        :sizeCalculator([objectSize](const T&){ return objectSize; })
        ,variableSizeObjects(false)
        ,maximalSize(maxSize)
        ,blockedPushLogger{logBlockedPush}
        ,blockedPopLogger{logBlockedPop}
    {}

    // Constructor for the object that do not have fixed size
    // Useful for complex and dynamically allocated objects
    // Warning: sizeCalc should always return same value for the same object. Undefined behavior otherwise
    // logBlockedPush will be called if a blocking push would actually block
    // logBlockedPop will be called if a blocking pop would actually block
    explicit CThreadSafeQueue(size_t maxSize, const SizeCalculator& sizeCalc,
                              const BlockedLogger& logBlockedPush = nullptr,
                              const BlockedLogger& logBlockedPop = nullptr)
        :sizeCalculator(sizeCalc)
        ,variableSizeObjects(true)
        ,maximalSize(maxSize)
        ,blockedPushLogger{logBlockedPush}
        ,blockedPopLogger{logBlockedPush}
    {}

    ~CThreadSafeQueue() = default;

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
        std::unique_lock<std::mutex> lock(mtx);

        if (isClosed)
        {
            return false;
        }

        size_t objectSize = sizeCalculator(value);
        
        if (objectSize > maximalSize)
        {
            return false; // object is too big for this queue
        }

        WaitForPush(lock, objectSize, "PushWait");

        if (isClosed)
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

  private:
    // Atomically appends a sequence of new values to the queue, optionally clearing it first.
    // Will block until there is enough space in the queue or the queue is closed.
    // If the queue is closed will not push anything and will return false immediately.
    template <typename C>
    bool FillOrReplaceWait(C&& value_sequence, bool replace, const char* method)
    {
        std::unique_lock<std::mutex> lock(mtx);

        if (isClosed)
        {
            return false;
        }

        size_t listSize = 0;
        for (const auto& value : value_sequence)
        {
            listSize += sizeCalculator(value);
        }

        if (listSize > maximalSize)
        {
            return false; // the list is too big for this queue
        }

        if (replace)
        {
            theQueue.clear();
            currentSize = 0;
            onPop.notify_all();
        }
        else
        {
            WaitForPush(lock, listSize, method);

            if (isClosed)
            {
                return false;
            }
        }

        for (auto&& value : value_sequence)
        {
            theQueue.emplace_back(std::move(value));
        }
        currentSize += listSize;

        onPush.notify_all();

        return true;
    }

  public:
    // Atomically appends a sequence of new values to the queue.
    // Will block until there is enough space in the queue or the queue is closed.
    // If the queue is closed will not push anything and will return false immediately.
    template <typename C>
    bool FillWait(C&& value_sequence)
    {
        return FillOrReplaceWait(std::forward<C>(value_sequence), false, "FillWait");
    }

    // Atomically replace the contents of the queue with a sequence of new values.
    // If the queue is closed will not push anything and will return false immediately.
    template <typename C>
    bool ReplaceWait(C&& value_sequence)
    {
        return FillOrReplaceWait(std::forward<C>(value_sequence), true, "ReplaceWait");
    }

    // Pops from the front of the queue. If the queue is empty this function
    // will block until something is pushed to the queue or the queue is closed.
    // If there is nothing to pop and the queue is closed, this function will return std::nullopt.
    std::optional<T> PopWait()
    {
        std::unique_lock<std::mutex> lock(mtx);

        WaitForPop(lock, "PopWait");

        if(theQueue.empty())
        {
            return {};
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
            return {};
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

        WaitForPop(lock, "PopAllWait");

        if(theQueue.empty())
        {
            return {};
        }

        currentSize = 0;
        onPop.notify_all();

        return {std::move(theQueue)};
    }

    // Non blocking implementation of the PopAllWait(). Will not wait until
    // there is something in the queue.
    // If the queue is empty but not closed, returns an empty queue.
    // If the queue is closed, returns std::nullopt.
    std::optional<std::deque<T>> PopAllNoWait()
    {
        std::unique_lock<std::mutex> lock(mtx);

        if (isClosed && theQueue.empty())
        {
            return {};
        }

        if (!theQueue.empty())
        {
            currentSize = 0;
            onPop.notify_all();
        }

        return {std::move(theQueue)};
    }
};
