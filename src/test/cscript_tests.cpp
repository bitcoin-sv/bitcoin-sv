// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/script.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_AUTO_TEST_SUITE(cscript_tests)

BOOST_AUTO_TEST_CASE(GetOp2)
{
    // input script, expected status, expected opcode, expected output script] 
    using test_data_type = tuple< vector<uint8_t>, bool, opcodetype, vector<uint8_t> >;

    vector<test_data_type> test_data {
        { {OP_0}, true, static_cast<opcodetype>(0), {} }, // Note: OP_0 = 0
        { {1, 1}, true, static_cast<opcodetype>(1), {1} },
        { {2, 1, 2}, true, static_cast<opcodetype>(2), {1, 2} },
        { {3, 1, 2, 3}, true, static_cast<opcodetype>(3), {1, 2, 3} },

        { {OP_PUSHDATA1, 3, 1, 2, 3}, true, OP_PUSHDATA1, {1, 2, 3} },
        { {OP_PUSHDATA2, 3, 0, 1, 2, 3}, true, OP_PUSHDATA2, {1, 2, 3} },
        { {OP_PUSHDATA4, 3, 0, 0, 0, 1, 2, 3}, true, OP_PUSHDATA4, {1, 2, 3} },
        
        { {OP_1}, true, OP_1, {} },
        { {OP_2}, true, OP_2, {} },
        
        { {OP_1, 42}, true, OP_1, {} },
        
        { {OP_INVALIDOPCODE}, true, OP_INVALIDOPCODE, {}},

        
        { {}, false, OP_INVALIDOPCODE, {}},
        { {1}, false, OP_INVALIDOPCODE, {} },
        { {2}, false, OP_INVALIDOPCODE, {} },
        { {2, 1}, false, OP_INVALIDOPCODE, {} },
        { {0x4b, 1}, false, OP_INVALIDOPCODE, {} },
        
        { {OP_PUSHDATA1 }, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA1, 1}, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA2 }, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA2, 1}, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA2, 0, 1}, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA4 }, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA4, 1}, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA4, 0, 1}, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA4, 0, 0, 1}, false, OP_INVALIDOPCODE, {} },
        { {OP_PUSHDATA4, 0, 0, 0, 1}, false, OP_INVALIDOPCODE, {} },
    };
    for(const auto& [ip, exp_status, exp_opcode, exp_v] : test_data)
    {
        const CScript script(begin(ip), end(ip));
        auto it{script.begin()};
        opcodetype opcode;
        vector<uint8_t> v;
        const auto s = script.GetOp2(it, opcode, &v);
        BOOST_CHECK_EQUAL(exp_status, s);
        BOOST_CHECK_EQUAL(exp_opcode, opcode);
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(exp_v), end(exp_v), begin(v),
                                      end(v));
    }
}

BOOST_AUTO_TEST_SUITE_END()

