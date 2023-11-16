// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "serialize.h"
#include "stream_test_helpers.h"
#include "protocol.h"
#include "blockfileinfostore.h"
#include "block_file_access.h"
#include "consensus/validation.h"
#include "config.h"
#include "chainparams.h"
#include "validation.h"
#include "hash.h"

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

#include <exception>
#include <iostream>
#include <string>
#include <optional>
#include <vector>

namespace{ class Unique; }

template <>
struct CBlockIndex::UnitTestAccess<class Unique>
{
    UnitTestAccess() = delete;

    static uint256 CorruptDiskBlockMetaData( CBlockIndex& blockIndex, DirtyBlockIndexStore& notifyDirty )
    {
        uint256 randomHash = InsecureRand256();
        blockIndex.SetDiskBlockMetaData( randomHash, 1, notifyDirty );

        return randomHash;
    }

    static int GetNFile(CBlockIndex& blockIndex)
    {
        return blockIndex.nFile;
    }
};
using TestAccessCBlockIndex = CBlockIndex::UnitTestAccess<class Unique>;

namespace
{
    void WriteBlockToDisk(
        const Config& config,
        const CBlock& block,
        CBlockIndex& index,
        CBlockFileInfoStore& blockFileInfoStore,
        DirtyBlockIndexStore& notifyDirty)
    {
        uint64_t nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        uint64_t nBlockSizeWithHeader =
            nBlockSize
            + GetBlockFileBlockHeaderSize(nBlockSize);
        CDiskBlockPos blockPos;
        CValidationState state;
        bool fCheckForPruning = false;
        if (!blockFileInfoStore.FindBlockPos(
                config,
                state,
                blockPos,
                nBlockSizeWithHeader,
                0,
                block.GetBlockTime(),
                fCheckForPruning))
        {
            throw std::runtime_error{"LoadBlockIndex(): FindBlockPos failed"};
        }

        CDiskBlockMetaData metaData;

        auto res =
            BlockFileAccess::WriteBlockToDisk(
                block,
                blockPos,
                config.GetChainParams().DiskMagic(),
                metaData);

        BOOST_REQUIRE( res );

        index.SetDiskBlockData(block.vtx.size(), blockPos, metaData, CBlockSource::MakeUnknown(), notifyDirty);
    }

    struct CScopeSetupTeardown
    {
        CScopeSetupTeardown(const std::string& testName)
            : path{boost::filesystem::current_path() / "tmp_data" / testName}
        {
            ClearDatadirCache();
            boost::filesystem::create_directories(path);
            if (gArgs.IsArgSet("-datadir"))
            {
                odlDataDir = gArgs.GetArg("-datadir", "");
            }
            gArgs.ForceSetArg("-datadir", path.string());
        }
        ~CScopeSetupTeardown()
        {
            if (odlDataDir)
            {
                gArgs.ForceSetArg("-datadir", odlDataDir.value());
            }
            else
            {
                gArgs.ClearArg("-datadir");
            }
            boost::filesystem::remove_all(path);
        }

        boost::filesystem::path path;
        std::optional<std::string> odlDataDir;
    };
}

