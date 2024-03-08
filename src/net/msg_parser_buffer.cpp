// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "msg_parser_buffer.h"

#include <cassert>
#include <iostream>
#include <numeric>

using namespace std;

// Always read all the bytes of input, by the parser or into the buffer.
void msg_parser_buffer::operator()(span<const uint8_t> s)
{
    if(overflow_)
    {
        buffer_.insert(buffer_.cend(),
                       s.begin(), 
                       s.end());
        return;
    }

    if(!buffer_.empty())
    {
        size_t bytes_read{};
        while(!bytes_read)
        {
            const size_t reqd_bytes = min(s.size(), buffer_size_reqd_ - buffer_.size());
            buffer_.append(s.first(reqd_bytes));
            s = s.subspan(reqd_bytes);
            if(buffer_.size() < buffer_size_reqd_)
                return;

            span ss(buffer_.data(), buffer_.size());
            const auto [bytes_read, bytes_reqd] = (*parser_)(ss);
            if(bytes_read == buffer_.size())
            {
                buffer_.clear();
                buffer_size_reqd_ = 0;
                if(s.empty())
                    return;
                break;
            }

            if(bytes_read == 0 && bytes_reqd == 0)
            {
                overflow_ = true;
                buffer_.insert(buffer_.cend(),
                               s.begin(),
                               s.end());
                bytes_read_ += s.size();
                return;
            }

            assert(bytes_read == 0);
            assert(bytes_reqd > buffer_size_reqd_);
            buffer_size_reqd_ = bytes_reqd;
        }
    }

    const auto [bytes_read, bytes_reqd]{(*parser_)(s)};
    if(bytes_read == 0 && bytes_reqd == 0)
    {
        overflow_ = true;
        buffer_.insert(buffer_.cend(),
                       s.begin(),
                       s.end());
        bytes_read_ += s.size();
        return;
    }

    bytes_read_ = bytes_read;
    const size_t remaining_input_len{s.size() - bytes_read};
    buffer_size_reqd_ = bytes_reqd ? bytes_reqd : remaining_input_len;
    if(remaining_input_len) 
    {
        buffer_.insert(buffer_.cend(),
                       s.begin() + bytes_read,
                       s.end());
    }
}

size_t msg_parser_buffer::read(size_t read_pos, span<uint8_t> s)
{
    return parser_->read(read_pos, s);
}

size_t msg_parser_buffer::size() const
{
    return parser_->size() + buffer_.size();
}

size_t msg_parser_buffer::parsed_size() const
{
    return parser_->size();
}

void msg_parser_buffer::clear()
{
    parser_->clear();
    buffer_.clear();
    buffer_size_reqd_ = 0; 
}

