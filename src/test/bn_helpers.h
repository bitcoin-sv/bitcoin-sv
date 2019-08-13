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

