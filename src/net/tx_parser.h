// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "unique_array.h"

// Parses a p2p message into a segment containing a single tx.
// As the tx may be parsed over several invocations of op() the 
// class must maintain state information.
// To avoid reallocation and copying, individual parts of the transaction are
// buffered separately and coalesced into a single buffer once parsing is
// complete.
class tx_parser
{
public:
    enum class state 
    {
        version,
        ip_count,
        ips_,
        op_count,
        ops_,
        lock_time,
        complete
    };

    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s);
    
    size_t buffer_size() const;
    size_t size() const;
    bool empty() const { return size() == 0; }
    void clear() { size_ = 0;}

    unique_array buffer() &&;
    
    friend std::ostream& operator<<(std::ostream&, const state&);

private:
    std::pair<size_t, size_t> parse_tx_count(std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_version(std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_ip_count(std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_inputs(std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_input(const std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_op_count(std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_outputs(std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_output(const std::span<const uint8_t>);
    std::pair<size_t, size_t> parse_locktime(std::span<const uint8_t>);

    state state_{state::version};

    uint64_t n_ips_{};
    uint64_t current_ip_{};

    uint64_t n_ops_{};
    uint64_t current_op_{};

    std::optional<uint64_t> script_len_{std::nullopt};

    std::vector<uint8_t> version_buffer_;
    std::vector<uint8_t> ip_count_buffer_;
    std::vector<std::vector<uint8_t>> ip_buffers_;
    std::vector<uint8_t> op_count_buffer_;
    std::vector<std::vector<uint8_t>> op_buffers_;
    std::vector<uint8_t> locktime_buffer_;

    unique_array buffer_;
    size_t size_{};
};

