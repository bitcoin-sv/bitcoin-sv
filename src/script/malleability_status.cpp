// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "malleability_status.h"

#include <iostream>

#if 0
std::ostream& operator<<(std::ostream& os, const malleability_status& ms)
{
    if(!is_malleable(ms))
        os << "non_malleable";
    else
    {
        bool separate{false};
        if(is_unclean_stack(ms))
        {
            os << "unclean_stack";
            separate = true;
        }   
        if(is_non_minimal_encoding(ms))
        {
            if(separate)
                os << " | ";
            else
                separate = true;
            os << "non_minimal_encoding";
        }
        if(is_high_s(ms))
        {
            if(separate)
                os << " | ";
            else
                separate = true;
            os << "high_s";
        }
        if(has_non_push_data(ms))
        {
            if(separate)
                os << " | ";
            else
                separate = true;
            os << "non_push_data";
        }
    }

    if(is_disallowed(ms))
        os << " | disallowed";

    return os;
}
#endif
