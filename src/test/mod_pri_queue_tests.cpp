// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <boost/test/unit_test.hpp>
#include <mod_pri_queue.h>

#include <iostream>

namespace
{
    // Check a queue has the expected contents
    template<typename Queue, typename Contents>
    bool CheckQContents(const Queue& q, const Contents& expected)
    {
        // Read the contents from the queue
        Queue qcopy {q};
        Contents actual {};
        while(!qcopy.empty())
        {
            actual.push_back(qcopy.top());
            qcopy.pop();
        }

        return { expected == actual };
    }
}

BOOST_AUTO_TEST_SUITE(ModifyPriorityQueue);

BOOST_AUTO_TEST_CASE(ConstructAndErase)
{
    // Test basic creation
    std::vector<int> values { 1, 3, 2, 7, 4, 5, 10, 6, 9, 8 };
    std::less<int> comp {};
    CModPriQueue<int> queue { comp, values };
    BOOST_CHECK(CheckQContents(queue, std::vector<int>{ 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 }));

    // Remove some values
    std::vector<int> remove { 10, 1, 6 };
    std::sort(remove.begin(), remove.end());
    queue.erase(remove);
    BOOST_CHECK(CheckQContents(queue, std::vector<int>{ 9, 8, 7, 5, 4, 3, 2 }));

    // Remove some non-existant values
    remove = { 20 };
    std::sort(remove.begin(), remove.end());
    queue.erase(remove);
    BOOST_CHECK(CheckQContents(queue, std::vector<int>{ 9, 8, 7, 5, 4, 3, 2 }));
}

BOOST_AUTO_TEST_SUITE_END();
