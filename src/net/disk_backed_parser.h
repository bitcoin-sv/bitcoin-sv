// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include "fs.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

// Global directory path for temporary P2P message storage
inline fs::path g_p2p_recv_tmp_dir; //NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Parser that stores large message payloads on disk during reception
// and reads from disk during processing. Implements the msg_parser interface.
class disk_backed_parser
{
    // Wrapper class for a unique temporary file path that ensures cleanup on destruction
    class unique_path
    {
        std::filesystem::path path_ {};

        void delete_file() noexcept;

      public:
        unique_path() = default;
        explicit unique_path(const fs::path& path) : path_{path.string()} {}
        ~unique_path();

        unique_path(unique_path&& that) noexcept;
        unique_path& operator=(unique_path&& that) noexcept;

        // Disable copy
        unique_path(const unique_path&) = delete;
        unique_path& operator=(const unique_path&) = delete;

        const std::filesystem::path& get() const { return path_; }
    };

    // The order is important here: file_path_ must be destroyed after file_stream_
    // to ensure the file is closed before deletion.
    unique_path file_path_ {};
    std::fstream file_stream_ {};
    uint64_t payload_len_ {0};
    uint64_t bytes_written_ {0};

public:
    explicit disk_backed_parser(uint64_t payload_len);
    ~disk_backed_parser() = default;

    disk_backed_parser(disk_backed_parser&& that) noexcept = default;
    disk_backed_parser& operator=(disk_backed_parser&& that) noexcept;

    // Disable copy
    disk_backed_parser(const disk_backed_parser&) = delete;
    disk_backed_parser& operator=(const disk_backed_parser&) = delete;

    // msg_parser interface
    // Write incoming bytes to disk, returns (bytes_written, bytes_remaining)
    [[nodiscard]] std::pair<size_t, size_t> operator()(const std::span<const uint8_t> s);

    // Read bytes from disk at specified position
    [[nodiscard]] size_t read(size_t read_pos, const std::span<uint8_t> s);

    // Total size of data written
    [[nodiscard]] uint64_t size() const { return bytes_written_; }

    // Amount of data available to read (same as size for this parser)
    [[nodiscard]] uint64_t readable_size() const { return bytes_written_; }
};

