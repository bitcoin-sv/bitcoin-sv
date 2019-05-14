// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <algorithm>
#include <iterator>
#include <queue>

/**
* A priority_queue that allows us to remove elements from locations
* other than just at the head.
*/
template<typename T,
         typename Container = std::vector<T>,
         typename Compare = std::less<typename Container::value_type>
        >
class CModPriQueue : public std::priority_queue<T, Container, Compare>
{
    using Base = std::priority_queue<T, Container, Compare>;

  public:

    // We don't need to provide a constructor of our own; just forward everything to
    // one of the base class constructors.
    template<typename... Args>
    CModPriQueue(Args&&... args) : Base { std::forward<Args>(args)... }
    {}

    // Remove the given list of elements from the queue.
    // NOTE: The list of items to remove must be pre-sorted by the caller.
    void erase(const Container& eles)
    {
        // Need everything sorted to make use of set_difference
        std::sort(Base::c.begin(), Base::c.end(), Base::comp);

        // Remove items in eles from our underlying heap
        Container newContents {};
        std::set_difference(Base::c.begin(), Base::c.end(), eles.begin(), eles.end(),
            std::inserter(newContents, newContents.begin()), Base::comp);
        Base::c = std::move(newContents);

        // Recreate underlying heap
        std::make_heap(Base::c.begin(), Base::c.end(), Base::comp);
    }
};
