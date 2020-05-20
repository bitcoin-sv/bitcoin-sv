// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cstddef>

namespace bsv
{
    // Simplified version of std::span (C++20)
    // Replace with std::span when available
    template <typename ElementType>
    class span
    {
        using element_type = ElementType;
        using pointer = ElementType*;
        using reference = ElementType&;
        using size_type = size_t;
        using iterator = pointer;
        pointer p_{};
        size_type n_{};

    public:
        constexpr span() = default;
        constexpr span(pointer p, size_type n) noexcept : p_(p), n_(n) {}

        template <typename Container>
        constexpr span(const Container& c) noexcept : p_{c.data()}, n_{c.size()}
        {
        }

        constexpr size_type size() const noexcept { return n_; }
        constexpr bool empty() const noexcept { return n_ == 0; }

        constexpr iterator begin() const noexcept { return p_; }
        constexpr iterator end() const noexcept { return p_ + n_; }

        constexpr pointer data() const noexcept { return p_; }
        constexpr reference front() const { return p_[0]; }
        constexpr reference back() const { return p_[size() - 1]; }
        constexpr reference operator[](size_t n) const { return p_[n]; }

        constexpr span last(size_t count) const
        {
            assert(count <= size());
            return span{p_ + size() - count, count};
        };
    };
}

