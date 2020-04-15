// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include "script.h"
#include "span.h"

#include <iostream>

namespace bsv
{
    class instruction
    {
        opcodetype opcode_;
        size_t offset_{};
        using operand_type = bsv::span<const uint8_t>;
        operand_type operand_{};

    public:
        constexpr instruction(opcodetype opcode) noexcept : opcode_{opcode} {}

        constexpr instruction(opcodetype opcode,
                              size_t offset,
                              const uint8_t* p,
                              size_t n) noexcept
            : opcode_{opcode}, offset_{offset}, operand_{p, n}
        {
        }

        constexpr opcodetype opcode() const noexcept { return opcode_; }
        constexpr size_t offset() const noexcept { return offset_; }
        constexpr const operand_type& operand() const noexcept
        {
            return operand_;
        }

    };

    inline constexpr bool operator==(const instruction& a,
                                     const instruction& b) noexcept
    {
        // clang-format off
    return a.opcode() == b.opcode() && 
           a.operand().data() == b.operand().data() && 
           a.operand().size() == b.operand().size();
        // clang-format on
    }

    inline constexpr bool operator!=(const instruction& a,
                                     const instruction& b) noexcept
    {
        return !(a == b);
    }

    inline std::ostream& operator<<(std::ostream& os, const instruction& inst)
    {
        os << inst.opcode() << ' ' << (void*)inst.operand().data() << ' '
           << inst.operand().size();
        return os;
    }
}

