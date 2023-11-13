// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "fixed_len_parser.h"

#include <cstdint>

using namespace std;

std::pair<size_t, size_t> fixed_len_parser::operator()(
    const span<const uint8_t> s)
{
    const auto capacity{buffer_.capacity()};
    const auto delta{capacity - buffer_.size()};
    if(s.size() <= delta)
    {
        buffer_.insert(buffer_.cend(), s.begin(), s.end());
        return make_pair(s.size(), capacity - buffer_.size());
    }
    else
    {
        buffer_.insert(buffer_.cend(), s.begin(), s.begin() + delta);
        return make_pair(delta, 0);
    }
}

void fixed_len_parser::reset()
{
    value_type v;
    buffer_.swap(v);
}

