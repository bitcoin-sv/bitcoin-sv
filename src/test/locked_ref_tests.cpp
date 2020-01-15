// Copyright (c) 2018 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <locked_ref.h>

#include <iostream>

namespace
{
}

BOOST_AUTO_TEST_SUITE(LockedRef);

BOOST_AUTO_TEST_CASE(ConstructUnique)
{
    std::mutex mtx {};
    std::shared_ptr<int> wrapped { std::make_shared<int>(1) };

    // Basic construction
    CLockedRef<decltype(wrapped), std::unique_lock<std::mutex>> lptr { wrapped, mtx };
    BOOST_CHECK(lptr.get() != nullptr);
    BOOST_CHECK_EQUAL(*(lptr.get()), 1);

    // Move construction
    auto lptrCopy { std::move(lptr) };
    BOOST_CHECK(lptr.get() == nullptr);
    BOOST_CHECK(lptrCopy.get() != nullptr);
    BOOST_CHECK_EQUAL(*(lptrCopy.get()), 1);
}

BOOST_AUTO_TEST_CASE(ConstructShared)
{
    std::shared_mutex mtx {};

    {
        // Construction unique
        CLockedRef<std::shared_ptr<int>, std::unique_lock<std::shared_mutex>> lptr { std::make_shared<int>(1), mtx };
        BOOST_CHECK(lptr.get() != nullptr);
        BOOST_CHECK_EQUAL(*(lptr.get()), 1);
    }

    {
        // Construction shared
        std::shared_ptr<int> wrapped { std::make_shared<int>(2) };
        CLockedRef<decltype(wrapped), std::shared_lock<std::shared_mutex>> lptr1 { wrapped, mtx };
        CLockedRef<decltype(wrapped), std::shared_lock<std::shared_mutex>> lptr2 { wrapped, mtx };
        CLockedRef<decltype(wrapped), std::shared_lock<std::shared_mutex>> lptr3 { wrapped, mtx };
        BOOST_CHECK(lptr1.get() != nullptr);
        BOOST_CHECK(lptr2.get() != nullptr);
        BOOST_CHECK(lptr3.get() != nullptr);
        BOOST_CHECK_EQUAL(*(lptr1.get()), 2);
        BOOST_CHECK_EQUAL(*(lptr2.get()), 2);
        BOOST_CHECK_EQUAL(*(lptr3.get()), 2);
    }
}

BOOST_AUTO_TEST_SUITE_END();
