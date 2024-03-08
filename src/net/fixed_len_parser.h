// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#pragma once 

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "unique_array.h"

class fixed_len_parser
{
    unique_array buffer_;

public:
    explicit fixed_len_parser(size_t n):buffer_{n}
    {}

    std::pair<size_t, size_t> operator()(const std::span<const uint8_t>);

    unique_array buffer() && { return std::move(buffer_); }

    bool empty() const { return buffer_.empty(); }
    size_t size() const { return buffer_.size(); }
    size_t capacity() const { return buffer_.capacity(); }

    auto data() const { return buffer_.data(); }
    
    auto cbegin() const { return buffer_.cbegin(); };
    auto cend() const { return buffer_.cend(); };

    auto begin() const { return buffer_.begin(); };
    auto end() const { return buffer_.end(); };

    auto reset() { buffer_.reset(); }
    void clear() { buffer_.clear(); }
};

