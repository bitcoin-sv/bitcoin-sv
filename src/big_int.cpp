// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "big_int.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <sstream>
#include <openssl/asn1.h>
#include <openssl/bn.h>

using namespace std;

void bsv::bint::empty_bn_deleter::operator()(bignum_st* p) const
{
    ::BN_free(p);
}

bsv::bint::bint() : value_{nullptr} {}

bsv::bint::bint(const int i) : value_(BN_new(), empty_bn_deleter())
{
    // assert(value_);
    if(!value_)
        throw big_int_error();

    if(i >= 0)
    {
        const auto s{BN_set_word(value_.get(), i)};
        // assert(s);
        if(!s)
            throw big_int_error();
    }
    else if(i > INT_MIN)
    {
        const auto s{BN_set_word(value_.get(), -i)};
        // assert(s);
        if(!s)
            throw big_int_error();
        BN_set_negative(value_.get(), 1);
    }
    else
    {
        const int ii{i + 1}; // add 1 to avoid overflow in negation
        auto s{BN_set_word(value_.get(), -ii)};
        // assert(s);
        if(!s)
            throw big_int_error();

        BN_set_negative(value_.get(), 1);

        // subtract 1 to compensate for earlier addition
        s = BN_sub(value_.get(), value_.get(), BN_value_one());
        // assert(s);
        if(!s)
            throw big_int_error();
    }

    // clang-format off
    //assert( ((i < 0) && (is_negative(*this))) ||
    //        ((i == 0) && (BN_is_zero(value_.get()))) ||
    //        ((i > 0) && (!is_negative(*this))) );
    // clang-format on
}

bsv::bint::bint(const int64_t i) : value_(BN_new(), empty_bn_deleter())
{
    // assert(value_);
    if(!value_)
        throw big_int_error();

    if(i >= 0)
    {
        const auto s{BN_set_word(value_.get(), i)};
        // assert(s);
        if(!s)
            throw big_int_error();
    }
    else if(i > INT64_MIN)
    {
        const auto s{BN_set_word(value_.get(), -i)};
        // assert(s);
        if(!s)
            throw big_int_error();
        BN_set_negative(value_.get(), 1);
    }
    else
    {
        const int64_t ii{i + 1}; // add 1 to avoid overflow in negation
        auto s{BN_set_word(value_.get(), -ii)};
        // assert(s);
        if(!s)
            throw big_int_error();

        BN_set_negative(value_.get(), 1);

        // subtract 1 to compensate for earlier addition
        s = BN_sub(value_.get(), value_.get(), BN_value_one());
        // assert(s);
        if(!s)
            throw big_int_error();
    }

    // clang-format off
    //assert( ((i < 0) && (is_negative(*this))) ||
    //      ((i == 0) && (BN_is_zero(value_.get()))) ||
    //      ((i > 0) && (!is_negative(*this))) );
    // clang-format on
}

bsv::bint::bint(const size_t i) : value_(BN_new(), empty_bn_deleter())
{
    assert(value_);
    if(!value_)
        throw big_int_error();

    // Precondition: i > std::numeric_limits<size_t>::min()
    // as negation is out-of-range of size_t
    // assert(i >= std::numeric_limits<size_t>::min());
    // assert(value_);
    const auto s{BN_set_word(value_.get(), i)};
    // assert(s);
    if(!s)
        throw big_int_error();

    // clang-format off
    //assert( ((i == 0) && (BN_is_zero(value_.get())))  ||
    //      ((i > 0) && (!is_negative(*this))) );
    // clang-format on
}

bsv::bint::bint(const std::string& n) : value_(BN_new(), empty_bn_deleter())
{
    // assert(value_);
    if(!value_)
        throw big_int_error();

    auto p{value_.get()};
    const auto s = BN_dec2bn(&p, n.c_str());
    // assert(s);
    if(!s)
        throw big_int_error();
}

bsv::bint::bint(const bint& other) : value_(BN_new(), empty_bn_deleter())
{
    // assert(other.value_); // See Note 2 @ eof
    // assert(value_);
    if(!value_)
        throw big_int_error();

    const auto s = BN_copy(value_.get(), other.value_.get());
    // assert(s);
    if(!s)
        throw big_int_error();
}

bsv::bint& bsv::bint::operator=(const bint& other)
{
    bint temp{other};
    swap(temp);
    return *this;
}

void bsv::bint::swap(bint& other) noexcept
{
    // assert(value_);
    using std::swap;
    swap(value_, other.value_);
}

// Relational operators
bool bsv::operator<(const bint& a, const bint& b)
{
    return a.spaceship_operator(b) < 0;
}
bool bsv::operator==(const bint& a, const bint& b)
{
    return a.spaceship_operator(b) == 0;
}

// Arithmetic operators
bsv::bint& bsv::bint::operator+=(const bint& other)
{
    // assert(value_);
    const auto s = BN_add(value_.get(), value_.get(), other.value_.get());
    // assert(s);
    if(!s)
        throw big_int_error();
    return *this;
}

