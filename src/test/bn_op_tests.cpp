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

#include <vector>

using namespace std;

using frame_type = vector<uint8_t>;
using stack_type = vector<frame_type>;
    
using bsv::bint;

constexpr auto min64{std::numeric_limits<int64_t>::min()+1};
constexpr auto max64{std::numeric_limits<int64_t>::max()};

BOOST_AUTO_TEST_SUITE(bn_op_tests)

BOOST_AUTO_TEST_CASE(bint_bint_op)
{
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

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        const auto status =
            EvalScript(
                source->GetToken(),
                stack,
                script,
                flags,
                BaseSignatureChecker{},
                &error);
        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1, stack.size());
        const auto frame = stack[0];
        const auto actual =
            frame.empty() ? bint{0}
                          : bsv::deserialize<bint>(begin(frame), end(frame));
        bint expected = polynomial_value(begin(exp_poly), end(exp_poly), bn);
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(bint_bint_numequalverify)
{
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

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        const auto status =
            EvalScript(
                source->GetToken(),
                stack,
                script,
                flags,
                BaseSignatureChecker{},
                &error);
        if(status.value())
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
                    ? bint{0}
                    : bsv::deserialize<bint>(begin(frame), end(frame));
            bint expected =
                polynomial_value(begin(exp_poly), end(exp_poly), bn);
            BOOST_CHECK_EQUAL(expected, actual);
        }
    }
}

BOOST_AUTO_TEST_CASE(operands_too_large)
{
    using test_args =
        tuple<int, int, opcodetype, bool, ScriptError>;
    const auto max_arg_len{500};
    // clang-format off
    vector<test_args> test_data = {
    {max_arg_len,   max_arg_len,   OP_ADD, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_ADD, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_ADD, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_ADD, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_SUB, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_SUB, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_SUB, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_SUB, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_MUL, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_MUL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_MUL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_MUL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_DIV, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_DIV, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_DIV, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_DIV, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_MOD, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_MOD, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_MOD, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_MOD, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_BOOLAND, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_BOOLAND, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_BOOLAND, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_BOOLAND, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_BOOLOR, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_BOOLOR, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_BOOLOR, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_BOOLOR, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_NUMEQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_NUMEQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_NUMEQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_NUMEQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_NUMNOTEQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_NUMNOTEQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_NUMNOTEQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_NUMNOTEQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_LESSTHAN, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_LESSTHAN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_LESSTHAN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_LESSTHAN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_LESSTHANOREQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_LESSTHANOREQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_LESSTHANOREQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_LESSTHANOREQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_GREATERTHAN, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_GREATERTHAN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_GREATERTHAN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_GREATERTHAN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_GREATERTHANOREQUAL, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_GREATERTHANOREQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_GREATERTHANOREQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_GREATERTHANOREQUAL, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_MIN, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_MIN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_MIN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_MIN, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len,   OP_MAX, true,  SCRIPT_ERR_OK},
    {max_arg_len+1, max_arg_len,   OP_MAX, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len,   max_arg_len+1, OP_MAX, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    {max_arg_len+1, max_arg_len+1, OP_MAX, false, SCRIPT_ERR_INVALID_OPERAND_SIZE},
    };
    // clang-format on

    for(const auto [arg0_size, arg1_size, op_code, exp_status, exp_script_error] : test_data)
    {
        stack_type stack;

        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA2);

        vector tmp0(arg0_size, 42);
        const bint arg0{bsv::deserialize<bint>(tmp0.begin(), tmp0.end())};

        args.push_back(arg0_size & 0xff);
        args.push_back((arg0_size / 256) & 0xff);
        bsv::serialize(arg0, back_inserter(args));

        args.push_back(OP_PUSHDATA2);

        vector tmp1(arg1_size, 69);
        const bint arg1{bsv::deserialize<bint>(tmp1.begin(), tmp1.end())};
        args.push_back(arg1_size & 0xff);
        args.push_back((arg1_size / 256) & 0xff);
        // args.push_back(arg1.size_bytes());
        bsv::serialize(arg1, back_inserter(args));
        
        args.push_back(op_code);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        auto source = task::CCancellationSource::Make();
        const auto status =
            EvalScript(
                source->GetToken(),
                stack,
                script,
                flags,
                BaseSignatureChecker{},
                &error);
        BOOST_CHECK_EQUAL(exp_status, status.value());
        BOOST_CHECK_EQUAL(exp_script_error, error);
        BOOST_CHECK_EQUAL(status.value() ? 1:2, stack.size());
    }
}

BOOST_AUTO_TEST_CASE(op_bin2num)
{
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
        stack_type stack;
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        args.push_back(ip.size());
        copy(begin(ip), end(ip), back_inserter(args));

        args.push_back(OP_BIN2NUM);

        CScript script(args.begin(), args.end());

        const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
        ScriptError error;
        const auto status =
            EvalScript(
                task::CCancellationSource::Make()->GetToken(),
                stack, script, flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(true, status.value());
        BOOST_CHECK_EQUAL(SCRIPT_ERR_OK, error);
        BOOST_CHECK_EQUAL(1, stack.size());
        BOOST_CHECK_EQUAL(op.size(), stack[0].size());
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack[0]), end(stack[0]), begin(op),
                                      end(op));
    }
}

BOOST_AUTO_TEST_CASE(op_num2bin)
{
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
        stack_type stack;
        vector<uint8_t> args;

        args.push_back(OP_PUSHDATA1);
        args.push_back(arg1.size());
        copy(begin(arg1), end(arg1), back_inserter(args));
        
        args.push_back(OP_PUSHDATA1);
        args.push_back(arg2.size());
        copy(begin(arg2), end(arg2), back_inserter(args));

        args.push_back(OP_NUM2BIN);

        CScript script(args.begin(), args.end());

        uint32_t flags(1 << 17);
        ScriptError error;
        const auto status =
            EvalScript(
                task::CCancellationSource::Make()->GetToken(),
                stack, script, flags, BaseSignatureChecker{}, &error);

        BOOST_CHECK_EQUAL(exp_status, status.value());
        BOOST_CHECK_EQUAL(exp_error, error);
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(stack[0]), end(stack[0]), begin(op),
                                      end(op));
    }
}

BOOST_AUTO_TEST_SUITE_END()
