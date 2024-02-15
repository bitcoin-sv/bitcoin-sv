// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "blockindex_with_descendants.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <random>


namespace{ class Unique; }

template<>
struct CBlockIndex::PrivateTag::UnitTestAccess<class Unique>
{
public:
    static PrivateTag GetPrivateTag() { return PrivateTag{}; }
};
using TestAccessCBlockIndexPrivateTag = CBlockIndex::PrivateTag::UnitTestAccess<class Unique>;

template <>
struct CBlockIndex::UnitTestAccess<class Unique>
{
    UnitTestAccess() = delete;

    static void SetVersion( std::unique_ptr<CBlockIndex>& index, int32_t version )
    {
        index->nVersion = version;
    }

    static void SetHeight( std::unique_ptr<CBlockIndex>& index, int32_t height)
    {
        index->nHeight = height;
    }

    static void SetPrev( std::unique_ptr<CBlockIndex>& index, CBlockIndex* prev)
    {
        index->pprev = prev;
    }

};
using TestAccessCBlockIndex = CBlockIndex::UnitTestAccess<class Unique>;

BOOST_AUTO_TEST_SUITE(blockindex_with_descendants_tests)

// Helper to create block with given id and parent
std::unique_ptr<CBlockIndex> CreateBlockIndex(std::int32_t id, CBlockIndex* pprev)
{
    std::unique_ptr<CBlockIndex> bi = std::make_unique<CBlockIndex>(TestAccessCBlockIndexPrivateTag::GetPrivateTag());
    TestAccessCBlockIndex::SetVersion(bi,id); // member nVersion is (ab)used to store id of a block to simplify referencing it
    TestAccessCBlockIndex::SetPrev(bi, pprev);
    if(bi->GetPrev())
    {
        TestAccessCBlockIndex::SetHeight(bi, bi->GetPrev()->GetHeight() + 1);
    }
    else
    {
        TestAccessCBlockIndex::SetHeight(bi, 0);
    }
    return bi;
}

// Just like std::set but can be also be streamed to provide better unit test diagnostics
template<class T>
class sset : public std::set<T>
{
public:
    using std::set<T>::set;

    friend std::ostream& operator<<(std::ostream& os, const sset& set)
    {
        os<<"{";
        bool first=true;
        for(auto& o: set)
        {
            if(first) { first=false; } else { os<<","; }
            os<<o;
        }
        os<<"}";

        return os;
    }
};

// Helper to store an array of CBlockIndex objects and provide
// access via block id stored in nVersion data member.
struct BlockIndexStorage
{
    BlockIndexStorage(std::size_t max_size)
    {
        // reserve enough space so that relocation will not happen when inserting new objects and all pointers remain valid
        storage.reserve(max_size);
    }

    void Add(std::unique_ptr<CBlockIndex> block)
    {
        assert(storage.size()<storage.capacity());
        storage.emplace_back(std::move(block));
        index.emplace(storage.back().get()->GetVersion(), storage.back().get());
        mapBlockIndex.emplace_back(0, storage.back().get());
    };

    template<class Func>
    void ForEach(Func callback) const
    {
        for (auto& item : mapBlockIndex)
        {
            callback( *item.second );
        }
    }

    // Access to CBlockIndex pointer from its id
    CBlockIndex* operator[](std::int32_t id) const
    {
        auto it = index.find(id);
        if(it==index.end())
        {
            return nullptr;
        }
        return it->second;
    }

    // Array with block index pointers that is passed to BlockIndexWithDescendants constructor.
    // This is a separate array, so that we can shuffle the elements to check that everything works
    // correctly regardless of the order of block index pointers.
    std::vector< std::pair<int, CBlockIndex*> > mapBlockIndex; // first object in pair is not used

private:
    std::vector<std::unique_ptr<CBlockIndex>> storage;
    std::map<std::int32_t, CBlockIndex*> index;
};

