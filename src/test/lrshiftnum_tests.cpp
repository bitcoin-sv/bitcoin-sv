// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "script/limitedstack.h"
#include "script/opcodes.h"
#include "script/script_error.h"
#include "script/script_flags.h"
#include "script/sign.h"
#include "taskcancellation.h"
#include "test/test_bitcoin.h"

#include <consensus/consensus.h>
#include <cstdint>

#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

BOOST_AUTO_TEST_SUITE(lrshiftnum_tests)

BOOST_AUTO_TEST_CASE(eval_script_op_lshiftnum)
{
    using namespace std;

    using test_args = tuple<uint32_t,                                        // flags
                            vector<uint8_t>,                                 // script
                            ScriptError,                                     // expected return
                            std::vector<std::vector<uint8_t>>>;              // expected stack
    const vector<test_args> test_data
    {
        // Pre-Chronicle
        {{},
         {OP_1, OP_NOP7},
         SCRIPT_ERR_OK,
         {{1}}},
        {SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS,
         {OP_1, OP_NOP7},
         SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS,
         {{1}}},

        // Post-Chronicle
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_LSHIFTNUM},
         SCRIPT_ERR_INVALID_STACK_OPERATION,
         {}},
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_LSHIFTNUM},
         SCRIPT_ERR_INVALID_STACK_OPERATION,
         {{1}}},
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_2, OP_1NEGATE, OP_LSHIFTNUM},
         SCRIPT_ERR_INVALID_NUMBER_RANGE,
         {{2}, {0x81}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {{}, OP_1, OP_LSHIFTNUM},  // empty input data
         SCRIPT_ERR_OK,
         {{}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_0, OP_LSHIFTNUM},  // shift by 0
         SCRIPT_ERR_OK,
         {{1}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_8, OP_LSHIFTNUM},  // shift by size of stack element (or greater)
         SCRIPT_ERR_OK,
         {{0, 1}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {2, 1, 2, OP_8, OP_LSHIFTNUM}, // shift 1 byte
         SCRIPT_ERR_OK,
         {{0, 1, 2}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {3, 1, 2, 3, OP_16, OP_LSHIFTNUM}, // shift 2 bytes
         SCRIPT_ERR_OK,
         {{0, 0, 1, 2, 3}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_1, OP_LSHIFTNUM}, // shift by 1 bit
         SCRIPT_ERR_OK,
         {{0x2}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_2, OP_LSHIFTNUM}, // shift by 2 bits
         SCRIPT_ERR_OK,
         {{4}}},

      {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
       {OP_1, OP_7, OP_LSHIFTNUM},
         SCRIPT_ERR_OK,
       {{0x80, 0x0}}}, // Note: Changes size of the data

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {2, 1, 2, OP_9, OP_LSHIFTNUM}, // shift by bits and bytes
         SCRIPT_ERR_OK,
         {{0, 2, 4}}},

      // Negative numbers
      {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
       {OP_1NEGATE, OP_7, OP_LSHIFTNUM}, // shift into the sign bit
         SCRIPT_ERR_OK,
       {{0x80, 0x80}}}, // Note: Changes size of the data

      {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
       {2, 0x1, 0x80, OP_7, OP_LSHIFTNUM}, // shift into the sign bit
       SCRIPT_ERR_OK,
       {{0x80, 0x80}}}, // Note: Changes size of the data
    };
    for(const auto& [flags, script, expected, exp_stack] : test_data)
    {
        const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                                  flags,
                                                  false)};
        auto source = task::CCancellationSource::Make();
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(params,
                                       source->GetToken(),
                                       stack,
                                       CScript{script.begin(), script.end()},
                                       flags,
                                       BaseSignatureChecker{});
        BOOST_REQUIRE(status);
        BOOST_CHECK_EQUAL(expected, status.value());
        BOOST_CHECK_EQUAL(exp_stack.size(), stack.size());
        if(expected == SCRIPT_ERR_OK)
        {
            // Validate all elements in the stack, not just the top
            for(size_t i = 0; i < exp_stack.size(); ++i)
            {
                const auto& actual_element = stack.at(i).GetElement();
                const auto& expected_element = exp_stack[i];
                BOOST_TEST(actual_element == expected_element, boost::test_tools::per_element());
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(eval_script_large_op_lshiftnum_1bit)
{
    using namespace std;

    constexpr int32_t flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                              flags,
                                              false)};
    auto source = task::CCancellationSource::Make();
    const auto data_size{params.MaxScriptNumLength()};
    const std::vector<uint8_t> data(data_size, 0x80);
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const vector<uint8_t> script{OP_1, OP_LSHIFTNUM};
    const auto status = EvalScript(params,
                                   source->GetToken(),
                                   stack,
                                   CScript{script.begin(), script.end()},
                                   flags,
                                   BaseSignatureChecker{});
    BOOST_CHECK(status);
    BOOST_CHECK_EQUAL(status.value(), SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(1, stack.size());
    const auto& e{stack.front().GetElement()};
    BOOST_CHECK_EQUAL(data_size, e.size());

    // Sample check throughout the data (checking every byte takes too long)
    BOOST_CHECK_EQUAL(0x00, e[0]);
    BOOST_CHECK_EQUAL(0x01, e[1]);
    BOOST_CHECK_EQUAL(0x01, e[size_t(data_size) / 4]);
    BOOST_CHECK_EQUAL(0x01, e[size_t(data_size) / 2]);
    BOOST_CHECK_EQUAL(0x01, e[size_t(data_size) * 3 / 4]);
    BOOST_CHECK_EQUAL(0x01, e.at(e.size()-2));
    BOOST_CHECK_EQUAL(0x81, e.at(e.size()-1));
}

static constexpr std::array<uint8_t, 5> to_array(const int n)
{
    constexpr auto N{sizeof(int)};
    static_assert(N == 4);
    std::array<uint8_t, N + 1> result{};
    result[0] = N;
    result[1] = n & 0xff;
    result[2] = (n >> 8) & 0xff;
    result[3] = (n >> 16) & 0xff;
    result[4] = (n >> 24) & 0xff;
    return result;
}
static_assert(std::array<uint8_t, 5>{4, 0x04, 0x03, 0x02, 0x01} == to_array(0x0102'0304));
static_assert(std::array<uint8_t, 5>{4, 0x78, 0x56, 0x34, 0x12} == to_array(0x1234'5678));

BOOST_AUTO_TEST_CASE(eval_script_op_lshiftnum_max_script_len)
{
    using namespace std;

    constexpr int32_t flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    auto config = GlobalConfig::GetConfig().GetConfigScriptPolicy();
    constexpr auto shift_bytes{MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE};
    config.SetMaxScriptNumLengthPolicy(shift_bytes);
    const auto params{make_eval_script_params(config,
                                              flags,
                                              false)};
    auto source = task::CCancellationSource::Make();
    const std::vector<uint8_t> data{1};
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const vector<uint8_t> script{[](const auto bit_shift)
                                 {
                                     const auto a{to_array(bit_shift)};
                                     vector<uint8_t> v{a.begin(), a.end()};
                                     v.push_back(OP_LSHIFTNUM);
                                     return v;
                                 }((shift_bytes - 1) * 8)};
    const auto status = EvalScript(params,
                                   source->GetToken(),
                                   stack,
                                   CScript{script.begin(), script.end()},
                                   flags,
                                   BaseSignatureChecker{});
    BOOST_CHECK(status);
    BOOST_CHECK_EQUAL(status.value(), SCRIPT_ERR_OK);

    // Sample check throughout the data (checking every byte takes too long)
    BOOST_CHECK_EQUAL(1, stack.size());
    const auto& e{stack.front().GetElement()};
    BOOST_CHECK_EQUAL(shift_bytes, e.size());
    BOOST_CHECK_EQUAL(0, e[0]);
    BOOST_CHECK_EQUAL(0, e[1]);
    BOOST_CHECK_EQUAL(0, e[e.size() / 4]);
    BOOST_CHECK_EQUAL(0, e[e.size() / 2]);
    BOOST_CHECK_EQUAL(0, e[e.size() * 3 / 4]);
    BOOST_CHECK_EQUAL(0, e[e.size()-2]);
    BOOST_CHECK_EQUAL(1, e[e.size()-1]);
}

BOOST_AUTO_TEST_CASE(eval_script_op_lshiftnum_gt_max_script_len)
{
    using namespace std;

    constexpr int32_t flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    auto config = GlobalConfig::GetConfig().GetConfigScriptPolicy();
    constexpr auto data_size{MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE};
    config.SetMaxScriptNumLengthPolicy(data_size);
    const auto params{make_eval_script_params(config,
                                              flags,
                                              false)};
    auto source = task::CCancellationSource::Make();
    const std::vector<uint8_t> data{1};
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const vector<uint8_t> script{[](const auto bit_shift)
                                 {
                                     const auto a{to_array(bit_shift)};
                                     vector<uint8_t> v{a.begin(), a.end()};
                                     v.push_back(OP_LSHIFTNUM);
                                     return v;
                                 }(data_size * 8)};
    const auto status = EvalScript(params,
                                   source->GetToken(),
                                   stack,
                                   CScript{script.begin(), script.end()},
                                   flags,
                                   BaseSignatureChecker{});
    BOOST_CHECK(status);
    BOOST_CHECK_EQUAL(status.value(), SCRIPT_ERR_SCRIPTNUM_OVERFLOW);

    BOOST_CHECK_EQUAL(1, stack.size());
    const auto& e{stack.front().GetElement()};
    BOOST_CHECK_EQUAL(1, e.size());
    BOOST_CHECK_EQUAL(1, e[0]);
}

BOOST_AUTO_TEST_CASE(eval_script_op_rshiftnum)
{
    using namespace std;

    using test_args = tuple<uint32_t,                                        // flags
                            vector<uint8_t>,                                 // script
                            ScriptError,                                     // expected return
                            std::vector<std::vector<uint8_t>>>;              // expected stack
    const vector<test_args> test_data
    {
        // Pre-Chronicle
        {{},
         {OP_1, OP_NOP7},
         SCRIPT_ERR_OK,
         {{1}}},
        {SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS,
         {OP_1, OP_NOP7},
         SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS,
         {{1}}},

        // Post-Chronicle
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_RSHIFTNUM},
         SCRIPT_ERR_INVALID_STACK_OPERATION,
         {}},
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_RSHIFTNUM},
         SCRIPT_ERR_INVALID_STACK_OPERATION,
         {{1}}},
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_2, OP_1NEGATE, OP_RSHIFTNUM},
         SCRIPT_ERR_INVALID_NUMBER_RANGE,
         {{2}, {0x81}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {{}, OP_1, OP_RSHIFTNUM},  // empty input data
         SCRIPT_ERR_OK,
         {{}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_0, OP_RSHIFTNUM},  // shift by 0
         SCRIPT_ERR_OK,
         {{1}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {2, 1, 2, OP_8, OP_RSHIFTNUM}, // shift 1 byte
         SCRIPT_ERR_OK,
         {{2}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {3, 1, 2, 3, OP_16, OP_RSHIFTNUM}, // shift 2 bytes
         SCRIPT_ERR_OK,
         {{3}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_2, OP_1, OP_RSHIFTNUM}, // shift by 1 bit
         SCRIPT_ERR_OK,
         {{0x1}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_4, OP_2, OP_RSHIFTNUM}, // shift by 2 bits
         SCRIPT_ERR_OK,
         {{1}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {2, 0, 1, OP_1, OP_RSHIFTNUM}, // 256 >> 1 -> 128
         SCRIPT_ERR_OK,
         {{0x80, 0x00}}},
  
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {2, 0, 0x81, OP_1, OP_RSHIFTNUM}, // -256 >> 1 -> -128
         SCRIPT_ERR_OK,
         {{0x80, 0x80}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {3, 1, 2, 4, OP_9, OP_RSHIFTNUM}, // shift by bits and bytes
         SCRIPT_ERR_OK,
         {{1, 2}}},

        // round to 0
        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_1, OP_RSHIFTNUM}, // 1 >> 1 -> 0
         SCRIPT_ERR_OK,
         {{}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1NEGATE, OP_1, OP_RSHIFTNUM}, // -1 >> 1 -> 0
         SCRIPT_ERR_OK,
         {{}}}, // Note: Changes size of the data
    };
    for(const auto& [flags, script, expected, exp_stack] : test_data)
    {
        const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                                  flags,
                                                  false)};
        auto source = task::CCancellationSource::Make();
        LimitedStack stack(UINT32_MAX);
        const auto status = EvalScript(params,
                                       source->GetToken(),
                                       stack,
                                       CScript{script.begin(), script.end()},
                                       flags,
                                       BaseSignatureChecker{});
        BOOST_REQUIRE(status);
        BOOST_CHECK_EQUAL(expected, status.value());
        BOOST_CHECK_EQUAL(exp_stack.size(), stack.size());
        if(expected == SCRIPT_ERR_OK)
        {
            // Validate all elements in the stack, not just the top
            for(size_t i = 0; i < exp_stack.size(); ++i)
            {
                const auto& actual_element = stack.at(i).GetElement();
                const auto& expected_element = exp_stack[i];
                BOOST_TEST(actual_element == expected_element, boost::test_tools::per_element());
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(eval_script_large_op_rshiftnum_1bit)
{
    using namespace std;

    constexpr int32_t flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                              flags,
                                              false)};
    auto source = task::CCancellationSource::Make();
    const auto data_size{params.MaxScriptNumLength()};
    const std::vector<uint8_t> data(data_size, 0x80);
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const vector<uint8_t> script{OP_1, OP_RSHIFTNUM};
    const auto status = EvalScript(params,
                                   source->GetToken(),
                                   stack,
                                   CScript{script.begin(), script.end()},
                                   flags,
                                   BaseSignatureChecker{});
    BOOST_CHECK(status);
    BOOST_CHECK_EQUAL(status.value(), SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(1, stack.size());
    const auto& e{stack.front().GetElement()};
    BOOST_CHECK_EQUAL(data_size - 1, e.size());

    // Sample check throughout the data (checking every byte takes too long)
    BOOST_CHECK_EQUAL(0x40, e[0]);
    BOOST_CHECK_EQUAL(0x40, e[1]);
    BOOST_CHECK_EQUAL(0x40, e[size_t(data_size) / 4]);
    BOOST_CHECK_EQUAL(0x40, e[size_t(data_size) / 2]);
    BOOST_CHECK_EQUAL(0x40, e[size_t(data_size) * 3 / 4]);
    BOOST_CHECK_EQUAL(0x40, e.at(e.size()-2));
    BOOST_CHECK_EQUAL(0xc0, e.at(e.size()-1));
}

BOOST_AUTO_TEST_SUITE_END()
