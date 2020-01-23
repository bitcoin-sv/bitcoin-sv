// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "serialize.h"
#include "stream_test_helpers.h"
#include "protocol.h"
#include "blockfileinfostore.h"
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

namespace
{
    void WriteBlockToDisk(
        const Config& config,
        const CBlock& block,
        CBlockIndex& index,
        CBlockFileInfoStore& blockFileInfoStore)
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

        // Open history file to append
        CAutoFile fileout{
            CDiskFiles::OpenBlockFile(blockPos),
            SER_DISK,
            CLIENT_VERSION};
        if (fileout.IsNull())
        {
            throw std::runtime_error{"WriteBlockToDisk: OpenBlockFile failed"};
        }

        // Write index header
        //CMessageHeader::MessageMagic diskMagic{{0xde, 0xad, 0xbe, 0xef}};
        const CChainParams& chainparams = config.GetChainParams();
        unsigned int nSize = GetSerializeSize(fileout, block);
        fileout << FLATDATA(chainparams.DiskMagic()) << nSize;

        // Write block
        long fileOutPos = ftell(fileout.Get());
        if (fileOutPos < 0) {
            throw std::runtime_error{"WriteBlockToDisk: ftell failed"};
        }

        // set index data
        index.nFile = blockPos.nFile;
        index.nDataPos = fileOutPos;
        index.nUndoPos = 0;
        index.nStatus = index.nStatus.withData();

        fileout << block;
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

    auto block = BuildRandomTestBlock();
    CBlockIndex index{block};
    std::unique_ptr<uint256> randomHash(new uint256(InsecureRand256()));
    index.phashBlock = randomHash.get();

    CBlockFileInfoStore blockFileInfoStore;
    WriteBlockToDisk(config, block, index, blockFileInfoStore);

    std::vector<uint8_t> expectedSerializedData{Serialize(block)};

    LOCK(cs_main);

    // check that blockIndex was updated with disk content size and hash data
    {
        auto stream = StreamBlockFromDisk(index, INIT_PROTO_VERSION);
        std::vector<uint8_t> serializedData{SerializeAsyncStream(*stream, 5u)};

        uint256 expectedHash =
            Hash(serializedData.begin(), serializedData.end());

        auto metaData = index.GetDiskBlockMetaData();
        BOOST_REQUIRE_EQUAL(metaData.diskDataSize, serializedData.size());
        BOOST_REQUIRE_EQUAL(
            metaData.diskDataHash.GetCheapHash(),
            expectedHash.GetCheapHash());

        BOOST_REQUIRE_EQUAL_COLLECTIONS(
            serializedData.begin(), serializedData.end(),
            expectedSerializedData.begin(), expectedSerializedData.end());
    }

    // corrupt the size and hash, then make sure that they are not changed to
    // confirm that once the data is present it is not updated once again
    // (is read from block index cache instead)
    {
        uint256 randomHash = GetRandHash();
        index.SetDiskBlockMetaData(randomHash, 1);

        auto streamCorruptMetaData =
            StreamBlockFromDisk(index, INIT_PROTO_VERSION);
        auto metaData = index.GetDiskBlockMetaData();
        BOOST_REQUIRE_EQUAL(metaData.diskDataSize, 1);
        BOOST_REQUIRE_EQUAL(
            metaData.diskDataHash.GetCheapHash(),
            randomHash.GetCheapHash());
        BOOST_REQUIRE_EQUAL(
            SerializeAsyncStream(*streamCorruptMetaData, 5u).size(),
            1);
    }
}

BOOST_AUTO_TEST_CASE(delete_block_file_while_reading)
{
    // Test that calling UnlinkPrunedFiles doesn't cause active file streams
    // termination
    CScopeSetupTeardown guard{"delete_block_file_while_reading"};
    const DummyConfig config;

    auto block = BuildRandomTestBlock();
    CBlockIndex index{block};
    std::unique_ptr<uint256> randomHash(new uint256(InsecureRand256()));
    index.phashBlock = randomHash.get();

    WriteBlockToDisk(config, block, index, *pBlockFileInfoStore);

    std::vector<uint8_t> expectedSerializedData{Serialize(block)};

    LOCK(cs_main);
    auto stream = StreamBlockFromDisk(index, INIT_PROTO_VERSION);
    std::vector<uint8_t> serializedData;

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
                std::set<int> fileIds;
                fileIds.insert(index.nFile);
                UnlinkPrunedFiles(fileIds);
                deleted = true;
            }

            auto chunk = stream->ReadAsync(5u);

            serializedData.insert(
                serializedData.end(),
                chunk.Begin(), chunk.Begin() + chunk.Size());
        }
        while(!stream->EndOfStream());
    }

    BOOST_REQUIRE_EQUAL_COLLECTIONS(
        serializedData.begin(), serializedData.end(),
        expectedSerializedData.begin(), expectedSerializedData.end());
}

BOOST_AUTO_TEST_SUITE_END()
