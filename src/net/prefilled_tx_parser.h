// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "tx_parser.h"

// Parses a prefilled tx as defined in the HeaderAndShortIDs section of a
// p2p cmpctblock message.
class prefilled_tx_parser
{
    tx_parser tx_parser_;
    unique_array buffer_;

public:
    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s);
    std::size_t size() const;
    
    unique_array buffer() &&;
};

