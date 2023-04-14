// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

struct bignum_st;

namespace bsv
{
    // Models Regular and StrictTotallyOrdered concepts
    class bint
    {
    public:
        bint();
        explicit bint(int); 
        explicit bint(int64_t); 
        explicit bint(std::size_t); 
        explicit bint(const std::string&);

        // special members
        ~bint() = default;

        bint(const bint&);
        bint& operator=(const bint&);
        bint(bint&&) noexcept = default;
        bint& operator=(bint&&) noexcept = default;

        void swap(bint&) noexcept;

        // Relational operators
        friend bool operator<(const bint&, const bint&);
        friend bool operator==(const bint&, const bint&);

        // Arithmetic operators
        bint& operator+=(const bint&);
        bint& operator-=(const bint&);
        bint& operator*=(const bint&);
        bint& operator/=(const bint&);
        bint& operator%=(const bint&);
        bint operator-() const;
        
        bint& operator+=(int64_t other){ return *this += bint(other); }
        bint& operator-=(int64_t other){ return *this -= bint(other); }
        bint& operator&=(int64_t other){ return *this &= bint(other); }

        // Bit-manipulation operators
        bint& operator>>=(int n);
        bint& operator<<=(int n);
        
        bint& operator&=(const bint&);
        bint& operator|=(const bint&);

        uint8_t lsb() const;
        
        int size_bytes() const;

        friend std::ostream& operator<<(std::ostream&, const bint&);

        friend bool is_negative(const bint&);

        friend long to_long(const bint&);
        friend std::size_t to_size_t_limited(const bint&);

        std::vector<uint8_t> serialize() const;
        static bint deserialize(std::span<const uint8_t>);

    private:
        int spaceship_operator(
            const bint&) const; // auto operator<=>(const bint&) in C++20
        void negate();

        int size_bits() const;
        bool empty() const { return size_bytes() == 0; }

        using buffer_type = std::vector<unsigned char>;
        buffer_type to_bin() const;
        void mask_bits(int n);
        
        struct empty_bn_deleter // See Note 1.
        {
            void operator()(bignum_st* ) const;
        };
        using unique_bn_ptr = std::unique_ptr<bignum_st, empty_bn_deleter>;
        static_assert(sizeof(unique_bn_ptr) == sizeof(bignum_st*));
        unique_bn_ptr value_;
    };
    
    inline void swap(bint& a, bint& b) { a.swap(b);}

    bool operator<(const bint&, const bint&);
    bool operator==(const bint&, const bint&);

    inline bool operator!=(const bint& a, const bint& b) { return !(a == b); }

    inline bool operator<=(const bint& a, const bint& b) { return !(b < a); }
    inline bool operator>(const bint& a, const bint& b) { return b < a; }
    inline bool operator>=(const bint& a, const bint& b) { return !(a < b); }
        
    inline bint operator+(bint a, const bint& b)
    {
        a += b;
        return a;
    }

    inline bint operator-(bint a, const bint& b)
    {
        a -= b;
        return a;
    }

    inline bint operator*(bint a, const bint& b)
    {
        a *= b;
        return a;
    }

    inline bint operator/(bint a, const bint& b)
    {
        a /= b;
        return a;
    }

    inline bint operator%(bint a, const bint& b)
    {
        a %= b;
        return a;
    }

    inline bint operator&(bint a, const bint& b)
    {
        a &= b;
        return a;
    }

    std::ostream& operator<<(std::ostream& os, const bint&);

    // int64_t overloads
    inline bool operator==(const bint& a, const int64_t b) { return a == bint{b}; } 
    inline bool operator==(const int64_t a, const bint& b)  { return bint{a} == b; }
    inline bool operator!=(const bint& a, const int64_t b) { return a != bint(b); }

    inline bool operator<(const bint& a, int64_t b) { return a < bint(b); }
    inline bool operator<(int64_t a, const bint& b) { return bint(a) < b; }
    inline bool operator<=(const bint& a, int64_t b) { return a <= bint(b); }
    inline bool operator>(const bint& a, int64_t b) { return a > bint(b); }
    inline bool operator>=(const bint& a, int64_t b) { return a >= bint(b); }

    inline bint operator+(bint a, const int64_t b) { return a + bint(b); }
    inline bint operator-(bint a, const int64_t b) { return a - bint(b); }
    inline bint operator*(bint a, const int64_t b) { return a * bint(b); }
    inline bint operator/(bint a, const int64_t b) { return a / bint(b); }
    inline bint operator%(bint a, const int64_t b) { return a % bint(b); }
    
    // size_t overloads
    inline bool operator==(const bint& a, const size_t b) { return a == bint{b}; } 
    inline bool operator==(const size_t a, const bint& b)  { return bint{a} == b; }
    inline bool operator!=(const bint& a, const size_t b) { return a != bint(b); }
    
    // int overloads
    inline bool operator==(const bint& a, const int b) { return a == bint{b}; } 
    inline bool operator==(const int a, const bint& b)  { return bint{a} == b; }
    inline bool operator!=(const bint& a, const int b) { return a != bint(b); }

    inline uint8_t operator&(const bint& a, const uint8_t b) { 
        return a.lsb() & b;
    }
    
    bool is_negative(const bint&);
    bint abs(const bint&);
    std::string to_string(const bint&);
    std::size_t to_size_t_limited(const bint&);
    long to_long(const bint&);

    template <typename O>
    inline void serialize(const bint& n, O o)
    {
        const std::vector<uint8_t> v{n.serialize()};
        std::copy(begin(v), end(v), o);
    }

    template <typename I>
    bint deserialize(I first, I last)
    {
        std::vector<uint8_t> v(first, last);
        return bint::deserialize(v);
    }

    class big_int_error : std::runtime_error
    {
    public:
        big_int_error() : std::runtime_error(""){};
    };
}

namespace std
{
    // See Effective C++ Third Edition Item 25 "Consider Support for a non-throwing Swap"
    template <>
    inline void swap<bsv::bint>(bsv::bint& a, bsv::bint& b) noexcept
    {
        a.swap(b);
    }
}

// Notes
// -----
// 1. Used to minimise size of the unique_ptr through empty base class optimization. See Effective Modern C++ Item 18



