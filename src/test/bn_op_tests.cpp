// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "big_int.h"

#include "bn_helpers.h"
#include <boost/test/unit_test.hpp>

#include "script/int_serialization.h"
#include "script/interpreter.h"
#include "script/script_flags.h"
#include "taskcancellation.h"

#include "config.h"
#include <vector>

using namespace std;

using frame_type = vector<uint8_t>;
using stack_type = vector<frame_type>;

using bsv::bint;

constexpr auto min64{std::numeric_limits<int64_t>::min() + 1};
constexpr auto max64{std::numeric_limits<int64_t>::max()};

BOOST_AUTO_TEST_SUITE(bn_op_tests)

BOOST_AUTO_TEST_CASE(bint_unary_ops)
{
    const Config& config = GlobalConfig::GetConfig();

    using polynomial = vector<int>;
    using test_args = tuple<int64_t, polynomial, opcodetype, polynomial>;
    // clang-format off
    vector<test_args> test_data = {
        {0, {-2}, OP_1ADD, {-1}},
        {0, {-1}, OP_1ADD, {0}},
        {0, {0}, OP_1ADD, {1}},
        {0, {1}, OP_1ADD, {2}},
        {max64, {1, 0}, OP_1ADD, {1, 1}},
        {max64, {1, 1}, OP_1ADD, {1, 2}},

        {0, {-1}, OP_1SUB, {-2}},
        {0, {0}, OP_1SUB, {-1}},
        {0, {1}, OP_1SUB, {0}},
        {0, {2}, OP_1SUB, {1}},
        {min64, {1, 0}, OP_1SUB, {1, -1}},
        
        {0, {-1}, OP_NEGATE, {1}},
        {0, {0}, OP_NEGATE, {0}},
        {0, {1}, OP_NEGATE, {-1}},
        {max64, {1, 0}, OP_NEGATE, {-1, 0}},
        {max64, {1, 1}, OP_NEGATE, {-1, -1}},
        {min64, {1, 0}, OP_NEGATE, {-1, 0}},
        {min64, {1, -1}, OP_NEGATE, {-1, 1}},
      
        {0, {-1}, OP_ABS, {1}},
        {0, {0}, OP_ABS, {0}},
        {0, {1}, OP_ABS, {1}},
        {max64, {1, 1}, OP_ABS, {1, 1}},
        {min64, {1, 1}, OP_ABS, {-1, -1}},
        
        {0, {-1}, OP_NOT, {0}},
        {0, {0}, OP_NOT, {1}},
        {0, {1}, OP_NOT, {0}},
        {max64, {1, 1}, OP_NOT, {0}},
        {min64, {1, 1}, OP_NOT, {0}},
        
        {0, {-1}, OP_0NOTEQUAL, {1}},
        {0, {0}, OP_0NOTEQUAL, {0}},
        {0, {1}, OP_0NOTEQUAL, {1}},
        {max64, {1, 1}, OP_0NOTEQUAL, {1}},
        {min64, {1, 1}, OP_0NOTEQUAL, {1}},
    };
    // clang-format on

    for(const auto& [n, arg_poly, op_code, exp_poly] : test_data)
    {
        const bint bn{n};

        vector<uint8_t> args;
        args.push_back(OP_PUSHDATA1);

        const bint arg = polynomial_value(begin(arg_poly), end(arg_poly), bn);
        const auto arg_serialized{arg.serialize()};
        args.push_back(arg_serialized.size());
        copy(begin(arg_serialized), end(arg_serialized), back_inserter(args));

        args.push_back(op_code);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        LimitedStack stack(UINT32_MAX);
        const auto status =
            EvalScript(config, false, source->GetToken(), stack, script, flags,
                       BaseSignatureChecker{}, &error);
        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1U, stack.size());
        const auto frame = stack.front();
        const auto actual =
            frame.empty() ? bint{0} 
                          : bsv::bint::deserialize(frame.GetElement());
        const bint expected =
            polynomial_value(begin(exp_poly), end(exp_poly), bn);
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(bint_binary_ops)
{
    const Config& config = GlobalConfig::GetConfig();

    using polynomial = vector<int>;
    using test_args =
        tuple<int64_t, polynomial, polynomial, opcodetype, polynomial>;
    vector<test_args> test_data = {
        {max64, {1, 1}, {1, 1}, OP_ADD, {2, 2}},
        {max64, {1, 1, 1}, {1, 0, 0}, OP_ADD, {2, 1, 1}},
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

    for(const auto& [n, arg_0_poly, arg_1_poly, op_code, exp_poly] : test_data)
    {
        LimitedStack stack(UINT32_MAX);

        const bint bn{n};
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        const bint arg1 =
            polynomial_value(begin(arg_0_poly), end(arg_0_poly), bn);
        const auto arg1_serialized{arg1.serialize()};
        args.push_back(arg1_serialized.size());
        copy(begin(arg1_serialized), end(arg1_serialized), back_inserter(args));

        args.push_back(OP_PUSHDATA1);
        const bint arg2 =
            polynomial_value(begin(arg_1_poly), end(arg_1_poly), bn);
        const auto arg2_serialized{arg2.serialize()};
        args.push_back(arg2_serialized.size());
        copy(begin(arg2_serialized), end(arg2_serialized), back_inserter(args));

        args.push_back(op_code);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        const auto status =
            EvalScript(config, true, source->GetToken(), stack, script, flags,
                       BaseSignatureChecker{}, &error);
        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1U, stack.size());
        const auto frame = stack.front();
        const auto actual =
            frame.empty() ? bint{0}
                          : bsv::bint::deserialize(frame.GetElement());
        bint expected = polynomial_value(begin(exp_poly), end(exp_poly), bn);
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(bint_ternary_ops)
{
    const Config& config = GlobalConfig::GetConfig();

    using polynomial = vector<int>;
    using test_args = tuple<int64_t, polynomial, polynomial, polynomial,
                            opcodetype, polynomial>;
    vector<test_args> test_data = {
        {0, {-1}, {0}, {2}, OP_WITHIN, {0}}, // too low
        {0, {0}, {0}, {2}, OP_WITHIN, {1}},  // lower boundary
        {0, {1}, {0}, {2}, OP_WITHIN, {1}},  // in-between
        {0, {2}, {0}, {2}, OP_WITHIN, {0}},  // upper boundary
        {0, {4}, {0}, {2}, OP_WITHIN, {0}},  // too high

        {max64, {1, -1}, {1, 0}, {1, 2}, OP_WITHIN, {0}}, // too low
        {max64, {1, 0}, {1, 0}, {1, 2}, OP_WITHIN, {1}},  // lower boundary
        {max64, {1, 1}, {1, 0}, {1, 2}, OP_WITHIN, {1}},  // in-between
        {max64, {1, 2}, {1, 0}, {1, 2}, OP_WITHIN, {0}},  // upper boundary
        {max64, {1, 4}, {1, 0}, {1, 2}, OP_WITHIN, {0}},  // too high

        {max64, {2, -1}, {2, 0}, {2, 2}, OP_WITHIN, {0}}, // too low
        {max64, {2, 0}, {2, 0}, {2, 2}, OP_WITHIN, {1}},  // lower boundary
        {max64, {2, 1}, {2, 0}, {2, 2}, OP_WITHIN, {1}},  // in-between
        {max64, {2, 2}, {2, 0}, {2, 2}, OP_WITHIN, {0}},  // upper boundary
        {max64, {2, 4}, {2, 0}, {2, 2}, OP_WITHIN, {0}},  // too high
    };

    for(const auto& [n, arg_0_poly, arg_1_poly, arg_2_poly, op_code, exp_poly] : test_data)
    {
        LimitedStack stack(UINT32_MAX);

        const bint bn{n};
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        const bint arg1 =
            polynomial_value(begin(arg_0_poly), end(arg_0_poly), bn);
        const auto arg1_serialized{arg1.serialize()};
        args.push_back(arg1_serialized.size());
        copy(begin(arg1_serialized), end(arg1_serialized), back_inserter(args));

        args.push_back(OP_PUSHDATA1);
        const bint arg2 =
            polynomial_value(begin(arg_1_poly), end(arg_1_poly), bn);
        const auto arg2_serialized{arg2.serialize()};
        args.push_back(arg2_serialized.size());
        copy(begin(arg2_serialized), end(arg2_serialized), back_inserter(args));

        args.push_back(OP_PUSHDATA1);
        const bint arg3 =
            polynomial_value(begin(arg_2_poly), end(arg_2_poly), bn);
        const auto arg3_serialized{arg3.serialize()};
        args.push_back(arg3_serialized.size());
        copy(begin(arg3_serialized), end(arg3_serialized), back_inserter(args));

        args.push_back(op_code);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        const auto status =
            EvalScript(config, true, source->GetToken(), stack, script, flags,
                       BaseSignatureChecker{}, &error);
        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1U, stack.size());
        const auto frame = stack.front();
        const auto actual =
            frame.empty() ? bint{0}
                          : bsv::bint::deserialize(frame.GetElement());
        bint expected = polynomial_value(begin(exp_poly), end(exp_poly), bn);
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(bint_bint_numequalverify)
{
    const Config& config = GlobalConfig::GetConfig();

    using polynomial = vector<int>;
    using test_args =
        tuple<int64_t, polynomial, polynomial, opcodetype, polynomial>;
    vector<test_args> test_data = {
        {max64, {1, 1}, {1, 1}, OP_NUMEQUALVERIFY, {0}},
        {max64, {1, 0, 0}, {1, 0, 0}, OP_NUMEQUALVERIFY, {0}},
        {max64, {1, 0, 0}, {-1, 0, 0}, OP_NUMEQUALVERIFY, {0}},
        {max64, {2, 0, 0}, {-1, 0, 0}, OP_NUMEQUALVERIFY, {0}},
    };

    for(const auto& [n, arg_0_poly, arg_1_poly, op_code, exp_poly] : test_data)
    {
        LimitedStack stack(UINT32_MAX);

        const bint bn{n};
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        const bint arg1 =
            polynomial_value(begin(arg_0_poly), end(arg_0_poly), bn);
        const auto arg1_serialized{arg1.serialize()};
        args.push_back(arg1_serialized.size());
        copy(begin(arg1_serialized), end(arg1_serialized), back_inserter(args));

        args.push_back(OP_PUSHDATA1);
        const bint arg2 =
            polynomial_value(begin(arg_1_poly), end(arg_1_poly), bn);
        const auto arg2_serialized{arg2.serialize()};
        args.push_back(arg2_serialized.size());
        copy(begin(arg2_serialized), end(arg2_serialized), back_inserter(args));

        args.push_back(op_code);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        const auto status =
            EvalScript(config, true, source->GetToken(), stack, script, flags,
                       BaseSignatureChecker{}, &error);
        if(status.value())
        {
            BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
            BOOST_CHECK(stack.empty());
        }
        else
        {
            BOOST_CHECK_EQUAL(SCRIPT_ERR_NUMEQUALVERIFY, error);
            BOOST_CHECK_EQUAL(1U, stack.size());
            auto frame = stack.front();
            auto actual =
                frame.empty()
                    ? bint{0}
                    : bsv::bint::deserialize(frame.GetElement());
            bint expected =
                polynomial_value(begin(exp_poly), end(exp_poly), bn);
            BOOST_CHECK_EQUAL(expected, actual);
        }
    }
}

BOOST_AUTO_TEST_CASE(operands_too_large)
{
    ConfigInit& config = GlobalConfig::GetModifiableGlobalConfig();
    using test_args = tuple<uint64_t, uint64_t, opcodetype, bool, ScriptError>;
    const auto max_arg_len{ MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS };

    // set policy for script size, stack memory usage and max number length in scripts 
    // to default after genesis
    config.SetMaxScriptSizePolicy(0);
    config.SetMaxStackMemoryUsage(0, 0);
    config.SetMaxScriptNumLengthPolicy(max_arg_len);

    // clang-format off
    vector<test_args> test_data = {
    {max_arg_len,   max_arg_len,   OP_ADD, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_ADD, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_ADD, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_ADD, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_SUB, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_SUB, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_SUB, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_SUB, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_MUL, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_MUL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_MUL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_MUL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_DIV, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_DIV, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_DIV, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_DIV, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_MOD, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_MOD, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_MOD, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_MOD, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_BOOLAND, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_BOOLAND, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_BOOLAND, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_BOOLAND, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_BOOLOR, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_BOOLOR, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_BOOLOR, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_BOOLOR, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_NUMEQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_NUMEQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_NUMEQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_NUMEQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_NUMNOTEQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_NUMNOTEQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_NUMNOTEQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_NUMNOTEQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_LESSTHAN, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_LESSTHAN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_LESSTHAN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_LESSTHAN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_LESSTHANOREQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_LESSTHANOREQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_LESSTHANOREQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_LESSTHANOREQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_GREATERTHAN, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_GREATERTHAN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_GREATERTHAN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_GREATERTHAN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_GREATERTHANOREQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_GREATERTHANOREQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_GREATERTHANOREQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_GREATERTHANOREQUAL, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_MIN, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_MIN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_MIN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_MIN, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len,   OP_MAX, true,  SCRIPT_ERR_OK},
    {max_arg_len + 1, max_arg_len,   OP_MAX, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len,   max_arg_len + 1, OP_MAX, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    {max_arg_len + 1, max_arg_len + 1, OP_MAX, false, SCRIPT_ERR_SCRIPTNUM_OVERFLOW},
    };
    // clang-format on

    for (const auto& [arg0_size, arg1_size, op_code, exp_status,
        exp_script_error] : test_data)
    {
        LimitedStack stack(UINT32_MAX);

        vector<uint8_t> arg0(arg0_size, 42);
        vector<uint8_t> arg1(arg1_size, 69);

        CScript script = CScript() << arg0 << arg1 << op_code;

        const auto flags{ SCRIPT_UTXO_AFTER_GENESIS };
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        const auto status =
            EvalScript(config, false, source->GetToken(), stack, script, flags,
                BaseSignatureChecker{}, &error);
        BOOST_CHECK_EQUAL(exp_status, status.value());
        BOOST_CHECK_EQUAL(exp_script_error, error);
        BOOST_CHECK_EQUAL(status.value() ? 1U : 2U, stack.size());
    }
}

BOOST_AUTO_TEST_CASE(op_bin2num)
{
    const Config& config = GlobalConfig::GetConfig();
    // clang-format off
    vector<tuple<vector<uint8_t>, vector<uint8_t>>> test_data = {
        { {}, {}},
        { {0x1}, {0x1}},               // +1
        { {0x7f}, {0x7f}},             // +127 
        { {0x80, 0x0}, {0x80, 0x0}},   // +128
        { {0xff, 0x0}, {0xff, 0x0}},   // 255
        { {0x81}, {0x81}},             // -1
        { {0xff}, {0xff}},             // -127 
        { {0x80, 0x80}, {0x80, 0x80}}, // -128
        { {0xff, 0x80}, {0xff, 0x80}}, // -255
        { {0x1, 0x0}, {0x1}},           // should be 0x1 for +1
        { {0x7f, 0x80}, {0xff}},        // should be 0xff for -127
        { {0x1, 0x2, 0x3, 0x4, 0x5}, {0x1, 0x2, 0x3, 0x4, 0x5}} // invalid range?
    };
    // clang-format on
    for(auto& [ip, op] : test_data)
    {
        LimitedStack stack(UINT32_MAX);
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        args.push_back(ip.size());
        copy(begin(ip), end(ip), back_inserter(args));

        args.push_back(OP_BIN2NUM);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        const auto status = EvalScript(
            config, false, task::CCancellationSource::Make()->GetToken(), stack,
            script, flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1U, stack.size());
        BOOST_CHECK_EQUAL(op.size(), stack.front().size());
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack.front()), end(stack.front()), begin(op),
                                      end(op));
    }
}

BOOST_AUTO_TEST_CASE(op_num2bin)
{
    const Config& config = GlobalConfig::GetConfig();
    // clang-format off
    vector<tuple<vector<uint8_t>, 
                 vector<uint8_t>,
                 bool,
                 ScriptError,
                 vector<uint8_t>>> test_data = {

        { {}, {}, true, SCRIPT_ERR_OK, {}},
        { {}, {0x0}, true, SCRIPT_ERR_OK, {}},
        { {}, {0x1}, true, SCRIPT_ERR_OK, {0x0}},
        { {}, {0x2}, true, SCRIPT_ERR_OK, {0x0, 0x0}},
        { {0x0}, {0x0}, true, SCRIPT_ERR_OK, {}},
        { {0x0}, {0x1}, true, SCRIPT_ERR_OK, {0x0}},
        { {0x0}, {0x2}, true, SCRIPT_ERR_OK, {0x0, 0x0}},
        { {0x1}, {0x1}, true, SCRIPT_ERR_OK, { 0x1}},
        { {0x1, 0x2}, {0x2}, true, SCRIPT_ERR_OK, { 0x1, 0x2}},
        { {0x1, 0x2, 0x3}, {0x3}, true, SCRIPT_ERR_OK, { 0x1, 0x2, 0x3}},
        { {0x1, 0x2, 0x3, 0x4}, {0x4}, true, SCRIPT_ERR_OK, { 0x1, 0x2, 0x3, 0x4}},
        { {0x1, 0x2, 0x3, 0x4, 0x5}, {0x5}, true, SCRIPT_ERR_OK, { 0x1, 0x2, 0x3, 0x4, 0x5}},
        
        // 0x0 used as padding
        { {0x1}, {0x2}, true, SCRIPT_ERR_OK, {0x1, 0x0}},
        { {0x2}, {0x2}, true, SCRIPT_ERR_OK, {0x2, 0x0}},

        // -ve numbers 
        { {0x81}, {0x1}, true, SCRIPT_ERR_OK, {0x81}},          
        { {0x81}, {0x2}, true, SCRIPT_ERR_OK, {0x1, 0x80}},     
        { {0x81}, {0x3}, true, SCRIPT_ERR_OK, {0x1, 0x0, 0x80}},

        // -ve length
        { {0x1}, {0x81}, false, SCRIPT_ERR_PUSH_SIZE, {0x1}},

        // requested length to short
        { {0x1}, {}, false, SCRIPT_ERR_IMPOSSIBLE_ENCODING, {0x1}},
        { {0x1}, {0x0}, false, SCRIPT_ERR_IMPOSSIBLE_ENCODING, {0x1}},
        { {0x1, 0x2}, {0x1}, false, SCRIPT_ERR_IMPOSSIBLE_ENCODING, { 0x1, 0x2}},

    };
    // clang-format on
    for(auto& [arg1, arg2, exp_status, exp_error, op] : test_data)
    {
        LimitedStack stack(UINT32_MAX);
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        args.push_back(arg1.size());
        copy(begin(arg1), end(arg1), back_inserter(args));

        args.push_back(OP_PUSHDATA1);
        args.push_back(arg2.size());
        copy(begin(arg2), end(arg2), back_inserter(args));

        args.push_back(OP_NUM2BIN);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        const auto status = EvalScript(
            config, false, task::CCancellationSource::Make()->GetToken(), stack,
            script, flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(exp_status, status.value());
        BOOST_CHECK_EQUAL(exp_error, error);
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack.front()), end(stack.front()), begin(op),
                                      end(op));
    }
}

BOOST_AUTO_TEST_CASE(op_depth)
{
    const Config& config = GlobalConfig::GetConfig();

    const vector<size_t> test_data = {0, 1, 20'000};
    for(const auto i : test_data)
    {
        LimitedStack stack(UINT32_MAX);
        vector<uint8_t> args(i, OP_0);

        args.push_back(OP_DEPTH);

        CScript script(args.begin(), args.end());

        const auto cancellation_source{task::CCancellationSource::Make()};
        const auto token{cancellation_source->GetToken()};
        const auto flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_GENESIS};
        ScriptError error;
        const auto status = EvalScript(config, true, token, stack, script,
                                       flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(i + 1, stack.size());
        vector<uint8_t> op;
        bsv::serialize<int>(i, back_inserter(op));
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack.at(i)), end(stack.at(i)), begin(op),
                                      end(op));
    }
}

BOOST_AUTO_TEST_CASE(op_size)
{
    const Config& config = GlobalConfig::GetConfig();

    using polynomial = vector<int>;
    using test_args = tuple<int64_t, polynomial>;
    vector<test_args> test_data = {
        {2, {1, 1}},
        {max64, {1, 1} },
    };

    for(const auto& [n, arg_poly] : test_data)
    {
        const bint bn{n};

        vector<uint8_t> args;
        args.push_back(OP_PUSHDATA1);

        const bint arg = polynomial_value(begin(arg_poly), end(arg_poly), bn);
        const auto arg_serialized{arg.serialize()};
        args.push_back(arg_serialized.size());
        copy(begin(arg_serialized), end(arg_serialized), back_inserter(args));

        args.push_back(OP_SIZE);

        CScript script(args.begin(), args.end());

        const auto cancellation_source{task::CCancellationSource::Make()};
        const auto token{cancellation_source->GetToken()};
        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(config, false, token, stack, script,
                                       flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(2U, stack.size());
        const auto expected{stack.front().size()};
        const auto actual{bsv::deserialize(begin(stack.at(1)), end(stack.at(1)))};
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(op_pick)
{
    const Config& config = GlobalConfig::GetConfig();

    using test_args = tuple<opcodetype, size_t>;
    vector<test_args> test_data = {
        {OP_0, 2},
        {OP_1, 1},
        {OP_2, 0},
    };

    for(const auto& [op_code, i] : test_data)
    {
        vector<uint8_t> args;
        args.push_back(OP_0);
        args.push_back(OP_1);
        args.push_back(OP_2);
        args.push_back(op_code);
        args.push_back(OP_PICK);

        CScript script(args.begin(), args.end());

        const auto cancellation_source{task::CCancellationSource::Make()};
        const auto token{cancellation_source->GetToken()};
        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(config, false, token, stack, script,
                                       flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(4U, stack.size());
        if(op_code == OP_2)
            BOOST_CHECK(stack.at(3).empty());
        else
            BOOST_CHECK_EQUAL(stack.at(i).front(), stack.at(3).front());
    }
}

BOOST_AUTO_TEST_CASE(op_roll)
{
    const Config& config = GlobalConfig::GetConfig();

    vector<opcodetype> test_data{
        OP_0,
        OP_1,
        OP_2,
    };

    for(const auto op_code : test_data)
    {
        vector<uint8_t> args;
        args.push_back(OP_0);
        args.push_back(OP_1);
        args.push_back(OP_2);
        args.push_back(op_code);
        args.push_back(OP_ROLL);

        CScript script(args.begin(), args.end());

        const auto cancellation_source{task::CCancellationSource::Make()};
        const auto token{cancellation_source->GetToken()};
        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(config, false, token, stack, script,
                                       flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(3U, stack.size());

        if(op_code == OP_0)
        {
            BOOST_CHECK_EQUAL(2, stack.at(2).front());
            BOOST_CHECK_EQUAL(1, stack.at(1).front());
            BOOST_CHECK(stack.at(0).empty());
        }
        else if(op_code == OP_1)
        {
            BOOST_CHECK_EQUAL(1, stack.at(2).front());
            BOOST_CHECK_EQUAL(2, stack.at(1).front());
            BOOST_CHECK(stack.at(0).empty());
        }
        else if(op_code == OP_2)
        {
            BOOST_CHECK(stack.at(2).empty());
            BOOST_CHECK_EQUAL(2, stack.at(1).front());
            BOOST_CHECK_EQUAL(1, stack.at(0).front());
        }
        else
            BOOST_CHECK(false);
    }
}

BOOST_AUTO_TEST_CASE(op_split)
{
    const Config& config = GlobalConfig::GetConfig();

    using data = vector<uint8_t>;
    using test_args = tuple<size_t, opcodetype, data, data>;
    // clang-format off
    vector<test_args> test_data = 
    {
        {0, OP_0, {}, {0, 1}},
        {1, {OP_1}, {0}, {1}},
        {2, {OP_2}, {0, 1}, {}},
    };
    // clang-format on

    for (const auto& [i, pos, lhs, rhs] : test_data) {
        vector<uint8_t> args;
        args.push_back(0x2);
        args.push_back(0x0);
        args.push_back(0x1);
        args.push_back(pos);

        args.push_back(OP_SPLIT);

        CScript script(args.begin(), args.end());

        const auto cancellation_source{task::CCancellationSource::Make()};
        const auto token{cancellation_source->GetToken()};
        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(config, false, token, stack, script,
                                       flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(2U, stack.size());
        BOOST_CHECK_EQUAL(2 - i, stack.at(1).size());
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack.at(1)), end(stack.at(1)),
                                      begin(rhs), end(rhs));
        BOOST_CHECK_EQUAL(i, stack.front().size());
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack.front()), end(stack.front()),
                                      begin(lhs), end(lhs));
    }
}

BOOST_AUTO_TEST_CASE(op_lshift)
{
    const Config& config = GlobalConfig::GetConfig();

    using test_args = tuple<vector<uint8_t>, vector<uint8_t>>;
    // clang-format off
    vector<test_args> test_data = 
    {
        {{OP_0}, {0x0, 0x1}},
        {{OP_1}, {0x0, 0x2}},
        {{OP_2}, {0x0, 0x4}},
        {{OP_8}, {0x1, 0x0}},
        {{OP_16}, {0x0, 0x0}},
        {{1, 0x7f}, {0x0, 0x0}},
        {{2, 0xff, 0x0}, {0x0, 0x0}},
        {{2, 0xff, 0x7f}, {0x0, 0x0}},
        {{3, 0xff, 0xff, 0x0}, {0x0, 0x0}},
        {{4, 0xff, 0xff, 0xff, 0x7f}, {0x0, 0x0}},
        {{5, 0xff, 0xff, 0xff, 0xff, 0x0}, {0x0, 0x0}},
        {{8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}, {0x0, 0x0}},
        {{9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0}, {0x0, 0x0}},
    };
    // clang-format on

    for(const auto& [n_shift, expected] : test_data)
    {
        vector<uint8_t> args{0x2, 0x0, 0x1}; // 0000 0000 0000 0001 <- bits to shift
        args.insert(args.end(), n_shift.begin(), n_shift.end());
        args.push_back(OP_LSHIFT);
        CScript script(args.begin(), args.end());

        const auto cancellation_source{task::CCancellationSource::Make()};
        const auto token{cancellation_source->GetToken()};
        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(config, false, token, stack, script,
                                       flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1U, stack.size());
        BOOST_CHECK_EQUAL(2U, stack.front().size());
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack.front()), end(stack.front()),
                                      begin(expected), end(expected));
    }
}

BOOST_AUTO_TEST_CASE(op_rshift)
{
    const Config& config = GlobalConfig::GetConfig();

    using test_args = tuple<vector<uint8_t>, vector<uint8_t>>;
    // clang-format off
    vector<test_args> test_data = 
    {
        {{OP_0}, {0x80, 0x0}},
        {{OP_1}, {0x40, 0x0}},
        {{OP_7}, {0x01, 0x0}},
        {{OP_8}, {0x0, 0x80}},
        {{OP_15}, {0x0, 0x1}},
        {{OP_16}, {0x0, 0x0}},
        {{1, 0x7f}, {0x0, 0x0}},
        {{2, 0xff, 0x0}, {0x0, 0x0}},
        {{2, 0xff, 0x7f}, {0x0, 0x0}},
        {{3, 0xff, 0xff, 0x0}, {0x0, 0x0}},
        {{4, 0xff, 0xff, 0xff, 0x7f}, {0x0, 0x0}},
        {{5, 0xff, 0xff, 0xff, 0xff, 0x0}, {0x0, 0x0}},
        {{8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}, {0x0, 0x0}},
        {{9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0}, {0x0, 0x0}},
    };
    // clang-format on

    for(const auto& [n_shift, expected] : test_data)
    {
        vector<uint8_t> args{0x2, 0x80, 0x0}; // 1000 0000 0000 0000 <- bits to shift
        args.insert(args.end(), n_shift.begin(), n_shift.end());
        args.push_back(OP_RSHIFT);
        CScript script(args.begin(), args.end());

        const auto cancellation_source{task::CCancellationSource::Make()};
        const auto token{cancellation_source->GetToken()};
        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(config, false, token, stack, script,
                                       flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1U, stack.size());
        BOOST_CHECK_EQUAL(2U, stack.front().size());
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack.front()), end(stack.front()),
                                      begin(expected), end(expected));
    }
}

