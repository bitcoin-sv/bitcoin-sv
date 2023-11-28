// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>

// vector-like wrapper around a unique_ptr to an array of uint8_t's
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class unique_array
{
public:
    using size_type = size_t;
    using value_type = uint8_t;
    
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = value_type*;
    using const_iterator = const value_type*;

    unique_array() = default;
    explicit unique_array(size_t);
    explicit unique_array(std::span<const uint8_t>);
    ~unique_array() = default;

    unique_array(unique_array&&) noexcept;
    unique_array& operator=(unique_array&&) noexcept;

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t capacity() const { return cap_; }
    void reserve(size_t);
    void shrink_to_fit();

    const value_type* data() const { return p_.get(); }
    
    iterator begin() { return &p_[0]; }
    iterator end() { return &p_[size_]; }
    
    const_iterator begin() const { return &p_[0]; }
    const_iterator end() const { return &p_[size_]; }

    const_iterator cbegin() const { return &p_[0]; }
    const_iterator cend() const { return &p_[size_]; }
    
    const value_type& operator[](size_t i) const { return p_[i]; }

    void push_back(const value_type&);

    void append(const std::span<const uint8_t>);
    void append(const value_type*, size_t);

    template<typename InputIt>
    void insert(const_iterator pos, InputIt first, InputIt last)
    {
        const auto offset{std::distance(cbegin(), pos)};

        const auto n{std::distance(first, last)};
        if(size_ + n > cap_)
            reallocate(n);

        std::copy(first, last, &p_[offset]);
        size_ += n;
    }

    void clear() { size_ = 0; } 

    void reset();

private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    std::unique_ptr<value_type[]> p_{std::make_unique<value_type[]>(0)};

    size_t cap_{0};
    size_t size_{0};

    void reallocate(size_t);
};

size_t read(const unique_array&, size_t read_pos, std::span<uint8_t>);

