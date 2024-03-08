// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <set>

namespace{ class blockstatus_tests_uid; } // only used as unique identifier

template <>
struct BlockStatus::UnitTestAccess<blockstatus_tests_uid>
{
    UnitTestAccess() = delete;

    static void CheckBlockStatus(const BlockStatus s, BlockValidity validity,
                                 bool hasData, bool hasUndo, bool hasFailed,
                                 bool hasFailedParent) {
        BOOST_CHECK(s.getValidity() == validity);
        BOOST_CHECK_EQUAL(s.hasData(), hasData);
        BOOST_CHECK_EQUAL(s.hasUndo(), hasUndo);
        BOOST_CHECK_EQUAL(s.hasFailed(), hasFailed);
        BOOST_CHECK_EQUAL(s.hasFailedParent(), hasFailedParent);
        BOOST_CHECK_EQUAL(s.isInvalid(), hasFailed || hasFailedParent);
    }

    static BlockStatus WithData(BlockStatus s, bool hasData)
    {
        return s.withData( hasData );
    }

    static BlockStatus WithUndo(BlockStatus s, bool hasUndo)
    {
        return s.withUndo( hasUndo );
    }
};
using TestBlockStatus = BlockStatus::UnitTestAccess<blockstatus_tests_uid>;

BOOST_FIXTURE_TEST_SUITE(blockstatus_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(sighash_construction_test) {
    // Check default values.
    TestBlockStatus::CheckBlockStatus(BlockStatus(), BlockValidity::UNKNOWN, false, false, false,
                     false);

    // Check all possible permutations.
    std::set<BlockValidity> baseValidities{
        BlockValidity::UNKNOWN, BlockValidity::HEADER,
        BlockValidity::TREE,    BlockValidity::TRANSACTIONS,
        BlockValidity::CHAIN,   BlockValidity::SCRIPTS};
    std::set<bool> hasDataValues{false, true};
    std::set<bool> hasUndoValues{false, true};
    std::set<bool> hasFailedValues{false, true};
    std::set<bool> hasFailedParentValues{false, true};

    for (BlockValidity validity : baseValidities) {
        for (bool hasData : hasDataValues) {
            for (bool hasUndo : hasUndoValues) {
                for (bool hasFailed : hasFailedValues) {
                    for (bool hasFailedParent : hasFailedParentValues) {
                        BlockStatus s =
                            BlockStatus()
                                .withValidity(validity)
                                .withFailed(hasFailed)
                                .withFailedParent(hasFailedParent);
                        s = TestBlockStatus::WithData(s, hasData);
                        s = TestBlockStatus::WithUndo(s, hasUndo);

                        TestBlockStatus::CheckBlockStatus(s, validity, hasData, hasUndo,
                                         hasFailed, hasFailedParent);

                        // Clears failure flags.
                        TestBlockStatus::CheckBlockStatus(s.withClearedFailureFlags(), validity,
                                         hasData, hasUndo, false, false);

                        // Also check all possible alterations.
                        TestBlockStatus::CheckBlockStatus(
                            TestBlockStatus::WithData(s, hasData),
                            validity,
                            hasData,
                            hasUndo,
                            hasFailed,
                            hasFailedParent);
                        TestBlockStatus::CheckBlockStatus(
                            TestBlockStatus::WithData(s, !hasData),
                            validity,
                            !hasData,
                            hasUndo,
                            hasFailed,
                            hasFailedParent);
                        TestBlockStatus::CheckBlockStatus(
                            TestBlockStatus::WithUndo(s, hasUndo),
                            validity,
                            hasData,
                            hasUndo,
                            hasFailed,
                            hasFailedParent);
                        TestBlockStatus::CheckBlockStatus(
                            TestBlockStatus::WithUndo(s, !hasUndo),
                            validity,
                            hasData,
                            !hasUndo,
                            hasFailed,
                            hasFailedParent);
                        TestBlockStatus::CheckBlockStatus(s.withFailed(hasFailed), validity,
                                         hasData, hasUndo, hasFailed,
                                         hasFailedParent);
                        TestBlockStatus::CheckBlockStatus(s.withFailed(!hasFailed), validity,
                                         hasData, hasUndo, !hasFailed,
                                         hasFailedParent);
                        TestBlockStatus::CheckBlockStatus(s.withFailedParent(hasFailedParent),
                                         validity, hasData, hasUndo, hasFailed,
                                         hasFailedParent);
                        TestBlockStatus::CheckBlockStatus(s.withFailedParent(!hasFailedParent),
                                         validity, hasData, hasUndo, hasFailed,
                                         !hasFailedParent);

                        for (BlockValidity newValidity : baseValidities) {
                            TestBlockStatus::CheckBlockStatus(s.withValidity(newValidity),
                                             newValidity, hasData, hasUndo,
                                             hasFailed, hasFailedParent);
                        }
                    }
                }
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
