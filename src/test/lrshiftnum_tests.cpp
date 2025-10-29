// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "overload.h"
#include "script/limitedstack.h"
#include "script/opcodes.h"
#include "script/script_error.h"
#include "script/script_flags.h"
#include "script/script_num.h"
#include "script/shiftnum.h"
#include "script/sign.h"
#include "taskcancellation.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>
#include <iterator>

BOOST_AUTO_TEST_SUITE(lrshiftnum_tests)

BOOST_AUTO_TEST_CASE(copy_lshift_tests)
{
    using namespace std;
    using test_args = tuple<vector<uint8_t>,                   // data
                            vector<uint8_t>::difference_type,  // byte shift
                            vector<uint8_t>::difference_type,  // bit shift
                            vector<uint8_t>>;                  // expected data 
    const vector<test_args> test_data
    {
        // input validation
        {{}, 0, 1, {}},     // empty input range

        // byte shifts only
        {{1}, 1, 0, {}},

        {{1, 2}, 1, 0, {2}},  // copy-shift by 1 byte

        {{1, 2, 4}, 0, 0, {1, 2, 4}},
        {{1, 2, 4}, 1, 0, {2, 4}},
        {{1, 2, 4}, 2, 0, {4}}, // copy-shift by 1 byte

        // bit shifts only
        {{1}, 0, 1, {2}},
        {{1}, 0, 2, {0x04}},
        {{1}, 0, 7, {0x80}},

        {{0x00, 0xff}, 0, 1, {0x01, 0xfe}},
        {{0x00, 0xff}, 0, 2, {0x03, 0xfc}},
        {{0x00, 0xff}, 0, 3, {0x07, 0xf8}},
        {{0x00, 0xff}, 0, 4, {0x0f, 0xf0}},
        {{0x00, 0xff}, 0, 5, {0x1f, 0xe0}},
        {{0x00, 0xff}, 0, 6, {0x3f, 0xc0}},
        {{0x00, 0xff}, 0, 7, {0x7f, 0x80}},

        {{0xf0, 0xf1, 0x01}, 0, 1, {0xe1, 0xe2, 0x02}},
        {{0xf0, 0xf1, 0x01}, 0, 2, {0xc3, 0xc4, 0x04}},
        {{0xf0, 0xf1, 0x01}, 0, 3, {0x87, 0x88, 0x08}},
        {{0xf0, 0xf1, 0x01}, 0, 4, {0x0f, 0x10, 0x10}},
        {{0xf0, 0xf1, 0x01}, 0, 5, {0x1e, 0x20, 0x20}},
        {{0xf0, 0xf1, 0x01}, 0, 6, {0x3c, 0x40, 0x40}},
        {{0xf0, 0xf1, 0x01}, 0, 7, {0x78, 0x80, 0x80}},

        // shift by bytes and bits
        {{0xf0, 0x01, 0xff, 0x01}, 1, 1, {0x03, 0xfe, 0x02}},
        {{0xf0, 0x01, 0xff, 0x01}, 1, 7, {0xff, 0x80, 0x80}},
        {{0xf0, 0x01, 0x02, 0xff}, 2, 1, {0x05, 0xfe}},
    };
    for(const auto& [data, byte_shift, bit_shift, exp_data] : test_data)
    {
        const auto exp_dist{ exp_data.empty() ? 0 
                                              : bit_shift % bits_per_byte ? exp_data.size() - 1
                                                                          : exp_data.size()};
        vector<uint8_t> actual(data.size(), 42);
        const auto it = copy_lshift(data.begin() + byte_shift, data.end(),
                                    bit_shift,
                                    actual.begin());
        BOOST_CHECK(std::next(actual.begin(), exp_dist) == it);
        BOOST_CHECK_EQUAL_COLLECTIONS(exp_data.begin(), exp_data.begin() + exp_dist,
                                      actual.begin(), actual.begin() + exp_dist);
    }
}

