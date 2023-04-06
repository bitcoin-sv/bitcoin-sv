// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "span.h"

// Parses a p2p message into a single segment - the default for most p2p
// messages.
class single_seg_parser
{
    std::vector<uint8_t> segment_{};

public:
    std::pair<size_t, size_t> operator()(bsv::span<const uint8_t>);
    size_t read(size_t read_pos, bsv::span<uint8_t>);
    size_t size() const;
    void clear(); 
};

