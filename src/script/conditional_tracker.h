// Copyright (c) 2026 BSV Blockchain
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <vector>

/**
 * Tracks nested IF/ELSE/ENDIF conditional execution state during script
 * evaluation, with O(1) execution checks.
 */
class conditional_tracker
{
    std::vector<bool>::size_type false_count_{};
    std::vector<bool> conditions_;
    std::vector<bool> elses_;

public:
    void op_if(bool condition);
    void op_else();
    bool has_op_else() const;
    void op_endif();

    bool empty() const noexcept;
    bool is_active() const noexcept;
};
