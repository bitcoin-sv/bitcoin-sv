// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/candidates.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(mining_candidates)

BOOST_AUTO_TEST_CASE(add_remove_candidates) {
    constexpr unsigned NUM_CANDIDATES = 250;
    std::set<MiningCandidateId> idsSet;

    // Make dummy coinbase and block
    CMutableTransaction tx {};
    CBlockRef block { std::make_shared<CBlock>() };
    block->vtx.resize(1);
    block->vtx[0] = MakeTransactionRef(std::move(tx));

    CMiningCandidateManager manager;
    MiningCandidateId first;
    MiningCandidateId fiftythird;
    BOOST_CHECK_EQUAL(0U, manager.Size());
    for(unsigned i = 0; i < NUM_CANDIDATES; i++) {
        CMiningCandidateRef ref = manager.Create(block);
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

    // fetch the first & 53rd elements
    BOOST_CHECK(manager.Get(first)!=nullptr);
    BOOST_CHECK(manager.Get(fiftythird)!=nullptr);

    // remove the first element
    manager.Remove(first);
    BOOST_CHECK_EQUAL(NUM_CANDIDATES-1, manager.Size());
    BOOST_CHECK(manager.Get(first)==nullptr);
    // remove it again
    manager.Remove(first);
    BOOST_CHECK_EQUAL(NUM_CANDIDATES-1, manager.Size());
    // remove the 53rd element
    manager.Remove(fiftythird);
    BOOST_CHECK_EQUAL(NUM_CANDIDATES-2, manager.Size());
    BOOST_CHECK(manager.Get(fiftythird)==nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
