// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/instruction_iterator.h"

#include <boost/test/unit_test.hpp>

#include <vector>

using namespace std;
using namespace bsv;

BOOST_AUTO_TEST_SUITE(instruction_iterator_tests)

BOOST_AUTO_TEST_CASE(decode_instruction_tests)
{
    // input script, expected opcode, expected offset, expected length] : test_data)
    using test_data_type = tuple< vector<uint8_t>, int, size_t, size_t>;

    vector<test_data_type> test_data {
        { {}, OP_INVALIDOPCODE, 0, 0 }, 

        { {0}, OP_0, 0, 0 }, 
        
        { {1 }, OP_INVALIDOPCODE, 0, 0 },
        { {1, 42}, 1, 0, 1 },
        
        { {2 }, OP_INVALIDOPCODE, 0, 0 },
        { {2, 42}, OP_INVALIDOPCODE, 0, 0 },
        { {2, 42, 42}, 2, 0, 2 },

        // ...

        { {75}, OP_INVALIDOPCODE, 0, 0 },
        { {75, 1}, OP_INVALIDOPCODE, 0, 0 },
        { {75, 1, 2}, OP_INVALIDOPCODE, 0, 0 },
        // ...
        
        { {OP_PUSHDATA1 }, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA1, 1 }, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA1, 2, 42 }, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA1, 0 }, OP_PUSHDATA1, 1, 0 },
        { {OP_PUSHDATA1, 1, 42 }, OP_PUSHDATA1, 1, 1 },
        { {OP_PUSHDATA1, 2, 42, 42 }, OP_PUSHDATA1, 1, 2 },


        { {OP_PUSHDATA2 }, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA2, 1 }, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA2, 1, 0}, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA2, 2, 0, 42}, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA2, 0, 0}, OP_PUSHDATA2, 2, 0 },
        { {OP_PUSHDATA2, 1, 0, 42}, OP_PUSHDATA2, 2, 1 },
        { {OP_PUSHDATA2, 2, 0, 42, 42}, OP_PUSHDATA2, 2, 2 },


        { {OP_PUSHDATA4 }, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 1 }, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 1, 1 }, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 1, 1, 1 }, OP_INVALIDOPCODE, 0, 0 },
        
        { {OP_PUSHDATA4, 1, 0, 0, 0}, OP_INVALIDOPCODE, 0, 0 },
        { {OP_PUSHDATA4, 2, 0, 0, 0, 42}, OP_INVALIDOPCODE, 0, 0 },

        { {OP_PUSHDATA4, 0, 0, 0, 0}, OP_PUSHDATA4, 4, 0},
        { {OP_PUSHDATA4, 1, 0, 0, 0, 42}, OP_PUSHDATA4, 4, 1},
        { {OP_PUSHDATA4, 2, 0, 0, 0, 42, 42}, OP_PUSHDATA4, 4, 2},
        
        { {OP_0}, OP_0, 0, 0 }, // Note: OP_0 = 0
        { {OP_1}, OP_1, 0, 0 },
        { {OP_2}, OP_2, 0, 0 },
        { {OP_3}, OP_3, 0, 0 },
        { {OP_4}, OP_4, 0, 0 },
        { {OP_5}, OP_5, 0, 0 },
        { {OP_6}, OP_6, 0, 0 },
        { {OP_7}, OP_7, 0, 0 },
        { {OP_8}, OP_8, 0, 0 },
        { {OP_9}, OP_9, 0, 0 },
        { {OP_10}, OP_10, 0, 0 },
        { {OP_11}, OP_11, 0, 0 },
        { {OP_12}, OP_12, 0, 0 },
        { {OP_13}, OP_13, 0, 0 },
        { {OP_14}, OP_14, 0, 0 },
        { {OP_15}, OP_15, 0, 0 },
        { {OP_16}, OP_16, 0, 0 },
        
        { {OP_NOP}, OP_NOP, 0, 0 },
        // ...
        { {OP_PUBKEY}, OP_PUBKEY, 0, 0 },
        { {OP_INVALIDOPCODE}, OP_INVALIDOPCODE, 0, 0 } 
    };
    for(const auto& [ip, exp_opcode, exp_offset, exp_len] : test_data)
    {
        const CScript script(begin(ip), end(ip));
        const auto [opcode, offset, len] = decode_instruction(
            span<const uint8_t>{script.data(), script.size()});
        BOOST_CHECK_EQUAL(exp_opcode, opcode);
        BOOST_CHECK_EQUAL(exp_offset, offset);
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

    instruction_iterator it_begin{span{ip.data(), ip.size()}};
    instruction_iterator it_end{span{ip.data() + ip.size(), 0}};

    // check op++
    auto n = std::distance(it_begin, it_end);
    BOOST_CHECK_EQUAL(expected.size(), n);

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

    instruction_iterator it_begin{span{ip.data(), ip.size()}};
    instruction_iterator it_end{span{ip.data() + ip.size(), 0}};

    // check op++
    auto n = std::distance(it_begin, it_end);
    BOOST_CHECK_EQUAL(0, n);

    const instruction expected{OP_INVALIDOPCODE, 0, ip.data() + ip.size(), 0};
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
                                    {OP_INVALIDOPCODE, 0, ip.data() + ip.size(), 0},
                                };

    instruction_iterator it_begin{span{ip.data(), ip.size()}};
    instruction_iterator it_end{span{ip.data() + ip.size(), 0}};

    // check op++
    auto n = std::distance(it_begin, it_end);
    BOOST_CHECK_EQUAL(expected.size() - 1, n);

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