BOOST_AUTO_TEST_CASE(copy_lshift_in_place_tests)
{
    using namespace std;

    // Test in-place shifts (input and output are the same buffer)
    using test_args = tuple<vector<uint8_t>,                   // data (will be modified in-place)
                            vector<uint8_t>::difference_type,  // bit shift
                            vector<uint8_t>>;                  // expected data
    const vector<test_args> test_data
    {
        // In-place bit shifts with byte_shift == 0
        {{1, 2}, 0, {1, 2}},           // No shift
        {{1, 2}, 1, {2, 4}},           // 1-bit left shift
        {{1, 2}, 2, {4, 8}},           // 2-bit left shift
        {{0xff, 0x01}, 1, {0xfe, 0x03}},  // Multi-byte with carry
        {{0xf0, 0xf1, 0x01}, 1, {0xe1, 0xe2, 0x02}},  // 3-byte shift
        {{0xf0, 0xf1, 0x01}, 4, {0x0f, 0x10, 0x10}},  // 4-bit shift
    };

    for(const auto& [initial_data, bit_shift, exp_data] : test_data)
    {
        vector<uint8_t> data = initial_data;  // Copy for in-place modification
        const auto exp_dist = bit_shift % bits_per_byte ? exp_data.size() - 1
                                                         : exp_data.size();

        const auto it = copy_lshift(data.begin(), data.end(),
                                    bit_shift,
                                    data.begin());

        BOOST_CHECK(std::next(data.begin(), exp_dist) == it);
        BOOST_CHECK_EQUAL_COLLECTIONS(exp_data.begin(), exp_data.begin() + exp_dist,
                                      data.begin(), data.begin() + exp_dist);
    }
}

