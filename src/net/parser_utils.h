// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <ios>
#include <span>
#include <vector>

inline size_t read(const std::vector<uint8_t>& a,
                   size_t read_pos,
                   std::span<uint8_t> s)
{
    if(a.empty())
        return 0;

    //assert(read_pos < a.size());
    if(read_pos >= a.size())
        throw std::ios_base::failure("read(): invalid read position");


    const auto n{std::min(a.size() - read_pos, s.size())};
    copy(a.cbegin() + read_pos,     //NOLINT(*-narrowing-conversions)
         a.cbegin() + read_pos + n, //NOLINT(*-narrowing-conversions)
         s.begin()); 
    return n; 
}

// reads from a parser and if it reaches the end of a parser's 
// segment resets the segment.
// Preconditions
// 0 <= read_pos < total size of all parser segments
template<typename T>
[[nodiscard]] size_t read(T& parser, size_t read_pos, std::span<uint8_t> s)
{
    if(parser.empty())
        return 0;

    size_t total_bytes_read{};
    const auto max_readable{std::min(parser.readable_size(), s.size())};
    auto [seg_offset, byte_offset] = parser.seg_offset(read_pos);
    while(total_bytes_read < max_readable)
    {
        const auto& seg{parser.at(seg_offset)};
        const auto seg_bytes_remaining{seg.size() - byte_offset};
        const auto n_bytes{std::min(seg_bytes_remaining, s.size())};
        const auto bytes_read = read(seg, byte_offset, s.first(n_bytes));
        s = s.subspan(bytes_read);
        total_bytes_read += bytes_read;
        if(bytes_read == seg_bytes_remaining)
        {
            parser.reset(seg_offset);
            ++seg_offset;
            byte_offset = 0;
        }
    }
    return total_bytes_read;
}

