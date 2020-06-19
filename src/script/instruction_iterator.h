// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include "instruction.h"

namespace bsv
{
    // Returns: opcode, offset to operand (see OP_PUSHDATA*), length of operand
    inline constexpr std::tuple<opcodetype, int8_t, size_t>
    decode_instruction(span<const uint8_t> s)
    {
        if(s.empty())
            return {OP_INVALIDOPCODE, 0, 0};

        const opcodetype opcode{static_cast<opcodetype>(s[0])};
        if(opcode > OP_PUSHDATA4 || opcode == 0)
            return std::make_tuple(opcode, 0, 0);

        s = {s.last(s.size() - 1)};

        if(s.empty())
            return {OP_INVALIDOPCODE, 0, 0};

        // for opcodes 0x0 to 0x75 the opcode is the length of
        // data to be pushed onto the stack
        if(opcode < OP_PUSHDATA1)
        {
            if(opcode <= s.size())
                return {opcode, 0, opcode};
            else
                return {OP_INVALIDOPCODE, 0, 0};
        }

        if(opcode == OP_PUSHDATA1)
        {
            if(s[0] > s.size() - 1)
                return {OP_INVALIDOPCODE, 0, 0};

            return {opcode, 1, s[0]};
        }

        if(opcode == OP_PUSHDATA2)
        {
            if(s.size() < 2)
                return {OP_INVALIDOPCODE, 0, 0};

            const auto operand_len{ReadLE16(s.data())};
            if(operand_len > s.size() - 2)
                return {OP_INVALIDOPCODE, 0, 0};

            return {opcode, 2, operand_len};
        }

        if(opcode == OP_PUSHDATA4)
        {
            if(s.size() < 4)
                return {OP_INVALIDOPCODE, 0, 0};

            const auto operand_len{ReadLE32(s.data())};
            if(operand_len > s.size() - 4)
                return {OP_INVALIDOPCODE, 0, 0};

            return {opcode, 4, operand_len};
        }
        return {opcode, 0, 0};
    }

    class instruction_iterator
        : public std::iterator<std::forward_iterator_tag, instruction>

    {
        instruction instruction_;
        span<const uint8_t> span_;

    public:
        constexpr instruction_iterator(span<const uint8_t> s) noexcept
            : instruction_{OP_INVALIDOPCODE}, span_{s} {
            const auto [opcode, offset, len]{decode_instruction(s)};
            if(opcode != OP_INVALIDOPCODE)
            {
                instruction_ =
                    instruction{opcode, offset, s.data() + 1 + offset, len};
            }
            else
            {
                // advance to end of range
                span_ = span_.last(0);
                instruction_ = instruction{opcode, offset, span_.data(), len};
            }
        }

        constexpr instruction_iterator& operator++() noexcept
        {
            if(instruction_.opcode() == OP_INVALIDOPCODE)
                return *this;

            const auto delta{1 + instruction_.offset() +
                             instruction_.operand().size()};

            span_ = span_.last(span_.size() - delta);

            auto [opcode, offset, len]{decode_instruction(span_)};
            if(opcode != OP_INVALIDOPCODE)
            {
                instruction_ =
                    instruction{opcode, offset, span_.data() + 1 + offset, len};
            }
            else
            {
                // advance to end of range
                span_ = span_.last(0);
                instruction_ = instruction{opcode, offset, span_.data(), len};
            }
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
}

