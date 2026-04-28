// Copyright (c) 2026 BSV Blockchain
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "script/conditional_tracker.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(conditional_tracker_tests)

BOOST_AUTO_TEST_CASE(default_cons)
{
    conditional_tracker ct;
    BOOST_CHECK(ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());
}

BOOST_AUTO_TEST_CASE(op_if_true)
{
    conditional_tracker ct;
    ct.op_if(true);
    BOOST_CHECK(!ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());

    ct.op_endif();
    BOOST_CHECK(ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());
}

BOOST_AUTO_TEST_CASE(op_if_false)
{
    conditional_tracker ct;
    ct.op_if(false);
    BOOST_CHECK(!ct.empty());
    BOOST_CHECK(!ct.is_active());
    BOOST_CHECK(!ct.has_op_else());

    ct.op_endif();
    BOOST_CHECK(ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());
}

BOOST_AUTO_TEST_CASE(inactive_op_else)
{
    conditional_tracker ct;
    ct.op_if(true);
    BOOST_CHECK(!ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());

    ct.op_else();
    BOOST_CHECK(!ct.empty());
    BOOST_CHECK(!ct.is_active());
    BOOST_CHECK(ct.has_op_else());

    ct.op_endif();
    BOOST_CHECK(ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());
}

BOOST_AUTO_TEST_CASE(active_op_else)
{
    conditional_tracker ct;
    ct.op_if(false);
    BOOST_CHECK(!ct.empty());
    BOOST_CHECK(!ct.is_active());
    BOOST_CHECK(!ct.has_op_else());

    ct.op_else();
    BOOST_CHECK(!ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(ct.has_op_else());

    ct.op_endif();
    BOOST_CHECK(ct.empty());
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());
}

BOOST_AUTO_TEST_CASE(nested_all_true)
{
    conditional_tracker ct;
    ct.op_if(true);
    ct.op_if(true);
    ct.op_if(true);
    BOOST_CHECK(ct.is_active());

    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(ct.empty());
}

BOOST_AUTO_TEST_CASE(nested_false_blocks_inner)
{
    conditional_tracker ct;
    ct.op_if(true);
    BOOST_CHECK(ct.is_active());
    ct.op_if(false);
    BOOST_CHECK(!ct.is_active());
    ct.op_if(true);
    BOOST_CHECK(!ct.is_active());

    ct.op_endif();
    BOOST_CHECK(!ct.is_active());
    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(ct.empty());
}

BOOST_AUTO_TEST_CASE(toggle_restores_executing_after_nested_false)
{
    conditional_tracker ct;
    ct.op_if(true);
    ct.op_if(false);
    BOOST_CHECK(!ct.is_active());

    ct.op_else();
    BOOST_CHECK(ct.is_active());

    ct.op_endif();
    ct.op_endif();
    BOOST_CHECK(ct.empty());
}

BOOST_AUTO_TEST_CASE(multiple_false_levels)
{
    conditional_tracker ct;
    ct.op_if(false);
    ct.op_if(false);
    ct.op_if(false);
    BOOST_CHECK(!ct.is_active());

    ct.op_endif();
    BOOST_CHECK(!ct.is_active());
    ct.op_endif();
    BOOST_CHECK(!ct.is_active());
    ct.op_endif();
    BOOST_CHECK(ct.is_active());
}

BOOST_AUTO_TEST_CASE(if_else_endif_sequence)
{
    conditional_tracker ct;
    ct.op_if(true);
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.has_op_else());

    ct.op_else();
    BOOST_CHECK(!ct.is_active());
    BOOST_CHECK(ct.has_op_else());

    ct.op_endif();
    BOOST_CHECK(ct.empty());
    BOOST_CHECK(ct.is_active());
}

BOOST_AUTO_TEST_CASE(if_else_endif_false_condition)
{
    conditional_tracker ct;
    ct.op_if(false);
    BOOST_CHECK(!ct.is_active());

    ct.op_else();
    BOOST_CHECK(ct.is_active());

    ct.op_endif();
    BOOST_CHECK(ct.empty());
}

BOOST_AUTO_TEST_CASE(pop_false_with_remaining_true)
{
    conditional_tracker ct;
    ct.op_if(true);
    ct.op_if(false);
    BOOST_CHECK(!ct.is_active());

    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.empty());

    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(ct.empty());
}

BOOST_AUTO_TEST_CASE(pop_false_with_remaining_false)
{
    conditional_tracker ct;
    ct.op_if(false);
    ct.op_if(false);
    BOOST_CHECK(!ct.is_active());

    ct.op_endif();
    BOOST_CHECK(!ct.is_active());
    BOOST_CHECK(!ct.empty());

    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(ct.empty());
}

BOOST_AUTO_TEST_CASE(pop_true_with_remaining_false)
{
    conditional_tracker ct;
    ct.op_if(false);
    ct.op_if(true);
    BOOST_CHECK(!ct.is_active());

    ct.op_endif();
    BOOST_CHECK(!ct.is_active());

    ct.op_endif();
    BOOST_CHECK(ct.is_active());
}

BOOST_AUTO_TEST_CASE(toggle_then_pop_with_remaining)
{
    conditional_tracker ct;
    ct.op_if(true);
    ct.op_if(true);
    BOOST_CHECK(ct.is_active());

    ct.op_else();
    BOOST_CHECK(!ct.is_active());

    ct.op_endif();
    BOOST_CHECK(ct.is_active());
    BOOST_CHECK(!ct.empty());

    ct.op_endif();
    BOOST_CHECK(ct.empty());
}

BOOST_AUTO_TEST_SUITE_END()
