// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "msg_parser_buffer.h"

#include <cassert>
#include <ios>

using namespace std;

// Always read all the bytes of input, by the parser or into the buffer.
void msg_parser_buffer::operator()(span<const uint8_t> s)
{
    if(parser_full_)
    {
        buffer_.insert(buffer_.end(), s.begin(), s.end());
        return;
    }

    if(!buffer_.empty())
    {
        while(!s.empty())
        {
            // Fill up the buffer to the required level
            const size_t reqd_bytes = min(s.size(), buffer_size_reqd_ - buffer_.size());
            buffer_.insert(buffer_.cend(), s.begin(),
                           s.begin() + reqd_bytes); //NOLINT(*-narrowing-conversions)

            s = s.subspan(reqd_bytes);
            if(buffer_.size() < buffer_size_reqd_)
                return;

            // Buffer filled, send the contents to the parser
            span ss(buffer_.data(), buffer_.size());
            const auto [bytes_read, bytes_reqd] = (*parser_)(ss);
            // A parser may not read any bytes even though it was supplied
            // with the bytes it stated it required. For example when
            // reading a CompactSize the parser requires at least one byte, 
            // but if that byte is a multi-byte CompactSize value the 
            // parser will not read but ask for further bytes.
            if(bytes_read)
            {
                // If bytes are read, failure to read the bytes_reqd 
                // represents a programming error in the parser.
                // assert(bytes_read == buffer_size_reqd_);
                if(bytes_read != buffer_size_reqd_)
                    throw ios_base::failure("msg_parser_buffer::op(): parser error");

                buffer_.clear();
                buffer_size_reqd_ = bytes_reqd;
                if(!bytes_reqd)
                {
                    parser_full_ = true;
                    break; // allow second parser::op() call to handle rest of input
                }
            }
            else
            {
                buffer_size_reqd_ = bytes_reqd;
                if(bytes_reqd && s.empty())
                    return;
            }
        }
    }

    const auto [bytes_read, bytes_reqd]{(*parser_)(s)};
    buffer_size_reqd_ = bytes_reqd; 
    if(!bytes_reqd)
        parser_full_ = true;

    buffer_.insert(buffer_.end(),
                   s.begin() + bytes_read, //NOLINT(*-narrowing-conversions)
                   s.end());
}

size_t msg_parser_buffer::read(size_t read_pos, span<uint8_t> s)
{
    return parser_->read(read_pos, s);
}

size_t msg_parser_buffer::size() const
{
    return parser_->size() + buffer_.size();
}

size_t msg_parser_buffer::readable_size() const
{
    return parser_->readable_size();
}

