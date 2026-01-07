// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include <cstdint>
#include <cassert>
#include <span>
#include <utility>
#include <vector>

// reads n bytes at a time up to a max value;
template<size_t N, size_t max_size>
class mod_n_byte_parser
{
public:
    using value_type = std::vector<uint8_t>;

    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s)
    {
        if(buffer_.size() >= max_size)
            return std::make_pair(0, 0);

        const auto old_size{buffer_.size()};


        while(s.size() >= N && (buffer_.size() + N <= max_size_))
        {
            buffer_.insert(buffer_.end(), s.begin(), s.begin() + N);
            s = s.subspan(N);
        }
        
        return std::make_pair(buffer_.size() - old_size,
                              buffer_.size() >= max_size_ ? 0 : N);
    }

    [[nodiscard]] std::size_t readable_size() const
    {
        return buffer_.size(); 
    }

    size_t read(size_t /*read_pos*/, std::span<uint8_t>)
    {
        assert(false);
        return 0;
    }

    size_t size() const { return buffer_.size(); }
    void clear() { buffer_.clear(); }

    value_type buffer() && { return std::move(buffer_); }

private:
    value_type buffer_;
    size_t max_size_{max_size};

};
