// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "msg_buffer.h"

#include "cmpctblock_parser.h"
#include "single_seg_parser.h"
#include "msg_parser_buffer.h"
#include "net/block_parser.h"
#include "net/blocktxn_parser.h"
#include "net/net_message.h"
#include "p2p_msg_lengths.h"
#include "protocol.h"
#include <cstdint>
#include <ios>
#include <memory>

using namespace std;
using namespace bsv;

size_t msg_buffer::size() const
{
    auto size{header_.size()};
    size += payload_ ? payload_->size() : 0;
    return size - read_pos_; 
}

bool msg_buffer::empty() const
{ 
    return size() == 0; 
}

const uint8_t* msg_buffer::data() const
{ 
    return header_.data() + read_pos_; 
}

void msg_buffer::command(const string& cmd)
{ 
    command_ = cmd;
}

void msg_buffer::payload_len(uint64_t len)
{ 
    payload_len_ = len;
}

static std::unique_ptr<msg_parser> make_parser(const string& cmd)
{
    // Note: It's not a protocol error to call make_parser with an
    // empty cmd string, that's just another example of an unknown
    // command which is detected in later processing.

    if(cmd == "block")
        return make_unique<msg_parser>(block_parser{});
    else if(cmd == "blocktxn")
        return make_unique<msg_parser>(blocktxn_parser{});
    else if(cmd == "cmpctblock")
        return make_unique<msg_parser>(cmpctblock_parser{});
    else
        return make_unique<msg_parser>(single_seg_parser{});
}

void msg_buffer::write(span<const uint8_t> s)
{
    if(!header_complete())
    {
        header_.insert(header_.cend(), s.begin(), s.end());
    }
    else
    {
        if(!payload_)
            payload_ = make_unique<msg_parser_buffer>(make_parser(command_));

        (*payload_)(s);
    }
}

void msg_buffer::read(span<uint8_t> s)
{
    if(s.empty())
        return;

    const size_type end_pos = read_pos_ + s.size();
    if(!header_complete())
    {
        if(end_pos > header_.size())
            throw std::ios_base::failure( "msg_buffer::read(): end of data");

        copy(&header_[read_pos_], &header_[read_pos_] + s.size(), s.begin());
        read_pos_ = end_pos;
    }
    else
    {
        const auto payload_len{payload_ ? payload_->parsed_size() : 0};
        if(end_pos > header_.size() + payload_len)
            throw std::ios_base::failure( "msg_buffer::read(): end of data");
    
        if(payload_)
        {
            payload_->read(read_pos_ - header_.size(), s);
            read_pos_ = end_pos;
        }
    }
}

void msg_buffer::read(char* p, size_t n)
{
    read(span{reinterpret_cast<uint8_t*>(p), n});
}

void msg_buffer::write(const char* p, size_t n)
{ 
    write(span{reinterpret_cast<const uint8_t*>(p), n});
}

