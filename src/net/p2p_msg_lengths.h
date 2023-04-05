// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cstddef>

namespace bsv
{
    static constexpr size_t msg_header_len{24};
    static constexpr size_t ext_msg_header_len{44};
    static constexpr size_t magic_bytes_len{4};
    static constexpr size_t cmd_len{12};
    static constexpr size_t version_len{4};
    static constexpr size_t outpoint_len{36};
    static constexpr size_t seq_len{4};
    static constexpr size_t value_len{8};
    static constexpr size_t locktime_len{4};

    static constexpr size_t var_int_len_1{1};
    static constexpr size_t var_int_len_3{3};
    static constexpr size_t var_int_len_5{5};
    static constexpr size_t var_int_len_9{9};
    
    static constexpr size_t block_header_len{80};
    static constexpr size_t blocktxn_header_len{32};
}
