// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "tx_parser.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <numeric>

#include "cmpct_size.h"
#include "p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

// requires s.data() is either:
// the start of a script input or 
// the start of the script length
// return (bytes_read, bytes_required)
std::pair<size_t, size_t> tx_parser::parse_input(span<const uint8_t> s)
{
    size_t total_bytes_read{};

    if(!script_len_)
    {
        if(s.size() < outpoint_len + 1)
            return make_pair(0, outpoint_len + 1);

        total_bytes_read += outpoint_len;
        const auto [bytes_read, val] = parse_compact_size(s.subspan(outpoint_len));
        if(!bytes_read)
            return make_pair(0, outpoint_len + val);

        script_len_ = val; 
        total_bytes_read += bytes_read;
        vector<uint8_t> v;
        v.reserve(total_bytes_read);
        v.insert(v.cend(), s.begin(), s.begin() + total_bytes_read);
        ip_buffers_.push_back(std::move(v));
        s = s.subspan(total_bytes_read);
    }

    const size_t extra_bytes_reqd{min(script_len_.value(),
                                      numeric_limits<size_t>::max() - seq_len) + seq_len};
    if(s.size() < extra_bytes_reqd)
        return make_pair(total_bytes_read, extra_bytes_reqd);

    script_len_.reset();

    auto& cur_ip_buffer{ip_buffers_[ip_buffers_.size() - 1]};
    cur_ip_buffer.insert(cur_ip_buffer.cend(), s.begin(),
                         s.begin() + extra_bytes_reqd);

    return make_pair(total_bytes_read + extra_bytes_reqd, 0);
}

// requires s.data() is either:
// the start of a script output or
// the start of the script length
// return (bytes_read, bytes_required)
std::pair<size_t, size_t> tx_parser::parse_output(span<const uint8_t> s)
{
    size_t total_bytes_read{};

    if(!script_len_)
    {
        if(s.size() < value_len + 1)
            return make_pair(0, value_len + 1);

        total_bytes_read += value_len;
        const auto [bytes_read, val] = parse_compact_size(s.subspan(value_len));
        if(!bytes_read)
            return make_pair(0, value_len + val);
        
        script_len_ = val;
        total_bytes_read += bytes_read;
        vector<uint8_t> v;
        v.reserve(total_bytes_read);
        v.insert(v.cend(), s.begin(), s.begin() + total_bytes_read);
        op_buffers_.push_back(std::move(v));
        s = s.subspan(total_bytes_read);
    }

    const size_t extra_bytes_reqd{script_len_.value()};
    if(s.size() < extra_bytes_reqd)
        return make_pair(total_bytes_read, extra_bytes_reqd);

    script_len_.reset();

    auto& cur_op_buffer{op_buffers_[op_buffers_.size() - 1]};
    cur_op_buffer.insert(cur_op_buffer.cend(), 
                         s.begin(),
                         s.begin() + extra_bytes_reqd);

    return make_pair(total_bytes_read + extra_bytes_reqd, 0);
}

std::pair<size_t, size_t> tx_parser::parse_version(const span<const uint8_t> s)
{
    assert(state_ == state::version);

    if(s.size() < version_len)
        return make_pair(0, version_len);

    version_buffer_.resize(version_len);
    copy(s.begin(), s.begin() + version_len, version_buffer_.begin());
    return make_pair(version_len, 0); 
}

std::pair<size_t, size_t> tx_parser::parse_ip_count(span<const uint8_t> s)
{
    assert(state_ == state::ip_count);

    const auto [bytes_read, val] = parse_compact_size(s);
    if(!bytes_read)
        return make_pair(bytes_read, val);

    n_ips_ = val;
    ip_count_buffer_.resize(bytes_read);
    copy(s.begin(), s.begin() + bytes_read, ip_count_buffer_.begin());
    return make_pair(bytes_read, 0);
}

// requires s.data() either:
// start of tx input or
// start of tx input's script length field 
std::pair<size_t, size_t> tx_parser::parse_inputs(span<const uint8_t> s)
{
    assert(state_ == state::ips_);

    size_t total_bytes_read{};
    while(current_ip_ < n_ips_)
    {
        const auto [bytes_read, bytes_reqd] = parse_input(s);
        if(bytes_read)
        {
            total_bytes_read += bytes_read;
            s = s.subspan(bytes_read);
        }

        if(bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);
                
        ++current_ip_;

    }
    return make_pair(total_bytes_read, 0);
}

std::pair<size_t, size_t> tx_parser::parse_op_count(span<const uint8_t> s)
{
    assert(state_ == state::op_count);

    const auto [bytes_read, val] = parse_compact_size(s);
    if(!bytes_read)
        return make_pair(bytes_read, val);

    n_ops_ = val;
    op_count_buffer_.resize(bytes_read);
    copy(s.begin(), s.begin() + bytes_read, op_count_buffer_.begin());
    return make_pair(bytes_read, 0);
}

std::pair<size_t, size_t> tx_parser::parse_outputs(span<const uint8_t> s)
{
    assert(state_ == state::ops_);

    size_t total_bytes_read{};
    while(current_op_ < n_ops_)
    {
        const auto [bytes_read, bytes_reqd] = parse_output(s);
        if(bytes_read)
        {
            total_bytes_read += bytes_read;
            s = s.subspan(bytes_read);
        }

        if(bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);
                
        ++current_op_;
    }
    return make_pair(total_bytes_read, 0);
}
    
