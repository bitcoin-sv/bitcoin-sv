// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "fixed_len_multi_parser.h"

#include <algorithm>
#include <boost/token_functions.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <thread>
#include <unistd.h>
#include <utility>

#include "cmpct_size.h"
#include "msg_parser.h"
#include "parser_utils.h"
#include "p2p_msg_lengths.h"
#include "rpc/blockchain.h"
#include "streams.h"
#include "unique_array.h"
#include "util.h"

using namespace std;

std::pair<size_t, size_t> fixed_len_multi_parser::parse_count(span<const uint8_t> s)
{
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
std::pair<size_t, size_t> fixed_len_multi_parser::operator()(span<const uint8_t> s)
{
    size_t total_bytes_read{};

    if(!n_.has_value())
    {
        const auto [bytes_read, bytes_reqd] = parse_count(s);
        total_bytes_read += bytes_read;
        if(bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);

        s = s.subspan(bytes_read);
    }

    if(current_ >= n_.value())
        return make_pair(total_bytes_read, 0);
    
    const auto max_fixed_lens{ numeric_limits<uint64_t>::max() / fixed_len_ };
       
    // Add any bytes that are given to the buffer, but only create
    // a new segment when the buffer has read the min seg size
    while(s.size() >= fixed_len_)
    {
        const auto fixed_lens_reqd{n_.value() - current_};
        const auto n_fixed_lens{min(fixed_lens_reqd, max_fixed_lens)}; 
        const auto bytes_reqd{ n_fixed_lens * fixed_len_ };

        const size_t seg_bytes_reqd{seg_size_ - buffer_.size()};
        const size_t min_bytes_reqd{min(seg_bytes_reqd, bytes_reqd)};
        const size_t n_bytes{min(s.size(), min_bytes_reqd)};
        const size_t quotient{(n_bytes / fixed_len_) * fixed_len_};
        buffer_.insert(buffer_.cend(), 
                       s.begin(), s.begin() + quotient);
        size_ += quotient;
        current_ += quotient / fixed_len_;
        total_bytes_read += quotient;
        assert(buffer_.size() <= seg_size_);
        
        if(buffer_.size() == seg_size_ || 
           (current_ >= n_ && !buffer_.empty()))
        {
            segments_.insert(segments_.end(), std::move(buffer_));
            buffer_.reserve(seg_size_);

            if(current_ >= n_)
                break;
        }
        
        s = s.subspan(quotient);
    }

    const auto fixed_lens_reqd{n_.value() - current_};
    const auto n_fixed_lens{min(fixed_lens_reqd, max_fixed_lens)}; 
    const auto bytes_reqd{ n_fixed_lens * fixed_len_ };
    return make_pair(total_bytes_read, bytes_reqd);
}

size_t fixed_len_multi_parser::size() const
{
    return size_;
}
    
size_t fixed_len_multi_parser::read(size_t read_pos, std::span<uint8_t> s)
{
    return ::read(*this, read_pos, s);
}

void fixed_len_multi_parser::reset(const size_t segment)
{
    segments_[segment].reset();
}

// converts the read position into an index into the segments and an offset
void fixed_len_multi_parser::init_cum_lengths() const
{
    assert(cum_lengths_.empty());

    vector<size_t> seg_lengths;
    seg_lengths.reserve(segment_count());

    std::transform(segments_.cbegin(), segments_.cend(),
                   back_inserter(seg_lengths),
                   [](const auto& a)
                   {
                       return a.size();
                   });
    
    std::partial_sum(seg_lengths.cbegin(), seg_lengths.cend(), 
                     back_inserter(cum_lengths_));
}

std::pair<ptrdiff_t, size_t> fixed_len_multi_parser::seg_offset(const size_t read_pos) const
{
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

