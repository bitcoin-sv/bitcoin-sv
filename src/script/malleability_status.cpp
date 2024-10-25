// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "malleability_status.h"

#include <iostream>

std::ostream& operator<<(std::ostream& os, const malleability_status& ms)
{
    switch(ms.value())
    {
        case malleability_status::non_malleable:
            os << "non_malleable";
            break;
        case malleability_status::unclean_stack:
            os << "unclean_stack";
            break;
        case malleability_status::non_minimal_encoding:
            os << "non_minimal_encoding";
            break;
        case malleability_status::high_s:
            os << "high_s";
            break;
        case malleability_status::non_push_data:
            os << "non_push_data";
            break;
    }
    return os;
}

