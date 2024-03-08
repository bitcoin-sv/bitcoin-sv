// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "cmpct_size.h"

#include "stream.h"

using namespace std;

std::pair<size_t, uint64_t> parse_compact_size(const span<const uint8_t> s)
{
    if(s.empty())
        return make_pair(0, 1);

    const auto len{cmpt_deser_size(s[0])}; 
    if(len > s.size())
        return make_pair(0, len);

    switch(len)
    {
    case 1:
        return std::make_pair(len, s[0]);
    case 3:
        return std::make_pair(len, ReadLE16(s.data()+1));
    case 5:
        return std::make_pair(len, ReadLE32(s.data()+1));
    case 9:
        return std::make_pair(len, ReadLE64(s.data()+1));
    default:
        assert(false);
    }
}

