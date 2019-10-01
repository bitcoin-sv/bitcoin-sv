// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "big_int.hpp"

#include "bn_helpers.h"
#include <boost/test/unit_test.hpp>

#include "script/int_serialization.h"
#include "script/interpreter.h"
#include <vector>

using namespace std;

using frame_type = vector<uint8_t>;
using stack_type = vector<frame_type>;

constexpr auto min64{std::numeric_limits<int64_t>::min()};
constexpr auto max64{std::numeric_limits<int64_t>::max()};

BOOST_AUTO_TEST_SUITE(bn_op_tests)

BOOST_AUTO_TEST_CASE(bint_bint_op)
{
    using bsv::bint;

    using polynomial = vector<int>;
    using test_args =
        tuple<int64_t, polynomial, polynomial, opcodetype, polynomial>;
    vector<test_args> test_data = {
        {max64, {1, 0, 0}, {1, 0, 0}, OP_ADD, {2, 0, 0}},
        {min64, {1, 0, 0}, {1, 0, 0}, OP_ADD, {2, 0, 0}},
        {max64, {-1, 0, 0}, {1, 0, 0}, OP_ADD, {0}},
        {min64, {1, 0, 0}, {-1, 0, 0}, OP_ADD, {0}},

        {max64, {2, 0, 0}, {1, 0, 0}, OP_SUB, {1, 0, 0}},

        {max64, {1, 0}, {1, 0}, OP_MUL, {1, 0, 0}},

        {max64, {1, 0, 0}, {1, 0}, OP_DIV, {1, 0}},

        {max64, {1, 0, 0}, {1, 0}, OP_MOD, {0}},
        {max64, {1, 1, 1}, {1, 0}, OP_MOD, {1}},
        {max64, {1, 1, 1, 1}, {1, 1, 0}, OP_MOD, {1, 1}},

        {max64, {1, 0, 0}, {1, 0, 0}, OP_BOOLAND, {1}},
        {max64, {1, 0, 0}, {0}, OP_BOOLAND, {0}},
        {max64, {0}, {1, 0, 0}, OP_BOOLAND, {0}},
        {max64, {0}, {0}, OP_BOOLAND, {0}},

        {max64, {1, 0, 0}, {1, 0, 0}, OP_BOOLOR, {1}},
        {max64, {1, 0, 0}, {0}, OP_BOOLOR, {1}},
        {max64, {0}, {1, 0, 0}, OP_BOOLOR, {1}},
        {max64, {0}, {0}, OP_BOOLOR, {0}},

        {max64, {1, 0, 0}, {1, 0, 0}, OP_NUMEQUAL, {1}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_NUMEQUAL, {0}},
        {max64, {1, 0, 0}, {2, 0, 0}, OP_NUMEQUAL, {0}},

        {max64, {1, 0, 0}, {1, 0, 0}, OP_NUMNOTEQUAL, {0}},
        {max64, {-1, 0, 0}, {1, 0, 0}, OP_NUMNOTEQUAL, {1}},

        {max64, {-1, 0, 0}, {1, 0, 0}, OP_LESSTHAN, {1}},
        {max64, {1, 0, 0}, {1, 0, 0}, OP_LESSTHAN, {0}},
        {max64, {-1, 0, 0}, {-1, 0, 0}, OP_LESSTHAN, {0}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_LESSTHAN, {0}},

        {max64, {-1, 0, 0}, {1, 0, 0}, OP_LESSTHANOREQUAL, {1}},
        {max64, {1, 0, 0}, {1, 0, 0}, OP_LESSTHANOREQUAL, {1}},
        {max64, {-1, 0, 0}, {-1, 0, 0}, OP_LESSTHANOREQUAL, {1}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_LESSTHANOREQUAL, {0}},

        {max64, {1, 0, 0}, {-1, 0, 0}, OP_GREATERTHAN, {1}},
        {max64, {1, 0, 0}, {1, 0, 0}, OP_GREATERTHAN, {0}},
        {max64, {-1, 0, 0}, {-1, 0, 0}, OP_GREATERTHAN, {0}},
        {max64, {-1, 0, 0}, {1, 0, 0}, OP_GREATERTHAN, {0}},

        {max64, {-1, 0, 0}, {1, 0, 0}, OP_GREATERTHANOREQUAL, {0}},
        {max64, {1, 0, 0}, {1, 0, 0}, OP_GREATERTHANOREQUAL, {1}},
        {max64, {-1, 0, 0}, {-1, 0, 0}, OP_GREATERTHANOREQUAL, {1}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_GREATERTHANOREQUAL, {1}},

        {max64, {-1, 0, 0}, {1, 0, 0}, OP_MIN, {-1, 0, 0}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_MIN, {-1, 0, 0}},

        {max64, {-1, 0, 0}, {1, 0, 0}, OP_MAX, {1, 0, 0}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_MAX, {1, 0, 0}},
    };

    for(const auto [n, arg_0_poly, arg_1_poly, op_code, exp_poly] : test_data)
    {
        stack_type stack;

        const bint bn{n};
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        const bint arg1 =
            polynomial_value(begin(arg_0_poly), end(arg_0_poly), bn);
        args.push_back(arg1.size_bytes());
        bsv::serialize(arg1, back_inserter(args));

        args.push_back(OP_PUSHDATA1);
        const bint arg2 =
            polynomial_value(begin(arg_1_poly), end(arg_1_poly), bn);
        args.push_back(arg2.size_bytes());
        bsv::serialize(arg2, back_inserter(args));

        args.push_back(op_code);

        CScript script(args.begin(), args.end());

        // uint32_t flags(1 << SCRIPT_ENABLE_BIG_INTS);
        uint32_t flags(1 << 17);
        ScriptError error;
        const auto status =
            EvalScript(stack, script, flags, BaseSignatureChecker{}, &error);
        BOOST_CHECK_EQUAL(true, status);
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1, stack.size());
        const auto frame = stack[0];
        const auto actual =
            frame.empty() ? 0
                          : bsv::deserialize<bint>(begin(frame), end(frame));
        bint expected = polynomial_value(begin(exp_poly), end(exp_poly), bn);
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(bint_bint_numequalverify)
{
    using bsv::bint;

    using polynomial = vector<int>;
    using test_args =
        tuple<int64_t, polynomial, polynomial, opcodetype, polynomial>;
    vector<test_args> test_data = {
        {max64, {1, 0, 0}, {1, 0, 0}, OP_NUMEQUALVERIFY, {0}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_NUMEQUALVERIFY, {0}},
        {max64, {2, 0, 0}, {-1, 0, 0}, OP_NUMEQUALVERIFY, {0}},
    };

    for(const auto [n, arg_0_poly, arg_1_poly, op_code, exp_poly] : test_data)
    {
        stack_type stack;

        const bint bn{n};
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        const bint arg1 =
            polynomial_value(begin(arg_0_poly), end(arg_0_poly), bn);
        args.push_back(arg1.size_bytes());
        bsv::serialize(arg1, back_inserter(args));

        args.push_back(OP_PUSHDATA1);
        const bint arg2 =
            polynomial_value(begin(arg_1_poly), end(arg_1_poly), bn);
        args.push_back(arg2.size_bytes());
        bsv::serialize(arg2, back_inserter(args));

        args.push_back(op_code);

        CScript script(args.begin(), args.end());

        // uint32_t flags(1 << SCRIPT_ENABLE_BIG_INTS);
        uint32_t flags(1 << 17);
        ScriptError error;
        const auto status =
            EvalScript(stack, script, flags, BaseSignatureChecker{}, &error);
        if(status)
        {
            BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
            BOOST_CHECK(stack.empty());
        }
        else
        {
            BOOST_CHECK_EQUAL(SCRIPT_ERR_NUMEQUALVERIFY, error);
            BOOST_CHECK_EQUAL(1, stack.size());
            const auto frame = stack[0];
            const auto actual =
                frame.empty()
                    ? 0
                    : bsv::deserialize<bint>(begin(frame), end(frame));
            bint expected =
                polynomial_value(begin(exp_poly), end(exp_poly), bn);
            BOOST_CHECK_EQUAL(expected, actual);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()


