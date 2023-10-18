// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "p2p_msg_lengths.h"
#include "unique_array.h"

// Parses a sequence of bytes into multiple segments, each of which contains a fixed number
// of fixed length bytes. 
//
// e.g. The cmpctblock msg contains a sequence of shortids each of length 6 bytes.
//      fixed_lengths_per_seg - determines how many shortids each segment stores.
class fixed_len_multi_parser
{
public:
    fixed_len_multi_parser(size_t fixed_len, size_t fixed_lengths_per_seg):
        fixed_len_{fixed_len},
        fixed_lengths_per_seg_{fixed_lengths_per_seg},
        seg_size_{fixed_len * fixed_lengths_per_seg}
    {}

    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s);
    
    size_t size() const;
    bool empty() const { return size() == 0; }

    const unique_array& operator[](size_t i) const { return segments_[i]; }

    auto begin() const { return segments_.begin(); }
    auto end() const { return segments_.end(); }
    auto cbegin() const { return segments_.cbegin(); }
    auto cend() const { return segments_.cend(); }
    auto begin() { return segments_.begin(); }
    auto end() { return segments_.end(); }

    size_t read(size_t read_pos, std::span<uint8_t>);

    auto segment_count() const { return segments_.size(); }
    std::pair<ptrdiff_t, size_t>  seg_offset(size_t read_pos) const;

    void reset(size_t segment);
    void clear() { segments_.clear(); size_ = 0;}

private:
    std::pair<size_t, size_t> parse_count(std::span<const uint8_t>);
    void init_cum_lengths() const;
    
    std::optional<uint64_t> n_{};
    uint64_t current_{};

    unique_array buffer_;
    using segments_type = std::vector<unique_array>;
    segments_type segments_;

    size_t fixed_len_;
    size_t fixed_lengths_per_seg_;
    size_t seg_size_{fixed_len_ * fixed_lengths_per_seg_};
    size_t size_{};

    mutable std::vector<size_t> cum_lengths_;
};
    
