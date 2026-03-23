// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "single_seg_parser.h"

std::pair<size_t, size_t> single_seg_parser::operator()(const std::span<const uint8_t> s)
{
    segment_.insert(segment_.end(), s.begin(), s.end());
    return std::make_pair(s.size(), payload_len_ - segment_.size());
};

size_t single_seg_parser::read(const size_t read_pos, std::span<uint8_t> s)
{
    const size_t size{std::min(s.size(), segment_.size() - read_pos)};
    //NOLINTBEGIN(*-narrowing-conversions)
    const auto cbegin{segment_.cbegin() + read_pos};
    copy(cbegin, cbegin + size, s.begin());
    //NOLINTEND(*-narrowing-conversions)
    return size;
}

size_t single_seg_parser::size() const 
{
    return segment_.size();
}

