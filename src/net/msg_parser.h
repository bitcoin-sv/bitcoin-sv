// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

// Type erased class that is constructed/implemented with a parser object
// appropriate to the message defined in the p2p message header command field. 
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class msg_parser 
{
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    struct msg_parser_concept
    {
        // return bytes_read, bytes_required
        // bytes_read - number of bytes read (will read any bytes that it can)
        // bytes_reqd - number of further bytes required (as many as it knows accurately)
        // (0, 0) indicates that the parser cannot accept any further input
        virtual std::pair<size_t, size_t> operator()(std::span<const uint8_t>) = 0;
        virtual size_t read(size_t read_pos, std::span<uint8_t>) = 0;
        virtual size_t size() const = 0;

        virtual void clear() = 0;
        
        virtual ~msg_parser_concept() = default;
    };

    template<typename T>
    struct msg_parser_model : msg_parser_concept
    {
        T object_;

        template<typename U,
                 typename = std::enable_if_t<
                     !std::is_base_of_v<msg_parser_model, std::decay_t<U>>
                                            >
                >
        msg_parser_model(U&& u):object_{std::forward<U>(u)}
        {}

        std::pair<size_t, size_t> operator()(std::span<const uint8_t> s) override
        {
            return object_(s); 
        }

        size_t read(size_t read_pos, std::span<uint8_t> s) override
        {
            return object_.read(read_pos, s);
        }
        
        size_t size() const override
        {
            return object_.size();
        }

        void clear() override
        {
            object_.clear();
        }
    };

    std::unique_ptr<msg_parser_concept> pimpl_;

public:
    template<typename T,
             typename = std::enable_if_t<
                !std::is_base_of_v<msg_parser, std::decay_t<T>>
                                        >
            >
    explicit msg_parser(T&& t):
        pimpl_{ std::make_unique<msg_parser_model<T>>(std::forward<T>(t))}
    {}

    msg_parser(msg_parser&&) = default;
    msg_parser& operator=(msg_parser&&) = default;

    std::pair<size_t, size_t> operator()(std::span<const uint8_t> s)
    {
        return (*pimpl_)(s);
    }

    size_t read(size_t read_pos, std::span<uint8_t> s) { return pimpl_->read(read_pos, s); }
    size_t size() const { return pimpl_->size(); }

    void clear() { return pimpl_->clear(); }

};