BOOST_AUTO_TEST_CASE(op_rshift_far)
{
    constexpr vector<uint8_t>::size_type size{INT32_MAX / 8};
    std::vector<uint8_t> data(size + 1l, 0x0);
    data[0] = 0x80;

    auto source = task::CCancellationSource::Make();
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    ScriptError err;
    const auto r =
        EvalScript(GlobalConfig::GetConfig(), true, source->GetToken(), stack,
                   CScript() << INT32_MAX << OP_RSHIFT, flags,
                   BaseSignatureChecker{}, &err);
    BOOST_CHECK(r.value());
    BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, err);
    const auto& top = stack.front();
    const auto& values = top.GetElement();
    const auto it{find_if(begin(values), end(values),
                          [](const auto n) { return n != 0; })};
    BOOST_CHECK_EQUAL(distance(begin(values), it), static_cast<int64_t>(values.size() - 1));
    BOOST_CHECK_EQUAL(1, values[values.size() - 1]);
}

BOOST_AUTO_TEST_CASE(op_lshift_far)
{
    constexpr vector<uint8_t>::size_type size{INT32_MAX / 8};
    std::vector<uint8_t> data(size + 1l, 0x0);
    data[size] = 0x1;

    auto source = task::CCancellationSource::Make();
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    ScriptError err;
    const auto r =
        EvalScript(GlobalConfig::GetConfig(), true, source->GetToken(), stack,
                   CScript() << INT32_MAX << OP_LSHIFT, flags,
                   BaseSignatureChecker{}, &err);
    BOOST_CHECK(r.value());
    BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, err);
    const auto& top = stack.front();
    const auto& values = top.GetElement();
    const auto it{find_if(begin(values), end(values),
                          [](const auto n) { return n != 0; })};
    BOOST_CHECK_EQUAL(distance(begin(values), it), 0);
    BOOST_CHECK_EQUAL(0x80, values[0]);
}

