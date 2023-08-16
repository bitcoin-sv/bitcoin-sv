// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include "unique_array.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <memory>

#include <unistd.h>

using namespace std;

unique_array::unique_array(size_t n):
    p_{make_unique<value_type[]>(n)},
    cap_(n)
{}

unique_array::unique_array(std::span<const uint8_t> s)
    : p_{make_unique<value_type[]>(s.size())},
      cap_{s.size()},
      size_{s.size()}
{
    copy(s.begin(), s.end(), &p_[0]);
}
    
unique_array::unique_array(unique_array&& a) noexcept:
    p_{std::move(a.p_)},
    cap_{a.cap_},
    size_{a.size_}
{
    a.p_ = make_unique<value_type[]>(0);
    a.cap_ = 0;
    a.size_ = 0;
}

unique_array& unique_array::operator=(unique_array&& a) noexcept
{
    p_ = std::move(a.p_);
    cap_ = a.cap_;
    size_ = a.size_;

    a.p_ = make_unique<value_type[]>(0);
    a.cap_ = 0;
    a.size_ = 0;
    return *this;
}
    
void unique_array::reserve(size_t n)
{
    if(n <= cap_ || n <= size_)
        return;

    auto tmp{make_unique<value_type[]>(n)}; 
    copy(&p_[0], &p_[size_], &tmp[0]);
    p_ = std::move(tmp);
    cap_ = n;
}
    
void unique_array::push_back(const value_type& v)
{
    append(&v, 1);
}

void unique_array::append(const std::span<const uint8_t> s)
{
    append(s.data(), s.size());
}

void unique_array::reallocate(size_t delta)
{
    reserve(size_ + max(size_, delta));
}

void unique_array::shrink_to_fit()
{
    if(size_ == cap_)
        return;

    unique_array a(size_);
    a.insert(a.cbegin(), cbegin(), cend());

    swap(*this, a);
}

void unique_array::append(const value_type* p, size_t n)
{
    if(size_ + n > cap_)
        reallocate(n);

    copy(&p[0], &p[n], &p_[size_]);
    size_ += n;
}

void unique_array::reset()
{
    p_ = make_unique<value_type[]>(0);
    size_ = 0;
    cap_ = 0;
}

size_t read(const unique_array& a, size_t read_pos, std::span<uint8_t> s)
{
    if(a.empty())
        return 0;
    
    assert(read_pos < a.size());

    const auto n{min(a.size() - read_pos, s.size())};
    copy(a.cbegin() + read_pos,
         a.cbegin() + read_pos + n,
         s.begin()); 
    return n; 
}

