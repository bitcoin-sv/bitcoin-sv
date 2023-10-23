// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include <cstdint>
#include <cassert>
#include <span>
#include <utility>
#include <vector>

#include "unique_array.h"

// reads n bytes at a time up to a max value;
template<size_t N, size_t max_size>
class mod_n_byte_parser
{
    unique_array buffer_;
    size_t max_size_{max_size};

public:
    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s)
    {
        if(buffer_.size() >= max_size)
            return std::make_pair(0, 0);

        const auto old_size{buffer_.size()};
        while(s.size() >= N && (max_size - buffer_.size() >= N))
        {
            buffer_.insert(buffer_.end(), s.begin(), s.begin() + N);
            s = s.subspan(N);
        }
        
        const size_t remainder{s.size() % N};
        return std::make_pair(buffer_.size() - old_size,
                         remainder ? N : 0);
    }

    size_t read(size_t read_pos, std::span<uint8_t>)
    {
        assert(false);
        return 0;
    }

    size_t size() const { return buffer_.size(); }
    void clear() { buffer_.clear(); }

    unique_array buffer() && { return std::move(buffer_); }
};
