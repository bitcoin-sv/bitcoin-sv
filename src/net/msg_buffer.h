// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "unique_array.h"
#include "msg_parser_buffer.h"
#include "streams.h"

class CMessageHeader;

// Replaces CDataStream in p2p message processing to enable parsing the 
// byte stream into multiple segments, so that object lifetime/memory 
// allocation can be more finely controlled.
class msg_buffer 
{
    using buffer_type = unique_array;

    buffer_type header_;
    std::string command_;
    std::optional<uint64_t> payload_len_{};
    std::unique_ptr<msg_parser_buffer> payload_;

    buffer_type::size_type read_pos_{};

    int nType;
    int nVersion;

public:
    using size_type = buffer_type::size_type;
    using value_type = buffer_type::value_type;
    using reference  = buffer_type::reference;
    using const_reference = buffer_type::const_reference;
    using iterator = buffer_type::iterator;
    using const_iterator = buffer_type::const_iterator;

    explicit msg_buffer(int nType, int nVersion):
            nType{nType},
            nVersion{nVersion}
    {}

    size_type size() const;
    bool empty() const;

    const value_type* data() const;

    int GetType() const { return nType; }

    void SetVersion(int n) { nVersion = n; }
    int GetVersion() const { return nVersion; }

    void command(const std::string& cmd);
    void payload_len(uint64_t len);
    bool header_complete() const { return payload_len_.has_value(); }

    void read(char* pch, size_t nSize);
    void read(std::span<uint8_t> s);
    void write(const char* p, size_t nSize);
    void write(std::span<const uint8_t>);

    template <typename T>
    msg_buffer& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return (*this);
    }

    template <typename T>
    msg_buffer& operator>>(T& obj)
    {
        ::Unserialize(*this, obj);
        return *this;
    }
};