namespace
{
    const vector<uint8_t> failure = {};
    const vector<uint8_t> success = {1};

    struct equality_checker : BaseSignatureChecker
    {
        bool CheckSig(const std::vector<uint8_t>& scriptsig,
                      const std::vector<uint8_t>& pubkey,
                      const CScript&,
                      bool enabledSighashForkid) const override
        {
            return scriptsig == pubkey;
        }

        bool CheckLockTime(const CScriptNum&) const override { return true; }
        bool CheckSequence(const CScriptNum&) const override { return true; }
    };
}

BOOST_AUTO_TEST_CASE(op_checksig)
{
    const Config& config = GlobalConfig::GetConfig();

    using test_args =
        tuple<opcodetype, opcodetype, bool, ScriptError, vector<uint8_t>>;
    // clang-format off
    vector<test_args> test_data = 
    {
        // signature, pub_key, exp_status, exp_error, 
        {OP_1, OP_1, true, SCRIPT_ERR_OK, success },
        {OP_1, OP_2, true, SCRIPT_ERR_OK, failure }
    };
    // clang-format on

    for(const auto& [signature, pub_key, exp_status, exp_error, exp_stack_top] :
        test_data)
    {
        vector<uint8_t> args;

        args.push_back(signature);
        args.push_back(pub_key);
        args.push_back(OP_CHECKSIG);

        const CScript script(args.begin(), args.end());

        uint32_t flags{};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const equality_checker checker;
        const auto status = EvalScript(
            config, false, task::CCancellationSource::Make()->GetToken(), stack,
            script, flags, checker, &error);

        BOOST_CHECK_EQUAL(exp_status, status.value());
        BOOST_CHECK_EQUAL(exp_error, error);
        BOOST_CHECK_EQUAL(1U, stack.size());
        const auto stack_0{stack.at(0)};
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack_0), end(stack_0),
                                      begin(exp_stack_top), end(exp_stack_top));
    }
}

