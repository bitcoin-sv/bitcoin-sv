// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "array_parser.h"
#include "fixed_len_parser.h"
#include "p2p_msg_lengths.h"
#include "tx_parser.h"

// Parses a p2p blocktxn message into a header and collection of tx objects
class blocktxn_parser
{
    fixed_len_parser header_parser_{bsv::blocktxn_header_len};
    array_parser<tx_parser> txs_parser_;

public:
    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s);
    [[nodiscard]] size_t read(size_t read_pos, std::span<uint8_t>);
    size_t size() const;
    void clear();
};

