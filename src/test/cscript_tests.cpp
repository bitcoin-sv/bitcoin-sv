// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <array>
#include "script/script.h"
#include "script_macros.h"

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

BOOST_AUTO_TEST_CASE(OpCount_tests)
{
    uint8_t a[] = {OP_1, OP_2, OP_2};
    BOOST_CHECK_EQUAL(0U, CountOp(a, OP_0));
    BOOST_CHECK_EQUAL(1U, CountOp(a, OP_1));
    BOOST_CHECK_EQUAL(2U, CountOp(a, OP_2));

    array<uint8_t, 3> arr;
    copy(begin(a), end(a), begin(arr));
    BOOST_CHECK_EQUAL(0U, CountOp(arr, OP_0));
    BOOST_CHECK_EQUAL(1U, CountOp(arr, OP_1));
    BOOST_CHECK_EQUAL(2U, CountOp(arr, OP_2));

    vector<uint8_t> v{begin(arr), end(arr)};
    BOOST_CHECK_EQUAL(0U, CountOp(v, OP_0));
    BOOST_CHECK_EQUAL(1U, CountOp(v, OP_1));
    BOOST_CHECK_EQUAL(2U, CountOp(v, OP_2));

    CScript script{begin(v), end(v)};
    BOOST_CHECK_EQUAL(0U, CountOp(script, OP_0));
    BOOST_CHECK_EQUAL(1U, CountOp(script, OP_1));
    BOOST_CHECK_EQUAL(2U, CountOp(script, OP_2));
}

BOOST_AUTO_TEST_CASE(GetSigOpCount)
{
    // input script, accurate, genesis_enabled, expected_count, expected_error
    using test_data_type = tuple< vector<uint8_t>, bool, bool, uint64_t, bool >;
    vector<test_data_type> test_data {
        { {}, false, false, 0, false },
        { {}, false, true, 0, false },
        { {}, true, false, 0, false },
        { {}, true, true, 0, false },
        
        { {OP_1}, false, false, 0, false },
        { {OP_1}, false, true, 0, false },
        { {OP_1}, true, false, 0, false },
        { {OP_1}, true, true, 0, false },
        
        { {OP_CHECKSIG}, false, false, 1, false },
        { {OP_CHECKSIG}, false, true, 1, false },
        { {OP_CHECKSIG}, true, false, 1, false },
        { {OP_CHECKSIG}, true, true, 1, false },
        
        { {OP_CHECKSIG, OP_CHECKSIG}, false, false, 2, false },
        { {OP_CHECKSIG, OP_CHECKSIG}, false, true, 2, false },
        { {OP_CHECKSIG, OP_CHECKSIG}, true, false, 2, false },
        { {OP_CHECKSIG, OP_CHECKSIG}, true, true, 2, false },
        
        { {OP_CHECKMULTISIG}, false, false, 20, false },
        { {OP_CHECKMULTISIG}, false, true, 0, false },
        { {OP_CHECKMULTISIG}, true, false, 20, false },
        { {OP_CHECKMULTISIG}, true, true, 0, false },
        
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, false, false, 40, false },
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, false, true, 0, false },
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, true, false, 40, false },
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, true, true, 0, false },

        { {MULTISIG_LOCKING_2}, false, false, 20, false }, 
        { {MULTISIG_LOCKING_2}, false, true, 2, false }, 
        { {MULTISIG_LOCKING_2}, true, false, 2, false }, 
        { {MULTISIG_LOCKING_2}, true, true, 2, false }, 
        
        { {MULTISIG_LOCKING_32}, false, false, 20, false },
        { {MULTISIG_LOCKING_32}, false, true, 32, false },
        { {MULTISIG_LOCKING_32}, true, false, 20, false },
        { {MULTISIG_LOCKING_32}, true, true, 32, false },
       
        { {MULTISIG_2_IF_LOCKING}, false, false, 21, false }, 
        { {MULTISIG_2_IF_LOCKING}, false, true, 3, false }, 
        { {MULTISIG_2_IF_LOCKING}, true, false, 3, false }, 
        { {MULTISIG_2_IF_LOCKING}, true, true, 3, false }, 
        
        { {P2SH_LOCKING}, true, true, 0, false }, 
    };
    for(const auto& [ip, accurate, genesis_enabled, exp_n, exp_error] : test_data)
    {
        const CScript script(begin(ip), end(ip));
        bool error{false};
        const auto n = script.GetSigOpCount(accurate, genesis_enabled, error);
        BOOST_CHECK_EQUAL(exp_n, n);
        BOOST_CHECK_EQUAL(exp_error, error);
    }
}

BOOST_AUTO_TEST_CASE(GetSigOpCount_p2sh)
{
    // input script, genesis_enabled, expected_count, expected_error
    using test_data_type = tuple< vector<uint8_t>, bool, uint64_t, bool >;
    vector<test_data_type> test_data {
        { {71, MULTISIG_LOCKING_2}, false, 2, false }, 
        { {71, MULTISIG_LOCKING_2}, true, 0, false }, 
        
        { {OP_PUSHDATA1, 139, MULTISIG_LOCKING_4}, false, 4, false }, 
        { {OP_PUSHDATA1, 139, MULTISIG_LOCKING_4}, true, 0, false }, 
        
        { {OP_PUSHDATA2, 0x13, 0x1, MULTISIG_LOCKING_8}, false, 8, false }, 
        { {OP_PUSHDATA2, 0x13, 0x1, MULTISIG_LOCKING_8}, true, 0, false }, 
        
        { {OP_PUSHDATA2, 0x23, 0x2, MULTISIG_LOCKING_16}, false, 16, false }, 
        { {OP_PUSHDATA2, 0x23, 0x2, MULTISIG_LOCKING_16}, true, 0, false }, 
       
        // Note: MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS = 20
        { {OP_PUSHDATA2, 0xac, 0x2, MULTISIG_LOCKING_20}, false, 20, false }, 
        { {OP_PUSHDATA2, 0xac, 0x2, MULTISIG_LOCKING_20}, true, 0, false }, 
        
        { {OP_PUSHDATA2, 0xce, 0x2, MULTISIG_LOCKING_21}, false, 20, false }, 
        { {OP_PUSHDATA2, 0xce, 0x2, MULTISIG_LOCKING_21}, true, 0, false }, 

        { {OP_PUSHDATA2, 0x44, 0x4, MULTISIG_LOCKING_32}, false, 20, false }, 
        { {OP_PUSHDATA2, 0x44, 0x4, MULTISIG_LOCKING_32}, true, 0, false }, 
 
        { {74, MULTISIG_2_IF_LOCKING}, false, 3, false }, 
        { {74, MULTISIG_2_IF_LOCKING}, true, 0, false }, 
    };
    vector<uint8_t> v{P2SH_LOCKING};
    const CScript p2sh_script(begin(v), end(v));
    for(const auto& [ip, genesis_enabled, exp_n, exp_error] : test_data)
    {
        const CScript redeem_script{begin(ip), end(ip)};
        bool error{false};
        const auto n = p2sh_script.GetSigOpCount(redeem_script, genesis_enabled, error);
        BOOST_CHECK_EQUAL(exp_n, n);
        BOOST_CHECK_EQUAL(exp_error, error);
    }
}

BOOST_AUTO_TEST_SUITE_END()