BOOST_AUTO_TEST_CASE(lshiftnum_tests)
{
    using namespace std;

    using test_args = tuple<vector<uint8_t>,    // data
                            vector<uint8_t>,    // shift bits
                            bool,               // expected return
                            vector<uint8_t>>;   // expected data
    const vector<test_args> test_data
    {
        {{1, 2},
         {8}, // Shift 1 byte
         false,
         {2, 0}},

        {{1, 2, 3},
         {16}, // Shift 2 bytes
         false,
         {3, 0, 0}},

        // Negative numbers
        {{0x80}, // -0 -> -0 Don't shift negative bit
         {1},
         false,
         {0x80}},
        
        {{0x81}, // -1 -> -2 Don't shift negative bit
         {1},
         false,
         {0x82}},

        {{0x81},
         {2}, // shift by 2 bits
         false,
         {0x84}},
        
        {{0x81},
         {8}, // Shift 1 byte
         false,
         {0x80}},

        {{0x40}, // +64 -> +0 Don't shift into negative bit
         {1},
         false,
         {0x0}},
        
        {{0x1, 0x81}, // -257 -> -514 don't shift negative bit
         {1},
         false,
         {0x2, 0x82}},

        // Shift by bits and bytes
        {{1, 2},
         {9}, // 1 byte, 1 bit
         false,
         {4, 0}},

        {{0x81, 0x02, 0x04, 0x81, 0x02, 0x04, 0x81, 0x02, 0x04}, // 0x81, 2, 4 = chunk
         {1}, // Shift 1 bit
         false,
         {2, 4, 9, 2, 4, 9, 2, 4, 8}},
        
        {{0x81, 0x02, 0x04, 0x81, 0x02, 0x04, 0x81, 0x02, 0x04}, // 0x81, 2, 4 = chunk
         {2}, // Shift by 2 bits
         false,
         {0x04, 0x08, 0x12, 0x04, 0x08, 0x12, 0x04, 0x08, 0x10}},
        
        {{1, 2, 4, 1, 2, 4, 1, 2, 4}, // 1, 2, 4 = chunk
         {8}, // Shift by 1 byte
         false,
         {2, 4, 1, 2, 4, 1, 2, 4, 0}},
       
        {{1, 1, 1, 2, 2, 2, 4, 4, 4}, // first chunk = 1, middle = 2, last = 4
         {9}, // Shift by 1 byte, 1 bit
         false,
         {2, 2, 4, 4, 4, 8, 8, 8, 0}},
        
        {{1, 1, 1, 2}, // Partial chunk
         {9}, // Shift by 1 byte, 1 bit
         false,
         {2, 2, 4, 0}},

        {{1, 1, 1, 2, 2}, // Partial chunk
         {9}, // Shift by 1 byte, 1 bit
         false,
         {2, 2, 4, 4, 0}},
    };
    for(const auto& [ip, sb, exp_status, exp_data] : test_data)
    {
        auto source = task::CCancellationSource::Make();
        CScriptNum num{sb,
                       min_encoding_check::no,
                       CScriptNum::MAXIMUM_ELEMENT_SIZE,
                       true};
        vector<uint8_t> op(ip.size(), 42);
        constexpr int32_t max_chunk_size{3}; // first, middle, last bytes
        const bool status{lshiftnum(source->GetToken(),
                                    num,
                                    ip,
                                    op,
                                    max_chunk_size)};
        BOOST_CHECK_EQUAL(exp_status, status);
        BOOST_CHECK_EQUAL_COLLECTIONS(op.begin(), op.end(),
                                      exp_data.begin(), exp_data.end());
    }
}

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
         {OP_1, OP_0, OP_LSHIFTNUM},  // Shift by 0
         SCRIPT_ERR_OK,
         {{1}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_8, OP_LSHIFTNUM},  // Shift by size of stack element (or greater)
         SCRIPT_ERR_OK,
         {{0}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {2, 1, 2, OP_8, OP_LSHIFTNUM}, // Shift 1 byte
         SCRIPT_ERR_OK,
         {{2, 0}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {3, 1, 2, 3, OP_16, OP_LSHIFTNUM}, // Shift 2 bytes
         SCRIPT_ERR_OK,
         {{3, 0, 0}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_1, OP_LSHIFTNUM}, // shift by 1 bit
         SCRIPT_ERR_OK,
         {{0x2}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {OP_1, OP_2, OP_LSHIFTNUM}, // shift by 2 bits
         SCRIPT_ERR_OK,
         {{4}}},

        {SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE,
         {2, 1, 2, OP_9, OP_LSHIFTNUM}, // Shift by bits and bytes
         SCRIPT_ERR_OK,
         {{4, 0}}},
    };
    for(const auto& [flags, script, expected, exp_stack] : test_data)
    {
        const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
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
            for (size_t i = 0; i < exp_stack.size(); ++i)
            {
                const auto& actual_element = stack.at(i).GetElement();
                const auto& expected_element = exp_stack[i];
                BOOST_CHECK_EQUAL_COLLECTIONS(actual_element.begin(), actual_element.end(),
                                              expected_element.begin(), expected_element.end());
            }
        }
    }
}

// Stack element size > int32_t::max() / 8
// shift_bits == 1
BOOST_AUTO_TEST_CASE(eval_script_large_op_lshiftnum_1bit)
{
    using namespace std;

    constexpr int32_t flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};

    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    auto source = task::CCancellationSource::Make();
    constexpr auto data_size{1'000'000'000};
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
    BOOST_CHECK_EQUAL(0x01, e[0]);
    BOOST_CHECK_EQUAL(0x01, e[1]);
    BOOST_CHECK_EQUAL(0x01, e[size_t(data_size) / 4]);
    BOOST_CHECK_EQUAL(0x01, e[size_t(data_size) / 2]);
    BOOST_CHECK_EQUAL(0x01, e[size_t(data_size) * 3 / 4]);
    BOOST_CHECK_EQUAL(0x00, e.at(e.size()-2));
    BOOST_CHECK_EQUAL(0x80, e.at(e.size()-1));
}

