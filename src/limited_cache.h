// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#pragma once

#include <algorithm>
#include <vector>

class limited_cache
{
    std::vector<size_t> v_;
    std::size_t head_{0}; 
    std::size_t max_;

public:
    explicit limited_cache(size_t max_size):max_{max_size}
    {
    }

    bool contains(size_t hash) const 
    { 
        return std::find(v_.begin(), v_.end(), hash) != v_.end();
    }

    void insert(size_t hash)
    {
        if(v_.size() < max_)
        {
            v_.push_back(hash);
            ++head_;
        }
        else
        {
            v_[head_] = hash;
            ++head_;
        }
        if(head_ == max_)
            head_ = 0;
    }
};