std::pair<size_t, size_t> tx_parser::parse_locktime(const span<const uint8_t> s)
{
    assert(state_ == state::lock_time);

    if(s.size() < locktime_len)
        return make_pair(0, locktime_len);

    locktime_buffer_.resize(locktime_len);
    copy(s.begin(), s.begin() + locktime_len, locktime_buffer_.begin());
    return make_pair(locktime_len, 0);
}
    
// return bytes_read, bytes_required
std::pair<size_t, size_t> tx_parser::operator()(span<const uint8_t> s)
{
    size_t total_bytes_read{};

    switch(state_)
    {
    case state::version:
    {
        const auto [bytes_read, bytes_reqd] = parse_version(s);
        total_bytes_read += bytes_read;
        s = s.subspan(bytes_read);
        if(s.size() < bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);
        
        [[fallthrough]];
    }
    case state::ip_count:
    {
        state_ = state::ip_count;
        const auto [bytes_read, bytes_reqd] = parse_ip_count(s);
        total_bytes_read += bytes_read;
        s = s.subspan(bytes_read);
        if(s.size() < bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);
        [[fallthrough]];
    }
    case state::ips_:
    {
        state_ = state::ips_;
        const auto [bytes_read, bytes_reqd] = parse_inputs(s);
        total_bytes_read += bytes_read;
        s = s.subspan(bytes_read);
        if(s.size() < bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);
        [[fallthrough]];
    }
    case state::op_count:
    {
        state_ = state::op_count;
        const auto [bytes_read, bytes_reqd] = parse_op_count(s);
        total_bytes_read += bytes_read;
        s = s.subspan(bytes_read);
        if(s.size() < bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);
        [[fallthrough]];
    }
    case state::ops_:
    {
        state_ = state::ops_;
        const auto [bytes_read, bytes_reqd] = parse_outputs(s);
        total_bytes_read += bytes_read;
        s = s.subspan(bytes_read);
        if(s.size() < bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);
        [[fallthrough]];
    }
    case state::lock_time:
    {
        state_ = state::lock_time;
        const auto [bytes_read, bytes_reqd] = parse_locktime(s);
        total_bytes_read += bytes_read;
        s = s.subspan(bytes_read);
        if(s.size() < bytes_reqd)
            return make_pair(total_bytes_read, bytes_reqd);

        const size_t size{buffer_size()};
        buffer_.reserve(size);
        buffer_.insert(buffer_.cend(), 
                       version_buffer_.cbegin(),
                       version_buffer_.cend());
        buffer_.insert(buffer_.cend(), 
                       ip_count_buffer_.cbegin(),
                       ip_count_buffer_.cend());
        for(size_t i{}; i < ip_buffers_.size(); ++i)
        {
            buffer_.insert(buffer_.cend(), 
                           ip_buffers_[i].cbegin(),
                           ip_buffers_[i].cend());
        }
        buffer_.insert(buffer_.cend(), 
                       op_count_buffer_.cbegin(),
                       op_count_buffer_.cend());
        for(size_t i{}; i < op_buffers_.size(); ++i)
        {
            buffer_.insert(buffer_.cend(),
                           op_buffers_[i].cbegin(),
                           op_buffers_[i].cend());
        }
        buffer_.insert(buffer_.cend(), 
                       locktime_buffer_.cbegin(),
                       locktime_buffer_.cend());
        size_ += size;
        [[fallthrough]];
    }
    case state::complete:
    {
        state_ = state::complete;
        current_ip_ = 0;
        current_op_ = 0;

        version_buffer_.clear();
        ip_count_buffer_.clear();
        ip_buffers_.clear();
        op_count_buffer_.clear();
        op_buffers_.clear();
        locktime_buffer_.clear();

        break;
    }
    default:
        assert(false);
    }
           
    return make_pair(total_bytes_read, 0);
}

unique_array tx_parser::buffer() &&
{
    assert(state_ == state::complete); 
    size_ = 0;
    state_ = state::version;
    return std::move(buffer_);
}

size_t tx_parser::buffer_size() const
{
    size_t size{version_buffer_.size() + 
                ip_count_buffer_.size() + 
                op_count_buffer_.size() + 
                locktime_buffer_.size()};

    size += accumulate(ip_buffers_.cbegin(), ip_buffers_.cend(),
                       0,
                       [](auto size, const auto& buffer)
                       {
                           return size + buffer.size();
                       });

    size += accumulate(op_buffers_.cbegin(), op_buffers_.cend(),
                       0,
                       [](auto size, const auto& buffer)
                       {
                           return size + buffer.size();
                       });
    return size;
}

size_t tx_parser::size() const
{
    return size_ + buffer_size(); 
}

std::ostream& operator<<(std::ostream& os, const tx_parser::state& state)
{
    switch(state)
    {
        case tx_parser::state::version:
            os << "version";
            break;
        case tx_parser::state::ip_count:
            os << "ip_count";
            break;
        case tx_parser::state::ips_:
            os << "ips";
            break;
        case tx_parser::state::op_count:
            os << "op_count";
            break;
        case tx_parser::state::ops_:
            os << "ops";
            break;
        case tx_parser::state::lock_time:
            os << "lock_time";
            break;
        case tx_parser::state::complete:
            os << "complete";
            break;
    }
    return os;
}

