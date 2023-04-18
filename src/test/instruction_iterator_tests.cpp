// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/instruction_iterator.h"

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

using namespace std;
using namespace bsv;

BOOST_AUTO_TEST_SUITE(instruction_iterator_tests)

BOOST_AUTO_TEST_CASE(decode_instruction_tests)
{
    // input script, expected values: status, opcode, offset, length]
    using test_data_type = tuple< vector<uint8_t>, bool, int, size_t, size_t>;

    vector<test_data_type> test_data {
        { {}, false, OP_INVALIDOPCODE, 0, 0 }, 

        { {0}, true, OP_0, 0, 0 }, 
        
        { {1 }, false, OP_INVALIDOPCODE, 0, 0 },
        { {1, 42}, true, 1, 0, 1 },
        
        { {2 }, false, OP_INVALIDOPCODE, 0, 0 },
        { {2, 42}, false, OP_INVALIDOPCODE, 0, 0 },
        { {2, 42, 42}, true, 2, 0, 2 },

        // ...

        { {75}, false, OP_INVALIDOPCODE, 0, 0 },
        { {75, 1}, false, OP_INVALIDOPCODE, 0, 0 },
        { {75, 1, 2}, false, OP_INVALIDOPCODE, 0, 0 },
        // ...
        
        { {OP_PUSHDATA1 }, false, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA1, 1 }, false, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA1, 2, 42 }, false, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA1, 0 }, true, OP_PUSHDATA1, 1, 0 },
        { {OP_PUSHDATA1, 1, 42 }, true, OP_PUSHDATA1, 1, 1 },
        { {OP_PUSHDATA1, 2, 42, 42 }, true, OP_PUSHDATA1, 1, 2 },


        { {OP_PUSHDATA2 }, false, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA2, 1 }, false, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA2, 1, 0}, false, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA2, 2, 0, 42}, false, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA2, 0, 0}, true, OP_PUSHDATA2, 2, 0 },
        { {OP_PUSHDATA2, 1, 0, 42}, true, OP_PUSHDATA2, 2, 1 },
        { {OP_PUSHDATA2, 2, 0, 42, 42}, true, OP_PUSHDATA2, 2, 2 },


        { {OP_PUSHDATA4 }, false, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 1 }, false, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 1, 1 }, false, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 1, 1, 1 }, false, OP_INVALIDOPCODE, 0, 0 },
        
        { {OP_PUSHDATA4, 1, 0, 0, 0}, false, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 2, 0, 0, 0, 42}, false, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA4, 0, 0, 0, 0}, true, OP_PUSHDATA4, 4, 0},
        { {OP_PUSHDATA4, 1, 0, 0, 0, 42}, true, OP_PUSHDATA4, 4, 1},
        { {OP_PUSHDATA4, 2, 0, 0, 0, 42, 42}, true, OP_PUSHDATA4, 4, 2},
        
        { {OP_0}, true, OP_0, 0, 0 }, // Note: OP_0 = 0
        { {OP_1}, true, OP_1, 0, 0 },
        { {OP_2}, true, OP_2, 0, 0 },
        { {OP_3}, true, OP_3, 0, 0 },
        { {OP_4}, true, OP_4, 0, 0 },
        { {OP_5}, true, OP_5, 0, 0 },
        { {OP_6}, true, OP_6, 0, 0 },
        { {OP_7}, true, OP_7, 0, 0 },
        { {OP_8}, true, OP_8, 0, 0 },
        { {OP_9}, true, OP_9, 0, 0 },
        { {OP_10}, true, OP_10, 0, 0 },
        { {OP_11}, true, OP_11, 0, 0 },
        { {OP_12}, true, OP_12, 0, 0 },
        { {OP_13}, true, OP_13, 0, 0 },
        { {OP_14}, true, OP_14, 0, 0 },
        { {OP_15}, true, OP_15, 0, 0 },
        { {OP_16}, true, OP_16, 0, 0 },
        
        { {OP_NOP}, true, OP_NOP, 0, 0 },
        // ...
        { {OP_PUBKEY}, true, OP_PUBKEY, 0, 0 },
        { {OP_INVALIDOPCODE}, true, OP_INVALIDOPCODE, 0, 0 } 
    };
    for(const auto& [ip, exp_status, exp_opcode, exp_offset, exp_len] : test_data)
    {
        const CScript script(begin(ip), end(ip));
        const auto [status, opcode, offset, len] = decode_instruction(
            std::span<const uint8_t>{script.data(), script.size()});
        BOOST_CHECK_EQUAL(exp_status, status);
        BOOST_CHECK_EQUAL(exp_opcode, opcode);
        BOOST_CHECK_EQUAL(exp_offset, static_cast<unsigned char>(offset));
        BOOST_CHECK_EQUAL(exp_len, len);
    }
}

