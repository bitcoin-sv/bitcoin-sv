// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "cmpct_size.h"
#include "unique_array.h"

// Parses a counted collection of msg parts into a vector of unique_array objects
// e.g. multiple txs as part of a block or blocktxn message or 
//      multiple prefilledtxs as part of a cmpctblock message.
template<typename T>
class array_parser
{
public:
    using value_type = unique_array;
    using segments_type = std::vector<value_type>;
    using size_type = std::vector<value_type>::size_type;

    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s);
    
    size_t size() const;
    bool empty() const { return size() == 0; }

    const unique_array& operator[](size_t i) const
    {
        if(i >= segments_.size())
        {
            throw std::ios_base::failure("parsing error: index out of bounds");
        }
        return segments_[i];
    }

    auto begin() const { return segments_.begin(); }
    auto end() const { return segments_.end(); }
    auto cbegin() const { return segments_.cbegin(); }
    auto cend() const { return segments_.cend(); }
    auto begin() { return segments_.begin(); }
    auto end() { return segments_.end(); }

    std::pair<ptrdiff_t, size_t>  seg_offset(size_t read_pos) const;

    size_type segment_count() const { return segments_.size(); }

    void reset(size_t segment);

    void clear() { segments_.clear(); size_ = 0;}

private:
    std::pair<size_t, size_t> parse_seg_count(std::span<const uint8_t>);

    void init_cum_lengths() const;
    
    T parser_;
    std::optional<uint64_t> n_{};
    uint64_t current_{};

    segments_type segments_;

    size_type size_{};

    mutable std::vector<size_type> cum_lengths_;
};

template<typename T>
inline std::pair<size_t, size_t> array_parser<T>::parse_seg_count(std::span<const uint8_t> s)
{
    using namespace std;

    assert(!n_.has_value());

    const auto [bytes_read, val] = parse_compact_size(s);
    if(!bytes_read)
        return make_pair(bytes_read, val);

    segments_.push_back(unique_array{s.first(bytes_read)});
    size_ += bytes_read;
    n_ = val;
    return make_pair(bytes_read, 0);
}
    
// return bytes_read, bytes_required
template<typename T>
inline std::pair<size_t, size_t> array_parser<T>::operator()(std::span<const uint8_t> s)
{
    using namespace std;

    size_t total_bytes_read{};

    if(!n_.has_value())
    {
        const auto [bytes_read, bytes_reqd] = parse_seg_count(s);
        total_bytes_read += bytes_read;
        if(bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);

        s = s.subspan(bytes_read);
    }

    while(current_ < n_)
    {
        const auto [bytes_read, bytes_reqd] = parser_(s);
        total_bytes_read += bytes_read;

        if(!bytes_read)
            return make_pair(total_bytes_read, bytes_reqd);

        if(bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);

        s = s.subspan(bytes_read);
        size_ += parser_.size();
        segments_.push_back(std::move(parser_).buffer());
        ++current_;
    }
    
    return make_pair(total_bytes_read, 0);
}

template<typename T>
inline size_t array_parser<T>::size() const
{
    return size_ + parser_.size(); 
}

template<typename T>
inline void array_parser<T>::reset(const size_t segment)
{
    segments_[segment].reset();
}

// converts the read position into an index into the segs and an offset
template<typename T>
inline void array_parser<T>::init_cum_lengths() const
{
    using namespace std;

    assert(cum_lengths_.empty());

    vector<size_t> seg_lengths;
    seg_lengths.reserve(segment_count());

    std::transform(segments_.cbegin(), segments_.cend(),
                   back_inserter(seg_lengths),
                   [](const auto& a)
                   {
                       return a.size();
                   });
    
    cum_lengths_.reserve(seg_lengths.size());
    std::partial_sum(seg_lengths.cbegin(), seg_lengths.cend(), 
                    back_inserter(cum_lengths_));
}

template<typename T>
inline std::pair<ptrdiff_t, size_t> array_parser<T>::seg_offset(const size_t read_pos) const
{
    using namespace std;

    if(cum_lengths_.empty())
        init_cum_lengths();

    if(segment_count() == 1)
        return make_pair(0, read_pos);

    const auto it = lower_bound(cum_lengths_.cbegin(), cum_lengths_.cend(),
                                read_pos + 1);
    const auto seg_offset = std::distance(cum_lengths_.cbegin(), it);
    const size_t byte_offset = read_pos - (seg_offset > 0 ? cum_lengths_[seg_offset-1] : 0);
    return make_pair(seg_offset, byte_offset);
}

