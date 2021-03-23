// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <limitedmap.h>

#include <chrono>
#include <mutex>
#include <stdexcept>

/**
 * Store some number of items which are blacklisted for some limited time.
 *
 * Any object which can be uniquely identified (and therefore compared)
 * may be blacklisted.
 *
 * A limit on the maximum number of items in the list is enforced.
 */
template<
    typename Item,
    typename Clock = std::chrono::system_clock>
class TimeLimitedBlacklist
{
  public:
    TimeLimitedBlacklist(size_t maxItems)
        : mBlacklist{maxItems}
    {}

    // Accessors
    [[nodiscard]] size_t GetMaxSize() const
    {
        return mBlacklist.max_size();
    }

    // Add a new item to the blacklist. It will be blacklisted until the specified time.
    //
    // If 'updateIfExists' is true and 'item' already exists in the blacklist then its time
    // will be updated. If 'updateIfExists' is false and 'item' already exists in the blacklist
    // then it is an error and an exception will be raised.
    void Add(const Item& item, const std::chrono::time_point<Clock>& until, bool updateIfExists = true)
    {
        std::lock_guard lock { mMtx };

        // Check for existing item
        if(mBlacklist.contains(item))
        {
            if(updateIfExists)
            {
                // Replace this item
                mBlacklist.erase(item);
                mBlacklist.insert(std::make_pair(item, until));
            }
            else
            {
                throw std::runtime_error("Item already exists in blacklist");
            }
        }
        else
        {
            // Just insert
            mBlacklist.insert(std::make_pair(item, until));
        }
    }

    // Add a new item to the blacklist. It will be blacklisted for the specified duration.
    //
    // If 'updateIfExists' is true and 'item' already exists in the blacklist then its time
    // will be updated. If 'updateIfExists' is false and 'item' already exists in the blacklist
    // then it is an error and an exception will be raised.
    template<typename Duration>
    void Add(const Item& item, const Duration& length, bool updateIfExists = true)
    {
        const auto& blacklistUntil { Clock::now() + length };
        Add(item, blacklistUntil, updateIfExists);
    }

    // Get whether the blacklist contains the specified item (whether or not it is
    // blacklisted currently)
    [[nodiscard]] bool Contains(const Item& item) const
    {
        std::lock_guard lock { mMtx };
        return mBlacklist.contains(item);
    }

    // Get whether the specified item is currently blacklisted
    bool IsBlacklisted(const Item& item) const
    {
        std::lock_guard lock { mMtx };

        // Does item have an entry in the blacklist?
        if(const auto& entry { mBlacklist.find(item) }; entry != mBlacklist.end())
        {   
            // Entry found, but has the time expired?
            if(entry->second > Clock::now())
            {   
                return true;
            }

            // Remove expired item from blacklist. This isn't strictly necessary since
            // the oldest item would be the one dropped from the blacklist if it fills up,
            // but it's just good house keeping.
            mBlacklist.erase(item);
        }

        return false;
    }

  private:

    mutable std::mutex mMtx {};
    mutable limitedmap<Item, std::chrono::time_point<std::chrono::system_clock>> mBlacklist;

};

