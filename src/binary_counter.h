// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// Based on Elements of Programming (Stepanov, McJones) p.100.
// See also A9 Efficient Programming with Components: Lecture 6 Part 1 et seq
//
// binary_counter: incremental balanced reduction. Slots hold partial
// results; pushing more inputs cascades carries between slots - analagous to 
// binary arithmetic.
//
// For example, starting with an empty binary_counter and inserting values:
//
// insert a:    [ a,   _,    _,    ...]
// insert b:    [ _,   ab,   _,    ...]    (slot 0 carries up)
// insert c:    [ c,   ab,   _,    ...]
// insert d:    [ _,   _,    abcd, ...]    (slot 0 carries up, slot 1 carries up)
// insert e:    [ e,   _,    abcd, ...]
//
// After N pushes, occupied slots correspond to the set bits of N.
// reduce_counter folds them into one result; working memory is
// O(log N) regardless of input size.
// For more examples see binary_counter_tests.cpp

// Precondition on T: a default-constructed T (T{}) is treated as the
// empty-slot sentinel. If T's default value can occur as real input, wrap
// T in std::optional<T> so that nullopt becomes the unambiguous sentinel.

#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <iostream>
#include <iterator>
#include <type_traits>

template<std::forward_iterator I,
         typename T = std::iter_value_t<I>,
         typename Op>
T add_to_counter(I first, I last,
                 Op op,
                 T carry,
                 const T& zero)
{
    if(carry==zero)
        return zero;

    while(first != last)
    {
        if(*first == zero)
        {
            *first = carry;
            return zero;
        }
        carry = op(*first, carry);
        *first = zero;
        first++;
    }
    return carry;
}

template<typename>
struct op_return_type;

template<typename C, typename R, typename... Args>
struct op_return_type<R(C::*)(Args...) const>
{
    using type = std::decay_t<R>;
};

template<typename R, typename... Args>
struct op_return_type<R(*)(Args...)>
{
    using type = std::decay_t<R>;
};

template<typename Op, bool IsClass = std::is_class_v<Op>>
struct op_return_helper;

template<typename Op>
struct op_return_helper<Op, true>
{
    using type = op_return_type<decltype(&Op::operator())>::type;
};

template<typename Op>
struct op_return_helper<Op, false>
{
    using type = op_return_type<Op>::type;
};

template<typename Op>
using op_return_t = op_return_helper<std::remove_cvref_t<Op>>::type;

template<typename Op,
         typename T = op_return_t<Op>,
         size_t N=32>
    requires std::invocable<Op, T, T>
class binary_counter
{
    Op op_;
    T zero_{};
    using value_type = std::array<T, N>;
    value_type a_{};

    using iterator = value_type::iterator;
    using const_iterator = value_type::const_iterator;
    iterator first_{a_.begin()};
    iterator last_{first_};

public:
    binary_counter(Op op):op_{op}
    {}

    void operator()(const T& t)
    {
        const T tmp{add_to_counter(first_, last_,
                                   op_,
                                   t,
                                   zero_)};
        if(tmp != zero_)
        {
            *last_ = tmp;
            ++last_;
        }
    }

    template<std::forward_iterator I>
        requires std::same_as<std::iter_value_t<I>, T>
    void operator()(I first, I last)
    {
        while(first != last)
        {
            (*this)(*first);
            ++first;
        }
    }

    const_iterator begin() const { return a_.begin(); }
    const_iterator end() const { return last_; }
    std::size_t size() const { return std::distance(begin(), end()); }
    const T& operator[](std::size_t i) const { return a_[i]; }
};

template<typename Op>
binary_counter(Op) -> binary_counter<Op>;

template<typename R, typename T>
binary_counter(R(*)(const T&, const T&))
    -> binary_counter<R(*)(const T&, const T&), T>;


template<typename Op, typename T, std::size_t N>
std::ostream& operator<<(std::ostream& os, const binary_counter<Op, T, N>& bc)
{
    for(std::size_t i{}; i < bc.size(); ++i)
        os << bc[i] << ' ';
    return os;
}

// Reduce the non-zero slots of a binary_counter range. Op is called in
// tree order: op(older_slot, newer_accumulator). Higher slot indices
// hold older subtree-roots, so this matches the merkle/EoP combine
// convention rather than the std::reduce left-fold convention.
template<std::forward_iterator I,
         typename T = std::iter_value_t<I>,
         std::invocable<T, T> Op>
T reduce_counter(I first, I last,
                 Op op,
                 const T& zero)
{
    first = std::find_if(first, last,
                         [&zero](const T& v) { return v != zero; });
    if(first == last)
        return zero;

    T result{*first};
    while(++first != last)
    {
        const T y{*first};
        if(y != zero)
            result = op(y, result);
    }
    return result;
}

template<std::forward_iterator I,
         typename T = std::iter_value_t<I>,
         std::invocable<T, T> Op>
T reduce_balanced(I first, I last,
                  Op op,
                  const T& zero)
{
    binary_counter bc{op};
    bc(first, last);
    return reduce_counter(bc.begin(), bc.end(), op, zero);
}

