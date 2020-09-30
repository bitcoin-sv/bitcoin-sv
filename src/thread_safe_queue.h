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
  private:
    std::deque<T> theQueue;
    std::mutex mtx;
    std::condition_variable onPush;
    std::condition_variable onPop;
    std::atomic_bool isClosed = false;
    const std::function<size_t(const T&)> sizeCalculator;
    const bool variableSizeObjects;
    size_t currentSize = 0;
    const size_t maximalSize;

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

  public:
    // Constructor for the fixed size objects
    // If one wants to limit this queue by number of elements set the objectSize to 1
    explicit CThreadSafeQueue(size_t maxSize = std::numeric_limits<size_t>::max(), size_t objectSize = sizeof(T))
        :sizeCalculator([objectSize](const T&){ return objectSize;})
        ,variableSizeObjects(false)
        ,maximalSize(maxSize)
    {    
    }

    // Constructor for the object that do not have fixed size
    // Useful for complex and dynamically allocated objects
    // Warning: sizeCalc should always return same value for the same object. Undefined behavior otherwise
    explicit CThreadSafeQueue(size_t maxSize, std::function<size_t(const T&)> sizeCalc)
        :sizeCalculator(sizeCalc)
        ,variableSizeObjects(true)
        ,maximalSize(maxSize)
    {
    }

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

        onPop.wait(lock, [&]() { return isClosed || (currentSize + objectSize <= maximalSize); });

        if (isClosed)
        {
            return false;
        }

        theQueue.push_back(std::forward<TT>(value));
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

        theQueue.push_back(std::forward<TT>(value));
        currentSize += objectSize;

        onPush.notify_one();

        return true;
    }


    // Pops from the front of the queue. If the queue is empty this function
    // will block until something is pushed to the queue or the queue is closed.
    // If there is nothing to pop and the queue is closed, this function will return std::nullopt.
    std::optional<T> PopWait()
    {
        std::unique_lock<std::mutex> lock(mtx);

        onPush.wait(lock, [&]() { return !theQueue.empty() || isClosed; });

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

};
