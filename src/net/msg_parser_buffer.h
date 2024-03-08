// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "msg_parser.h"
#include "unique_array.h"

// Buffers any bytes that can't be read by the parser until the required bytes
// have been received
class msg_parser_buffer
{
    std::unique_ptr<msg_parser> parser_;
    unique_array buffer_;
    size_t bytes_read_{};
    size_t buffer_size_reqd_{};
    bool overflow_{};

public:
    explicit msg_parser_buffer(std::unique_ptr<msg_parser> parser):parser_{std::move(parser)}
    {}

    void operator()(std::span<const uint8_t> s);
    size_t read(size_t read_pos, std::span<uint8_t>);
    size_t size() const;
    size_t parsed_size() const;
    void clear();

    size_t buffer_size() const { return buffer_.size(); }
    size_t buffer_size_reqd() const { return buffer_size_reqd_; }
};

 
