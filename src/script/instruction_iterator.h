// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <tuple>

#include "instruction.h"

#include <span>
#include <string_view>

using namespace std;

namespace bsv
{
    // Returns: status, opcode, offset to operand (see OP_PUSHDATA*), length of operand
    inline constexpr std::tuple<bool, opcodetype, int8_t, size_t>
    decode_instruction(span<const uint8_t> s)
    {
        if(s.empty())
            return {false, OP_INVALIDOPCODE, 0, 0};

        const opcodetype opcode{static_cast<opcodetype>(s[0])};
        if(opcode > OP_PUSHDATA4 || opcode == 0)
            return std::make_tuple(true, opcode, 0, 0);

        s = {s.last(s.size() - 1)};

        if(s.empty())
            return {false, OP_INVALIDOPCODE, 0, 0};

        // for opcodes 0x0 to 0x75 the opcode is the length of
        // data to be pushed onto the stack
        if(opcode < OP_PUSHDATA1)
        {
            if(opcode <= s.size())
                return {true, opcode, 0, opcode};
            else
                return {false, OP_INVALIDOPCODE, 0, 0};
        }

        if(opcode == OP_PUSHDATA1)
        {
            if(s[0] > s.size() - 1)
                return {false, OP_INVALIDOPCODE, 0, 0};

            return {true, opcode, 1, s[0]};
        }

        if(opcode == OP_PUSHDATA2)
        {
            if(s.size() < 2)
                return {false, OP_INVALIDOPCODE, 0, 0};

            const auto operand_len{ReadLE16(s.data())};
            if(operand_len > s.size() - 2)
                return {false, OP_INVALIDOPCODE, 0, 0};

            return {true, opcode, 2, operand_len};

        }

        if(opcode == OP_PUSHDATA4)
        {
            if(s.size() < 4)
                return {false, OP_INVALIDOPCODE, 0, 0};

            const auto operand_len{ReadLE32(s.data())};
            if(operand_len > s.size() - 4)
                return {false, OP_INVALIDOPCODE, 0, 0};

            return {true, opcode, 4, operand_len};
        }
        return {true, opcode, 0, 0};
    }

    class instruction_iterator
    {
        span<const uint8_t> span_;
        bool valid_{};
        instruction instruction_;

        constexpr std::tuple<bool, instruction> next(span<const uint8_t> s)
        {
            const auto [status, opcode, offset, len]{decode_instruction(s)};
            return {status, instruction{opcode, offset, s.data() + 1 + offset, len}};
        }

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = instruction;
        using difference_type = std::ptrdiff_t;
        using pointer = instruction*;
        using reference = instruction&;

        constexpr instruction_iterator(span<const uint8_t> s) noexcept : 
            span_{s} 
        {
            const auto& [valid, instruction] = next(s);
            valid_ = valid;
            instruction_ = instruction;
        }

        constexpr explicit operator bool() const { return valid_; }

        const uint8_t* data() const { return span_.data(); }

        constexpr instruction_iterator& operator++() noexcept
        {
            if(!valid_)
            {
                // advance to end of script range
                span_ = span_.last(0);
                instruction_ = instruction{OP_INVALIDOPCODE, 0, span_.data(), 0};
                return *this;
            }

            const auto delta{1 + instruction_.offset() +
                             instruction_.operand().size()};
            span_ = span_.last(span_.size() - delta);
            const auto& [valid, instruction] = next(span_);
            valid_ = valid;
            instruction_ = instruction;
            return *this;
        }

        constexpr instruction_iterator operator++(int) noexcept
        {
            instruction_iterator tmp(*this);
            operator++();
            return tmp;
        }

        constexpr const instruction& operator*() const noexcept
        {
            return instruction_;
        }

        constexpr const instruction* operator->() const noexcept
        {
            return &instruction_;
        }

        constexpr bool operator==(const instruction_iterator& other) const
            noexcept
        {
            return span_.data() == other.span_.data() &&
                   span_.size() == other.span_.size();
        }

        constexpr bool operator!=(const instruction_iterator& other) const
            noexcept
        {
            return !(operator==(other));
        }
    };

    inline std::string_view to_sv(std::span<const uint8_t> s)
    {
        return std::string_view{reinterpret_cast<const char*>(s.data()),
                                s.size()};
    }
}