BOOST_FIXTURE_TEST_SUITE(blockfile_reading_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(read_without_meta_info)
{

    CScopeSetupTeardown guard{"read_without_meta_info"};
    const DummyConfig config;
    DirtyBlockIndexStore dummyDirty;

    auto block = BuildRandomTestBlock();
    CBlockIndex::TemporaryBlockIndex index{ block };
    CBlockFileInfoStore blockFileInfoStore;
    WriteBlockToDisk(config, block, index, blockFileInfoStore, dummyDirty);

    std::vector<uint8_t> expectedSerializedData{Serialize(block)};

    LOCK(cs_main);

    // check that blockIndex was updated with disk content size and hash data
    {
        auto data = index->StreamBlockFromDisk(INIT_PROTO_VERSION, dummyDirty);

        BOOST_REQUIRE( data.stream );

        std::vector<uint8_t> serializedData{SerializeAsyncStream(*data.stream, 5u)};

        uint256 expectedHash =
            Hash(serializedData.begin(), serializedData.end());

        BOOST_REQUIRE_EQUAL(data.metaData.diskDataSize, serializedData.size());
        BOOST_REQUIRE_EQUAL(
            data.metaData.diskDataHash.GetCheapHash(),
            expectedHash.GetCheapHash());

        BOOST_REQUIRE_EQUAL_COLLECTIONS(
            serializedData.begin(), serializedData.end(),
            expectedSerializedData.begin(), expectedSerializedData.end());
    }

    // corrupt the size and hash, then make sure that they are not changed to
    // confirm that once the data is present it is not updated once again
    // (is read from block index cache instead)
    {
        uint256 randomHash =
            TestAccessCBlockIndex::CorruptDiskBlockMetaData( index, dummyDirty );

        auto streamCorruptMetaData =
            index->StreamBlockFromDisk(INIT_PROTO_VERSION, dummyDirty);
        BOOST_REQUIRE_EQUAL(streamCorruptMetaData.metaData.diskDataSize, 1U);
        BOOST_REQUIRE_EQUAL(
            streamCorruptMetaData.metaData.diskDataHash.GetCheapHash(),
            randomHash.GetCheapHash());
        BOOST_REQUIRE_EQUAL(
            SerializeAsyncStream(*streamCorruptMetaData.stream, 5u).size(),
            1U);
    }
}

BOOST_AUTO_TEST_CASE(delete_block_file_while_reading)
{
    // Test that calling UnlinkPrunedFiles doesn't cause active file streams
    // termination
    CScopeSetupTeardown guard{"delete_block_file_while_reading"};
    const DummyConfig config;
    DirtyBlockIndexStore dummyDirty;

    auto block = BuildRandomTestBlock();
    CBlockIndex::TemporaryBlockIndex index{ block };

    WriteBlockToDisk(config, block, index, *pBlockFileInfoStore, dummyDirty);

    std::vector<uint8_t> expectedSerializedData{Serialize(block)};

    LOCK(cs_main);
    auto data = index->StreamBlockFromDisk(INIT_PROTO_VERSION, dummyDirty);

    BOOST_REQUIRE( data.stream );

    std::vector<uint8_t> serializedData;

    // prepare file ids set for pruning
    std::set<int> fileIds;
    fileIds.insert(TestAccessCBlockIndex::GetNFile(index));

    // start reading and inbetween the read try to delete the file on disk
    {
        auto runStart = std::chrono::steady_clock::now();
        bool deleted = false;

        do
        {
            using namespace std::chrono_literals;

            if ((std::chrono::steady_clock::now() - runStart) > 5s)
            {
                throw std::runtime_error("Test took too long");
            }

            if (!deleted &&
                ((expectedSerializedData.size() / 2) <= serializedData.size()))
            {
                // we're half way through the file so it's time to delete it
                UnlinkPrunedFiles(fileIds);
                deleted = true;
            }

            auto chunk = data.stream->ReadAsync(5u);

            serializedData.insert(
                serializedData.end(),
                chunk.Begin(), chunk.Begin() + chunk.Size());
        }
        while(!data.stream->EndOfStream());
    }

    // Previously called UnlinkPrunedFiles might be unsuccessful because file was still open.
    // With resetting the stream, file is closed and we can perform cleanup.
    // On UNIX, pruning is performed successfully; resetting and unlinking is only needed for Windows.
    data.stream.reset();
    UnlinkPrunedFiles(fileIds);

    BOOST_REQUIRE_EQUAL_COLLECTIONS(
        serializedData.begin(), serializedData.end(),
        expectedSerializedData.begin(), expectedSerializedData.end());
}

BOOST_AUTO_TEST_SUITE_END()
