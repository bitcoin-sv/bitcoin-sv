// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "disk_backed_parser.h"
#include "logging.h"
#include "tinyformat.h"

#include <atomic>
#include <stdexcept>

namespace
{
    // Counter for unique filenames
    std::atomic<uint64_t> s_next_id {0};
}

disk_backed_parser::disk_backed_parser(uint64_t payload_len)
    : payload_len_{payload_len}
{
    // Validate payload length against stream capabilities
    if (payload_len > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
    {
        throw std::runtime_error("Payload size exceeds streamsize maximum");
    }

    // Get unique ID for this message
    uint64_t id = s_next_id.fetch_add(1, std::memory_order_relaxed);

    // Construct filename: msg_{counter}.tmp
    file_path_ = unique_path { g_p2p_recv_tmp_dir / strprintf("msg_%llu.tmp", id) };

    // Open file in binary write mode
    file_stream_.open(file_path_.get(), std::ios::out | std::ios::binary | std::ios::trunc);
    if(!file_stream_.is_open())
    {
        throw std::runtime_error(
            strprintf("Failed to open disk file for P2P message: %s",
                file_path_.get().string()));
    }

    LogPrint(BCLog::NET, "Creating disk-backed parser for message, size=%llu bytes, file=%s\n",
        payload_len, file_path_.get().string());
}

disk_backed_parser& disk_backed_parser::operator=(disk_backed_parser&& that) noexcept
{
    if(this != &that)
    {
        // Ensure file_path doesn't get overwritten (and file deleted) before we move the file stream
        file_stream_ = std::move(that.file_stream_);
        file_path_ = std::move(that.file_path_);
        payload_len_ = that.payload_len_;
        bytes_written_ = that.bytes_written_;
    }

    return *this;
}

std::pair<size_t, size_t> disk_backed_parser::operator()(const std::span<const uint8_t> s)
{
    if(bytes_written_ >= payload_len_)
    {
        return {0, 0};
    }
    if(s.empty())
    {
        return {0, static_cast<size_t>(payload_len_ - bytes_written_)};
    }

    // Calculate how many bytes we can write
    std::streamsize bytes_to_write = static_cast<std::streamsize>(std::min<uint64_t>(s.size(), payload_len_ - bytes_written_));

    // Write to file
    file_stream_.write(reinterpret_cast<const char*>(s.data()), bytes_to_write);

    if(!file_stream_.good())
    {
        throw std::runtime_error(
            strprintf("Failed to write to disk file for P2P message: %s",
                file_path_.get().string()));
    }

    bytes_written_ += bytes_to_write;

    // Close the write stream when all data has been received
    if(bytes_written_ == payload_len_)
    {
        file_stream_.close();
        if(file_stream_.fail())
        {
            throw std::runtime_error(
                strprintf("Failed to close disk file after writing P2P message: %s",
                    file_path_.get().string()));
        }
        LogPrint(BCLog::NET, "Disk message write complete: file=%s, bytes=%llu\n",
            file_path_.get().string(), bytes_written_);
    }

    size_t bytes_remaining = payload_len_ - bytes_written_;
    return {bytes_to_write, bytes_remaining};
}

size_t disk_backed_parser::read(size_t read_pos, const std::span<uint8_t> s)
{
    constexpr std::streamsize max_streamsize = std::numeric_limits<std::streamsize>::max();
    static_assert(max_streamsize <= std::numeric_limits<size_t>::max(), "streamsize must fit in size_t");

    if(s.empty())
    {
        return 0;
    }
    if(read_pos >= bytes_written_)
    {
        throw std::runtime_error("disk_backed_parser::read() end of data");
    }

    // Open file for reading on first read call (write stream was closed on completion)
    if(!file_stream_.is_open())
    {
        file_stream_.clear();
        file_stream_.open(file_path_.get(), std::ios::in | std::ios::binary);
        if(!file_stream_.is_open())
        {
            throw std::runtime_error(
                strprintf("Failed to open disk file for reading P2P message: %s",
                    file_path_.get().string()));
        }
    }

    file_stream_.seekg(read_pos); // NOLINT(*-narrowing-conversions)
    if(!file_stream_.good())
    {
        throw std::runtime_error(
            strprintf("Failed to seek in disk file for P2P message: %s at pos=%zu",
                file_path_.get().string(), read_pos));
    }

    // Calculate how many bytes we can read
    uint64_t bytes_available = bytes_written_ - read_pos;
    uint64_t bytes_to_read_max = std::min<uint64_t>(s.size(), bytes_available);
    //NOLINTNEXTLINE(*-narrowing-conversions)
    std::streamsize bytes_to_read = std::min(bytes_to_read_max, static_cast<uint64_t>(max_streamsize));

    // Read from file
    file_stream_.read(reinterpret_cast<char*>(s.data()), bytes_to_read);
    std::streamsize bytes_read = file_stream_.gcount();

    if(bytes_read != bytes_to_read)
    {
        throw std::runtime_error(
            strprintf("Failed to read from disk file for P2P message: %s, expected=%zu, got=%zu",
                file_path_.get().string(), bytes_to_read, bytes_read));
    }

    return static_cast<size_t>(bytes_read);
}

disk_backed_parser::unique_path::~unique_path()
{
    delete_file();
}

disk_backed_parser::unique_path::unique_path(unique_path&& that) noexcept
: path_ { std::move(that.path_) }
{
    // Clear the moved-from object to prevent destructor from deleting the file
    that.path_.clear();
}

disk_backed_parser::unique_path& disk_backed_parser::unique_path::operator=(unique_path&& that) noexcept
{
    if(this != &that)
    {
        // Remove any previous file we had open
        delete_file();

        path_ = std::move(that.path_);

        // Clear the moved-from object to prevent destructor from deleting the file
        that.path_.clear();
    }

    return *this;
}

// Helper function to delete file with error handling
void disk_backed_parser::unique_path::delete_file() noexcept
{
    try
    {
        // Delete temporary file
        if(!path_.empty())
        {
            std::filesystem::remove(path_);
        }
    }
    catch(const std::exception& e)
    {
        LogPrintf("Error: Failed to delete temporary message file %s: %s\n",
            path_.string(), e.what());
    }
}

