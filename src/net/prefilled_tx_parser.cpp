// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#include "prefilled_tx_parser.h"

#include <iostream>

#include "cmpct_size.h"
#include "p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

pair<size_t, size_t> prefilled_tx_parser::operator()(span<const uint8_t> s)
{
    if(s.empty())
        return make_pair(0, var_int_len_1);

    size_t total_bytes_read{};

    if(buffer_.empty())
    {
        const auto [index_bytes_read, index_bytes_reqd]{parse_compact_size(s)};
        total_bytes_read += index_bytes_read;
        if(!index_bytes_read)
            return make_pair(total_bytes_read, index_bytes_reqd);

        buffer_.insert(buffer_.cend(), 
                       s.begin(),
                       s.begin() + index_bytes_read);
        
        s = s.subspan(index_bytes_read);
    }

    const auto [bytes_read, bytes_reqd]{ tx_parser_(s) };
    total_bytes_read += bytes_read;
    return make_pair(total_bytes_read, bytes_reqd);
}
    
std::size_t prefilled_tx_parser::size() const
{
    return buffer_.size() + tx_parser_.size();
}

unique_array prefilled_tx_parser::buffer() &&
{
    unique_array a{std::move(tx_parser_).buffer()};
    buffer_.insert(buffer_.cend(), a.cbegin(), a.cend());
    return std::move(buffer_);
}

