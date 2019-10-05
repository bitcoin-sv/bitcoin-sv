// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

// requires Op models concept of Binary Operation with Domain(T)
// requires N models concept of Integer
template <typename T, typename Op, typename N>
T power_binary(T t, Op op, N n)
{
    while(n > 0)
    {
        t = op(t, t);
        --n;
    }
    return t;
}

// requires I models concept of InputIterator
// requires R models concept of Semiring
template<typename I, typename R>
inline R polynomial_value(I first, I last, R x) // See Note 1.
{
    if(first == last)
        return R{0};

    R sum{*first};
    while(++first != last)
    {
        sum *= x;
        sum += *first;
    }
    return sum;
}

// 1. See 'From Mathematics to Generic Programming' page 132 A.Stepanov& D.Rose
// Uses Horner's rule to evalutate a polynomial
// e.g. a sequence of {a, b, c, d}
//    a*(x^3) + b*(x^2) + c*(x^1) + d*(x^0)
//    ((a*x + b)x + c)x + d
//
// if x=2 and a sequence of {4, 7, 3, -5}
//    ((((4*2 + 7) * 2) + 3) * 2) - 5