BOOST_AUTO_TEST_CASE(instruction_iterator_happy_case)
{
    vector<uint8_t> ip = {
                            0,
                            1, 42,
                            2, 42, 42,
                            OP_1,
                            OP_16,
                            OP_PUSHDATA1, 1, 42,
                            OP_PUSHDATA2, 1, 0, 42,
                            OP_PUSHDATA4, 1, 0, 0, 0, 42,
                            OP_DUP
                         };
    vector<instruction> expected{ 
                                    {static_cast<opcodetype>(0), 0, ip.data() + 1, 0}, 
                                    {static_cast<opcodetype>(1), 0, ip.data() + 2, 1},
                                    {static_cast<opcodetype>(2), 0, ip.data() + 4, 2},
                                    {OP_1, 0, ip.data() + 7, 0},
                                    {OP_16, 0, ip.data() + 8, 0},
                                    {OP_PUSHDATA1, 1, ip.data() + 10, 1},
                                    {OP_PUSHDATA2, 2, ip.data() + 14, 1},
                                    {OP_PUSHDATA4, 4, ip.data() + 20, 1},
                                    {OP_DUP, 0, ip.data() + 22, 0}
                                };

    instruction_iterator it_begin{std::span{ip.data(), ip.size()}};
    instruction_iterator it_end{std::span{ip.data() + ip.size(), 0}};

    // check op++
    auto n = std::distance(it_begin, it_end);
    BOOST_CHECK_EQUAL(expected.size(), static_cast<unsigned>(n));

    for(size_t i{0}; i < expected.size(); ++i, ++it_begin)
    {
        // check op* and op==
        BOOST_CHECK_EQUAL(expected[i], *it_begin);

        // check op->
        BOOST_CHECK_EQUAL(expected[i].opcode(), it_begin->opcode());
        BOOST_CHECK_EQUAL(expected[i].operand().size(),
                          it_begin->operand().size());
        BOOST_CHECK_EQUAL(expected[i].operand().data(),
                          it_begin->operand().data());
    }
}

BOOST_AUTO_TEST_CASE(too_short_single_instruction)
{
    vector<uint8_t> ip = {
                            OP_PUSHDATA4, 1, 0, 0, 0, //42, <- not enough data
                         };

    instruction_iterator it_begin{std::span{ip.data(), ip.size()}};
    instruction_iterator it_end{std::span{ip.data() + ip.size(), 0}};

    // check op++
    auto n = std::distance(it_begin, it_end);
    BOOST_CHECK_EQUAL(1, n);

    const instruction expected{OP_INVALIDOPCODE, 0, ip.data() + 1, 0};
    BOOST_CHECK_EQUAL(expected, *it_begin);
}

BOOST_AUTO_TEST_CASE(too_short_two_instructions)
{
    vector<uint8_t> ip = {
                            OP_PUSHDATA4, 1, 0, 0, 0, 42, 
                            OP_PUSHDATA4, 1, 0, 0, 0, //42, <- not enough data
                         };
    vector<instruction> expected{ 
                                    {OP_PUSHDATA4, 0, ip.data() + 5, 1},
                                    {OP_INVALIDOPCODE, 0, ip.data() + 7, 0},
                                };

    instruction_iterator it_begin{std::span{ip.data(), ip.size()}};
    instruction_iterator it_end{std::span{ip.data() + ip.size(), 0}};

    // check op++
    auto n = std::distance(it_begin, it_end);
    BOOST_CHECK_EQUAL(expected.size(), static_cast<unsigned>(n));

    for(size_t i{0}; i < expected.size(); ++i, ++it_begin)
    {
        BOOST_CHECK_EQUAL(expected[i], *it_begin);

        BOOST_CHECK_EQUAL(expected[i].opcode(), it_begin->opcode());
        BOOST_CHECK_EQUAL(expected[i].operand().size(),
                          it_begin->operand().size());
        BOOST_CHECK_EQUAL(expected[i].operand().data(),
                          it_begin->operand().data());
    }
}

BOOST_AUTO_TEST_SUITE_END()
