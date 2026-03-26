// Copyright (c) 2026 BSV Blockchain
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
//
// conditions_: Purpose:execution control — records if each nesting level is active.
//              That is, whether an if condition was true.
//              All must be true for opcodes to execute.
// elses_:   grammar guard — an OP_IF need not have an OP_ELSE.
//           Records whether an else exists for each OP_IF.
//           Prevents duplicate OP_ELSE at the same level (post-Genesis).
// false_count_: number of false entries in conditions_; O(1) execution check.
//
// Example trace — script: OP_1 OP_IF OP_1 OP_IF OP_ELSE OP_ENDIF OP_0 OP_IF OP_ELSE OP_ENDIF OP_ELSE OP_ENDIF
//
//                     conditions_:              elses_:                false_count_:
// OP_1                []                     []                     0
// OP_IF (true)        [true]                 [false]                0  executing
//   OP_1              [true]                 [false]                0  executing
//   OP_IF (true)      [true,true]            [false,false]          0  executing
//   OP_ELSE           [true,false]           [false,true]           1  skipping; else seen
//   OP_ENDIF          [true]                 [false]                0  executing
//   OP_0              [true]                 [false]                0  executing
//   OP_IF (false)     [true,false]           [false,false]          1  skipping
//   OP_ELSE           [true,true]            [false,true]           0  executing; else seen
//   OP_ENDIF          [true]                 [false]                0  executing
// OP_ELSE             [false]                [true]                 1  skipping; else seen
// OP_ENDIF            []                     []                     0  executing

#include "script/conditional_tracker.h"

#include <cassert>

bool conditional_tracker::empty() const noexcept
{
    return conditions_.empty();
}

void conditional_tracker::op_if(const bool condition)
{
    conditions_.push_back(condition);
    elses_.push_back(false);

    if(!condition)
        ++false_count_;
}

bool conditional_tracker::is_active() const noexcept
{
    return false_count_ == 0;
}

bool conditional_tracker::has_op_else() const
{
    // Post-Genesis grammar check - only 1 OP_ELSE allowed.
    return elses_.empty() ? false : elses_.back();
}

void conditional_tracker::op_else()
{
    assert(!empty());

    conditions_.back() = !conditions_.back();
    elses_.back() = true;

    conditions_.back() ? --false_count_ : ++false_count_;
}

void conditional_tracker::op_endif()
{
    assert(!empty());

    if(!conditions_.back())
        --false_count_;

    conditions_.pop_back();
    elses_.pop_back();
}