BOOST_AUTO_TEST_CASE(op_checkmultisig)
{
    const Config& config = GlobalConfig::GetConfig();

    using test_args = tuple<int, vector<opcodetype>, int, vector<opcodetype>,
                            bool, ScriptError, vector<uint8_t>>;
    // clang-format off
    vector<test_args> test_data = 
    {
        // n_signatures, signatures, 
        // n_public_keys, public_keys, 
        // exp_status, exp_error, top_stack_value 

        // Success True
        {1, {OP_1}, 1, {OP_1}, true, SCRIPT_ERR_OK, success },
        {1, {OP_1}, 2, {OP_1, OP_16}, true, SCRIPT_ERR_OK, success },
        {1, {OP_1}, 2, {OP_16, OP_1}, true, SCRIPT_ERR_OK, success },

        {2, {OP_1, OP_2}, 2, {OP_1, OP_2}, true, SCRIPT_ERR_OK, success },

        {2, {OP_1, OP_2}, 3, {OP_16, OP_1, OP_2}, true, SCRIPT_ERR_OK, success},
        {2, {OP_1, OP_2}, 3, {OP_1, OP_16, OP_2}, true, SCRIPT_ERR_OK, success},
        {2, {OP_1, OP_2}, 3, {OP_1, OP_2, OP_16}, true, SCRIPT_ERR_OK, success},

        {2, {OP_1, OP_2}, 4, {OP_16, OP_1, OP_16, OP_2}, true, SCRIPT_ERR_OK, success},

        // Success false
        {1, {OP_1}, 1, {OP_16}, true, SCRIPT_ERR_OK, failure},

        {2, {OP_1, OP_2}, 2, {OP_1, OP_16}, true, SCRIPT_ERR_OK, failure},
        {2, {OP_1, OP_2}, 2, {OP_16, OP_2}, true, SCRIPT_ERR_OK, failure},
        {2, {OP_1, OP_2}, 2, {OP_2, OP_1}, true, SCRIPT_ERR_OK, failure},
        
        // Fails 
        {2, {OP_1, OP_2}, 1, {OP_1}, false, SCRIPT_ERR_SIG_COUNT, failure},
        {-1, {OP_1}, 1, {OP_1}, false, SCRIPT_ERR_SIG_COUNT, failure},
        {1, {OP_1}, -1, {OP_1}, false, SCRIPT_ERR_PUBKEY_COUNT, failure},
    };
    // clang-format on

    for(const auto& [n_sigs, signatures, n_pub_keys, public_keys, exp_status,
                     exp_error, exp_stack_top] : test_data)
    {
        vector<uint8_t> args{OP_0}; // historic bug start with OP_0

        reverse_copy(begin(signatures), end(signatures), back_inserter(args));
        args.push_back(1);
        args.push_back(n_sigs);

        reverse_copy(begin(public_keys), end(public_keys), back_inserter(args));
        args.push_back(1);
        args.push_back(n_pub_keys);

        args.push_back(OP_CHECKMULTISIG);

        const CScript script(args.begin(), args.end());

        uint32_t flags{};
        ScriptError error;
        LimitedStack stack(UINT32_MAX);
        const equality_checker checker;
        const auto status = EvalScript(
            config, false, task::CCancellationSource::Make()->GetToken(), stack,
            script, flags, checker, &error);

        BOOST_CHECK_EQUAL(exp_status, status.value());
        BOOST_CHECK_EQUAL(exp_error, error);
        BOOST_CHECK_EQUAL(
            exp_status ? 1 : signatures.size() + public_keys.size() + 3,
            stack.size());
        const auto stack_0{stack.at(0)};
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack_0), end(stack_0),
                                      begin(exp_stack_top), end(exp_stack_top));
    }
}

BOOST_AUTO_TEST_SUITE_END()