// Test basic functionality of class BlockIndexWithDescendants
BOOST_AUTO_TEST_CASE(basic) {

    /*
     * Create the following hierarchy of CBlockIndex objects:
     *      0
     *      |
     *      1
     *     / \
     *    2   8
     *   /|\   \
     *  3 4 6   9
     *    | |
     *    5 7
     *
     * Id of block is chosen to represent order of traversal if the order of children
     * is as shown above.
     */
    static constexpr std::size_t N = 10; // number of blocks
    BlockIndexStorage b(N);
    b.Add(CreateBlockIndex(0, nullptr));
    b.Add(CreateBlockIndex(1, b[0]));
    b.Add(CreateBlockIndex(2, b[1]));
    b.Add(CreateBlockIndex(8, b[1]));
    b.Add(CreateBlockIndex(9, b[8]));
    b.Add(CreateBlockIndex(3, b[2]));
    b.Add(CreateBlockIndex(4, b[2]));
    b.Add(CreateBlockIndex(6, b[2]));
    b.Add(CreateBlockIndex(5, b[4]));
    b.Add(CreateBlockIndex(7, b[6]));

    // Sanity check that we created all blocks we intended
    BOOST_REQUIRE_EQUAL( b.mapBlockIndex.size(), N );

    ResetGlobalRandomContext();
    // Perform all checks for various order of block index objects
    std::mt19937 random(insecure_rand()); // always use the same seed for consistent results
    for(int i=0; i<1000; ++i)
    {
        // Create BlockIndexWithDescendants object for specified block, iterate over all descendant blocks,
        // and return an array of block ids that were visited.
        // Also check that each block is not visited more than once, and that parents are visited before their children.


        using TraverseResult = sset<std::int32_t>;
        auto Traverse = [&b](CBlockIndex* root_block, std::int32_t maxHeight=std::numeric_limits<std::int32_t>::max()) {
            BlockIndexWithDescendants blocks{root_block, b, maxHeight};

            BOOST_REQUIRE_EQUAL( blocks.Root()->BlockIndex(), root_block );
            BOOST_REQUIRE_EQUAL( blocks.Root()->Parent(), nullptr );

            TraverseResult result;
            for(auto* item=blocks.Root(); item!=nullptr; item=item->Next())
            {
                // Each block must only be visited once
                BOOST_REQUIRE_EQUAL(result.count(item->BlockIndex()->GetVersion()), 0U);

                if(item->BlockIndex() != root_block)
                {
                    // Parent of each block (except the root) must have been visited before
                    BOOST_REQUIRE_EQUAL(result.count(item->BlockIndex()->GetPrev()->GetVersion()), 1U);
                }

                result.insert(item->BlockIndex()->GetVersion());
            }

            return result;
        };

        // Check that BlockIndexWithDescendants is properly created and that traversal works for any block selected as root.
        // NOTE: We cannot check the exact traversal order because the order of children is unspecified.
        BOOST_REQUIRE_EQUAL( Traverse(b[0]), (TraverseResult{0,1,2,3,4,5,6,7,8,9}) );
        BOOST_REQUIRE_EQUAL( Traverse(b[1]), (TraverseResult{  1,2,3,4,5,6,7,8,9}) );
        BOOST_REQUIRE_EQUAL( Traverse(b[2]), (TraverseResult{    2,3,4,5,6,7    }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[3]), (TraverseResult{      3            }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[4]), (TraverseResult{        4,5        }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[5]), (TraverseResult{          5        }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[6]), (TraverseResult{            6,7    }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[7]), (TraverseResult{              7    }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[8]), (TraverseResult{                8,9}) );
        BOOST_REQUIRE_EQUAL( Traverse(b[9]), (TraverseResult{                  9}) );

        // Check handling of maxHeight parameter
        BOOST_REQUIRE_EQUAL( Traverse(b[0], 3), (TraverseResult{0,1,2,3,4,  6,  8,9}) );
        BOOST_REQUIRE_EQUAL( Traverse(b[0], 2), (TraverseResult{0,1,2,          8  }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[1], 2), (TraverseResult{  1,2,          8  }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[1], 1), (TraverseResult{  1                }) );
        BOOST_REQUIRE_EQUAL( Traverse(b[1], 0), (TraverseResult{  1                }) ); // if we set maxHeight below the root block we should still get root block
        BOOST_REQUIRE_EQUAL( Traverse(b[2], 3), (TraverseResult{    2,3,4,  6,     }) );

        // Change the order of block index pointers and try again
        std::shuffle(b.mapBlockIndex.begin(), b.mapBlockIndex.end(), random);
    }
}

// Test BlockIndexWithDescendants with large number of blocks and show
// some timing info that can be used for quick performance evaluation.
BOOST_AUTO_TEST_CASE(large) {

    // Create large number of block index objects
    static constexpr std::int32_t N = 1000000; // number of blocks
    BlockIndexStorage b(N);
    b.Add(CreateBlockIndex(0, nullptr));
    for(std::int32_t i=1; i<N; ++i)
    {
        b.Add(CreateBlockIndex(i, b[i-1]));
    }

    // Sanity check that we created all blocks we intended
    BOOST_REQUIRE_EQUAL( b.mapBlockIndex.size(), static_cast<uint32_t>(N) );

    using clock = std::chrono::steady_clock;

    // Find descendants of block 10 blocks from the tip
    auto tp0=clock::now();
    BlockIndexWithDescendants blocks{b[N-10], b, std::numeric_limits<std::int32_t>::max()};
    BOOST_TEST_MESSAGE("Finding descendants of block that is 10 blocks from the tip took: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now()-tp0 ).count())+"ms" );

    // Check that we got all of them
    std::int32_t count=0;
    for(auto* item=blocks.Root(); item!=nullptr; item=item->Next())
    {
        BOOST_CHECK_EQUAL(item->BlockIndex()->GetHeight(), count+N-10);
        ++count;
    }
    BOOST_CHECK_EQUAL(count, 10);

    // Find all descendants of genesis block
    tp0=clock::now();
    blocks = BlockIndexWithDescendants {b[0], b, std::numeric_limits<std::int32_t>::max()};
    BOOST_TEST_MESSAGE("Finding all descendants ("+std::to_string(N)+") of genesis block descendants took: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now()-tp0 ).count())+"ms" );

    // Iterate over all descendants
    tp0=clock::now();
    count=0;
    for(auto* item=blocks.Root(); item!=nullptr; item=item->Next())
    {
        assert(item->BlockIndex()->GetHeight() == count); // using assert because BOOST_CHECK_EQUAL is slow
        ++count;
    }
    BOOST_CHECK_EQUAL(count, N);
    BOOST_TEST_MESSAGE("Iterating over " + std::to_string(N) + " block descendants took: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now()-tp0 ).count())+"ms" );
}

BOOST_AUTO_TEST_SUITE_END()
