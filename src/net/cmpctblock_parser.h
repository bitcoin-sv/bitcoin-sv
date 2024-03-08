// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "fixed_len_parser.h"
#include "fixed_len_multi_parser.h"
#include "p2p_msg_lengths.h"
#include "array_parser.h"
#include "prefilled_tx_parser.h"

// Parses a p2p cmpctblock message into a header, a collection of shortid
// segments (each containing 1,000 shortids) and a collection of prefilled_tx
// objects
class cmpctblock_parser
{
    static constexpr size_t nonce_len{8}; 
    fixed_len_parser header_parser_{bsv::block_header_len + nonce_len};
    fixed_len_multi_parser shortid_parser_{6, 1'000};
    array_parser<prefilled_tx_parser> pftxs_parser_;

public:
    [[nodiscard]] std::pair<size_t, size_t> operator()(std::span<const uint8_t> s);
    [[nodiscard]] size_t read(size_t read_pos, std::span<uint8_t>);
    [[nodiscard]] size_t size() const;
    void clear();
};