bsv::bint& bsv::bint::operator-=(const bint& other)
{
    // assert(value_);
    const auto s = BN_sub(value_.get(), value_.get(), other.value_.get());
    // assert(s);
    if(!s)
        throw big_int_error();
    return *this;
}

namespace
{
    struct empty_ctx_deleter // See note 1
    {
        void operator()(BN_CTX* p) const { ::BN_CTX_free(p); }
    };
    using unique_ctx_ptr = std::unique_ptr<BN_CTX, empty_ctx_deleter>;
    static_assert(sizeof(unique_ctx_ptr) == sizeof(BN_CTX*));

    unique_ctx_ptr make_unique_ctx_ptr()
    {
        return unique_ctx_ptr{BN_CTX_new(), empty_ctx_deleter()};
    }
}

bsv::bint& bsv::bint::operator*=(const bint& other)
{
    // assert(value_);
    unique_ctx_ptr ctx{make_unique_ctx_ptr()};
    const auto s{
        BN_mul(value_.get(), value_.get(), other.value_.get(), ctx.get())};
    // assert(s);
    if(!s)
        throw big_int_error();
    return *this;
}

bsv::bint& bsv::bint::operator/=(const bint& other)
{
    // assert(value_);
    bint rem;
    unique_ctx_ptr ctx{make_unique_ctx_ptr()};
    const auto s{BN_div(value_.get(), rem.value_.get(), value_.get(),
                        other.value_.get(), ctx.get())};
    // assert(s);
    if(!s)
        throw big_int_error();
    return *this;
}

bsv::bint& bsv::bint::operator%=(const bint& other)
{
    // assert(value_);
    bint rem;
    unique_ctx_ptr ctx{make_unique_ctx_ptr()};
    const auto s{
        BN_mod(value_.get(), value_.get(), other.value_.get(), ctx.get())};
    // assert(s);
    if(!s)
        throw big_int_error();
    return *this;
}

// Bitwise operators
bsv::bint& bsv::bint::operator&=(const bint& other)
{
    if(this == &other)
        return *this;

    if(other.empty())
    {
        *this = bint{0};
        return *this;
    }

    bool negate{};
    if((is_negative(*this)) && is_negative(other))
        negate = true;

    auto bytes_other{other.to_bin()};
    auto bytes_this{to_bin()};

    if(bytes_other.size() <= bytes_this.size())
    {
        transform(rbegin(bytes_other), rend(bytes_other), rbegin(bytes_this),
                  rbegin(bytes_other), [](auto byte_other, auto byte_this) {
                      return byte_other & byte_this;
                  });
        BN_bin2bn(bytes_other.data(), bytes_other.size(), value_.get());
    }
    else
    {
        transform(rbegin(bytes_this), rend(bytes_this), rbegin(bytes_other),
                  rbegin(bytes_this), [](auto byte_this, auto byte_other) {
                      return byte_this & byte_other;
                  });
        BN_bin2bn(bytes_this.data(), bytes_this.size(), value_.get());
    }

    if(negate)
        this->negate();

    return *this;
}

bsv::bint& bsv::bint::operator|=(const bint& other)
{
    if(this == &other)
        return *this;

    if(other.empty())
        return *this;

    bool negate{};
    if((is_negative(other) && !is_negative(*this)) ||
       (is_negative(*this) && !is_negative(other)))
        negate = true;

    auto bytes_other{other.to_bin()};
    auto bytes_this{to_bin()};

    if(bytes_other.size() <= bytes_this.size())
    {
        transform(rbegin(bytes_other), rend(bytes_other), rbegin(bytes_this),
                  rbegin(bytes_this), [](auto byte_other, auto byte_this) {
                      return byte_other | byte_this;
                  });
        BN_bin2bn(bytes_this.data(), bytes_this.size(), value_.get());
    }
    else
    {
        transform(rbegin(bytes_this), rend(bytes_this), rbegin(bytes_other),
                  rbegin(bytes_other), [](auto byte_this, auto byte_other) {
                      return byte_this | byte_other;
                  });
        BN_bin2bn(bytes_other.data(), bytes_other.size(), value_.get());
    }

    if(negate)
        this->negate();

    return *this;
}

bsv::bint& bsv::bint::operator<<=(const int n)
{
    if(n <= 0)
        return *this;

    const auto s{BN_lshift(value_.get(), value_.get(), n)};
    // assert(s);
    if(!s)
        throw big_int_error();
    return *this;
}

bsv::bint& bsv::bint::operator>>=(const int n)
{
    if(n <= 0)
        return *this;

    const auto s{BN_rshift(value_.get(), value_.get(), n)};
    // assert(s);
    if(!s)
        throw big_int_error();
    return *this;
}

bsv::bint bsv::bint::operator-() const
{
    bint rv{*this};
    rv.negate();
    return rv;
}

uint8_t bsv::bint::lsb() const
{
    const auto buffer{to_bin()};
    if(buffer.empty())
        return 0;

    return buffer[buffer.size() - 1];
}

int bsv::bint::spaceship_operator(
    const bint& other) const // auto operator<=>(const bint&) in C++20
{
    // assert(value_);
    return BN_cmp(value_.get(), other.value_.get());
}