// Stack element size > int32_t::max() / 8
// shift_bits == (Stack element size * bits_per_byte) - 1
BOOST_AUTO_TEST_CASE(eval_script_large_op_lshiftnum_size_minus_1bit)
{
    using namespace std;

    constexpr int32_t flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    auto source = task::CCancellationSource::Make();
    constexpr auto data_size{1'000'000'000};
    const std::vector<uint8_t> data(data_size, 1);
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const vector<uint8_t> script{5, 0xff, 0x4f, 0xd6, 0xdc, 0x01, OP_LSHIFTNUM}; // data size * 8 - 1
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
    BOOST_CHECK_EQUAL(0x80, e[0]);
    BOOST_CHECK_EQUAL(0, e[1]);
    BOOST_CHECK_EQUAL(0, e[size_t(data_size) / 4]);
    BOOST_CHECK_EQUAL(0, e[size_t(data_size) / 2]);
    BOOST_CHECK_EQUAL(0, e[size_t(data_size) * 3 / 4]);
    BOOST_CHECK_EQUAL(0, e.at(e.size()-2));
    BOOST_CHECK_EQUAL(0, e.at(e.size()-1));
}

BOOST_AUTO_TEST_CASE(lshiftnum_cancellation_before_operation)
{
    using namespace std;

    auto source = task::CCancellationSource::Make();
    constexpr auto data_size{100'000'000};
    const vector<uint8_t> data(data_size, 0xFF);
    vector<uint8_t> output(data_size, 0);

    source->Cancel();
    const bool cancelled{lshiftnum(source->GetToken(),
                                   CScriptNum{bsv::bint{1}},
                                   data,
                                   output)};
    BOOST_CHECK(cancelled);
}

BOOST_AUTO_TEST_CASE(lshiftnum_cancellation_not_triggered)
{
    using namespace std;

    auto source = task::CCancellationSource::Make();
    const vector<uint8_t> data{0x01, 0x02, 0x03};
    vector<uint8_t> output(3, 0);

    const bool cancelled{lshiftnum(source->GetToken(),
                                   CScriptNum{bsv::bint{1}},
                                   data,
                                   output)};
    BOOST_CHECK(!cancelled);
    BOOST_CHECK_EQUAL(0x02, output[0]);
    BOOST_CHECK_EQUAL(0x04, output[1]);
    BOOST_CHECK_EQUAL(0x06, output[2]);
}

BOOST_AUTO_TEST_CASE(extremely_large_shift_positive_data)
{
    using namespace std;

    auto source = task::CCancellationSource::Make();
    const vector<uint8_t> data{0x01, 0x02, 0x03, 0x04};
    vector<uint8_t> output(4, 0xFF);

    const bsv::bint huge_shift{std::numeric_limits<int64_t>::max()};
    const CScriptNum shift_amount{huge_shift + bsv::bint{1000}};

    const bool cancelled{lshiftnum(source->GetToken(), shift_amount, data, output)};
    BOOST_CHECK(!cancelled);
    BOOST_CHECK_EQUAL(0x00, output[0]);
    BOOST_CHECK_EQUAL(0x00, output[1]);
    BOOST_CHECK_EQUAL(0x00, output[2]);
    BOOST_CHECK_EQUAL(0x00, output[3]);
}

BOOST_AUTO_TEST_CASE(extremely_large_shift_negative_data)
{
    using namespace std;

    auto source = task::CCancellationSource::Make();
    const vector<uint8_t> data{0x01, 0x02, 0x03, 0x84};
    vector<uint8_t> output(4, 0xFF);

    const bsv::bint huge_shift{std::numeric_limits<int64_t>::max()};
    const CScriptNum shift_amount{huge_shift + bsv::bint{1000}};

    const bool cancelled{lshiftnum(source->GetToken(), shift_amount, data, output)};
    BOOST_CHECK(!cancelled);
    BOOST_CHECK_EQUAL(0x00, output[0]);
    BOOST_CHECK_EQUAL(0x00, output[1]);
    BOOST_CHECK_EQUAL(0x00, output[2]);
    BOOST_CHECK_EQUAL(0x80, output[3]);
}

BOOST_AUTO_TEST_SUITE_END()
