// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "fixed_len_parser.h"

using namespace std;

std::pair<size_t, size_t> fixed_len_parser::operator()(
    const span<const uint8_t> s)
{
    const auto capacity{buffer_.capacity()};
    const auto delta{capacity - buffer_.size()};
    if(s.size() <= delta)
    {
        buffer_.append(s);
        return make_pair(s.size(), capacity - buffer_.size());
    }
    else
    {
        buffer_.append(s.first(delta));
        return make_pair(delta, 0);
    }
}