void bsv::bint::negate()
{
    const bool neg = is_negative(*this);
    if(neg)
        BN_set_negative(value_.get(), 0); // set +ve
    else
        BN_set_negative(value_.get(), 1); // set -ve
}

void bsv::bint::mask_bits(const int n)
{
    const auto s{BN_mask_bits(value_.get(), n)};
    // assert(s);
    if(!s)
        throw big_int_error();
}

int bsv::bint::size_bits() const { return BN_num_bits(value_.get()); }

int bsv::bint::size_bytes() const { return BN_num_bytes(value_.get()); }

bsv::bint::buffer_type bsv::bint::to_bin() const
{
    // assert(value_);

    buffer_type buffer(size_bytes());
    BN_bn2bin(value_.get(), buffer.data());
    // const auto n{BN_bn2bin(value_.get(), buffer.data())};
    // assert(buffer.size() == static_cast<buffer_type::size_type>(n));

    return buffer;
}

namespace
{
    struct empty_str_deleter // See note 1
    {
        void operator()(const char* p) const { ::OPENSSL_free((void*)p); }
    };
    using unique_str_ptr = std::unique_ptr<const char[], empty_str_deleter>;
    static_assert(sizeof(unique_str_ptr) == sizeof(const char*));

    unique_str_ptr to_str(bignum_st* bn)
    {
        return unique_str_ptr{BN_bn2dec(bn)};
    }
}

std::ostream& bsv::operator<<(std::ostream& os, const bint& n)
{
    if(n.value_ == nullptr)
        return os;

    const auto s{to_str(n.value_.get())};
    os << s.get();
    return os;
}

bool bsv::is_negative(const bint& n)
{
    const auto s{BN_is_negative(n.value_.get())};
    return s == 1;
}

bsv::bint bsv::abs(const bint& n) { return is_negative(n) ? -n : n; }

std::string bsv::to_string(const bint& n) // used in gdb pretty-printer
{
    std::ostringstream oss;
    oss << n;
    return oss.str();
}

namespace
{
    struct empty_asn1_deleter // See note 1
    {
        void operator()(ASN1_INTEGER* p) const { ::ASN1_INTEGER_free(p); }
    };
    using unique_asn1_ptr = std::unique_ptr<ASN1_INTEGER, empty_asn1_deleter>;
    static_assert(sizeof(unique_asn1_ptr) == sizeof(ASN1_INTEGER*));

    unique_asn1_ptr to_asn1(bignum_st* bn)
    {
        return unique_asn1_ptr{BN_to_ASN1_INTEGER(bn, nullptr)};
    }
}

long bsv::to_long(const bint& n)
{
    // Precondition:
    // Windows/MSVC (sizeof(long) == 4 bytes)
    // n <= numeric_limit<int32_t>::max() and n>=0

    // Linux/GCC (sizeof(long) == 8 bytes)
    // n <= numeric_limit<int64_t>::max() and n>=0

    const auto asn1{to_asn1(n.value_.get())};
    // assert(asn1);
    if(!asn1)
        throw big_int_error();

    //-1 means either error or an integer with a value of -1
    //(we don't want to use ASN1_INTEGER_get_uint64 because it's not supported
    // in older version of OpenSSL)
    return ASN1_INTEGER_get(asn1.get());
}

std::size_t bsv::to_size_t_limited(const bint& n)
{
    // Precondition:
    // Windows/MSVC (sizeof(long) == 4 bytes)
    // n <= numeric_limit<int32_t>::max() and n>=0

    // Linux/GCC (sizeof(long) == 8 bytes)
    // n <= numeric_limit<int64_t>::max() and n>=0

    const int64_t i64 = to_long(n);
    // assert(i64 >= 0);
    return static_cast<size_t>(i64);
}

namespace
{
    constexpr auto length_in_bytes{4};
}

std::vector<uint8_t> bsv::bint::serialize() const
{
    const auto len{BN_bn2mpi(value_.get(), nullptr)};
    // assert(len >= length_in_bytes);
    vector<unsigned char> result(len);
    BN_bn2mpi(value_.get(), result.data());
    result.erase(begin(result), begin(result) + length_in_bytes);
    reverse(begin(result), end(result));
    return result;
}

bsv::bint bsv::bint::deserialize(span<const uint8_t> s)
{
    const auto size{s.size()};
    vector<uint8_t> tmp(size + length_in_bytes);
    tmp[0] = (size >> 24) & 0xff;
    tmp[1] = (size >> 16) & 0xff;
    tmp[2] = (size >> 8) & 0xff;
    tmp[3] = (size >> 0) & 0xff;
    reverse_copy(begin(s), end(s), begin(tmp) + length_in_bytes);
    auto p{BN_mpi2bn(tmp.data(), tmp.size(), nullptr)};
    bint b;
    b.value_.reset(p);
    return b;
}

// Notes
// -----
// 1. Used to minimise size of the unique_ptr through empty base class
// optimization. See Effective Modern C++ Item 18

// 2. An object that is not well formed (i.e. moved from) can only be destroyed
// or assigned to (on the lhs). See 'Elements of Programming' Stepanov,
// Chapter 1.5
