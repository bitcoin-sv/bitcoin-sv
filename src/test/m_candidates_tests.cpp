// Copyright (c) 2019 The Bitcoin SV developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mining/candidates.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(mining_candidates)

BOOST_AUTO_TEST_CASE(add_remove_candidates) {
    constexpr int NUM_CANDIDATES = 250;
    std::set<MiningCandidateId> idsSet;
    uint256 randomHash = InsecureRand256();

    CMiningCandidateManager manager;
    MiningCandidateId first;
    MiningCandidateId fiftythird;
    BOOST_CHECK_EQUAL(0, manager.Size());
    for(int i = 0; i < NUM_CANDIDATES; i++) {
        CMiningCandidateRef ref = manager.Create(randomHash);
        BOOST_CHECK(ref != nullptr);
        if (ref != nullptr) {
            // every id should be unique
            BOOST_CHECK(idsSet.find(ref->GetId()) == idsSet.end());
            idsSet.insert(ref->GetId());
            if (i == 0)
                first = ref->GetId();
            else if (i == 52)
                fiftythird = ref->GetId();
        }
    }
    BOOST_CHECK_EQUAL(NUM_CANDIDATES, manager.Size());

    // remove the first element
    manager.Remove(first);
    BOOST_CHECK_EQUAL(NUM_CANDIDATES-1, manager.Size());
    BOOST_CHECK(manager.Get(first)==std::nullopt);
    // remove it again
    manager.Remove(first);
    BOOST_CHECK_EQUAL(NUM_CANDIDATES-1, manager.Size());
    // remove the 53rd element
    manager.Remove(fiftythird);
    BOOST_CHECK_EQUAL(NUM_CANDIDATES-2, manager.Size());
    BOOST_CHECK(manager.Get(fiftythird)==std::nullopt);
}

BOOST_AUTO_TEST_SUITE_END()
