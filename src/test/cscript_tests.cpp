// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/script.h"

#include "protocol_era.h"
#include "script_macros.h"

#include <boost/test/unit_test.hpp>

#include <array>

using namespace std;

BOOST_AUTO_TEST_SUITE(cscript_tests)

BOOST_AUTO_TEST_CASE(GetOp2)
{
    // input script, expected status, expected opcode, expected output script] 
    using test_data_type = tuple< vector<uint8_t>, bool, opcodetype, vector<uint8_t> >;

    vector<test_data_type> test_data {
        { {OP_0}, true, static_cast<opcodetype>(0), {} }, // Note: OP_0 = 0
        { {1, 1}, true, static_cast<opcodetype>(1), {1} }, // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        { {2, 1, 2}, true, static_cast<opcodetype>(2), {1, 2} }, // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
        { {3, 1, 2, 3}, true, static_cast<opcodetype>(3), {1, 2, 3} }, // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

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
        opcodetype opcode; // NOLINT(cppcoreguidelines-init-variables)
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
    uint8_t a[] = {OP_1, OP_2, OP_2}; // NOLINT(cppcoreguidelines-avoid-c-arrays)
    BOOST_CHECK_EQUAL(0U, CountOp(a, OP_0));
    BOOST_CHECK_EQUAL(1U, CountOp(a, OP_1));
    BOOST_CHECK_EQUAL(2U, CountOp(a, OP_2));

    array<uint8_t, 3> arr; // NOLINT(cppcoreguidelines-pro-type-member-init)
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
    // input script, accurate, protocol, expected_count, expected_error
    using test_data_type = tuple< vector<uint8_t>, bool, ProtocolEra, uint64_t, bool >;
    vector<test_data_type> test_data {
        { {}, false, ProtocolEra::PreGenesis, 0, false },
        { {}, false, ProtocolEra::PostGenesis, 0, false },
        { {}, true, ProtocolEra::PreGenesis, 0, false },
        { {}, true, ProtocolEra::PostGenesis, 0, false },
        
        { {OP_1}, false, ProtocolEra::PreGenesis, 0, false },
        { {OP_1}, false, ProtocolEra::PostGenesis, 0, false },
        { {OP_1}, true, ProtocolEra::PreGenesis, 0, false },
        { {OP_1}, true, ProtocolEra::PostGenesis, 0, false },
        
        { {OP_CHECKSIG}, false, ProtocolEra::PreGenesis, 1, false },
        { {OP_CHECKSIG}, false, ProtocolEra::PostGenesis, 1, false },
        { {OP_CHECKSIG}, true, ProtocolEra::PreGenesis, 1, false },
        { {OP_CHECKSIG}, true, ProtocolEra::PostGenesis, 1, false },
        
        { {OP_CHECKSIG, OP_CHECKSIG}, false, ProtocolEra::PreGenesis, 2, false },
        { {OP_CHECKSIG, OP_CHECKSIG}, false, ProtocolEra::PostGenesis, 2, false },
        { {OP_CHECKSIG, OP_CHECKSIG}, true, ProtocolEra::PreGenesis, 2, false },
        { {OP_CHECKSIG, OP_CHECKSIG}, true, ProtocolEra::PostGenesis, 2, false },
        
        { {OP_CHECKMULTISIG}, false, ProtocolEra::PreGenesis, 20, false },
        { {OP_CHECKMULTISIG}, false, ProtocolEra::PostGenesis, 0, false },
        { {OP_CHECKMULTISIG}, true, ProtocolEra::PreGenesis, 20, false },
        { {OP_CHECKMULTISIG}, true, ProtocolEra::PostGenesis, 0, false },
        
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, false, ProtocolEra::PreGenesis, 40, false },
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, false, ProtocolEra::PostGenesis, 0, false },
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, true, ProtocolEra::PreGenesis, 40, false },
        { {OP_CHECKMULTISIG, OP_CHECKMULTISIG}, true, ProtocolEra::PostGenesis, 0, false },

        { {MULTISIG_LOCKING_2}, false, ProtocolEra::PreGenesis, 20, false }, 
        { {MULTISIG_LOCKING_2}, false, ProtocolEra::PostGenesis, 2, false }, 
        { {MULTISIG_LOCKING_2}, true, ProtocolEra::PreGenesis, 2, false }, 
        { {MULTISIG_LOCKING_2}, true, ProtocolEra::PostGenesis, 2, false }, 
        
        { {MULTISIG_LOCKING_32}, false, ProtocolEra::PreGenesis, 20, false },
        { {MULTISIG_LOCKING_32}, false, ProtocolEra::PostGenesis, 32, false },
        { {MULTISIG_LOCKING_32}, true, ProtocolEra::PreGenesis, 20, false },
        { {MULTISIG_LOCKING_32}, true, ProtocolEra::PostGenesis, 32, false },
       
        { {MULTISIG_2_IF_LOCKING}, false, ProtocolEra::PreGenesis, 21, false }, 
        { {MULTISIG_2_IF_LOCKING}, false, ProtocolEra::PostGenesis, 3, false }, 
        { {MULTISIG_2_IF_LOCKING}, true, ProtocolEra::PreGenesis, 3, false }, 
        { {MULTISIG_2_IF_LOCKING}, true, ProtocolEra::PostGenesis, 3, false }, 
        
        { {P2SH_LOCKING}, true, ProtocolEra::PostGenesis, 0, false }, 
    };
    for(const auto& [ip, accurate, protocol, exp_n, exp_error] : test_data)
    {
        const CScript script(begin(ip), end(ip));
        bool error{false};
        const auto n = script.GetSigOpCount(accurate, protocol, error);
        BOOST_CHECK_EQUAL(exp_n, n);
        BOOST_CHECK_EQUAL(exp_error, error);
    }
}

BOOST_AUTO_TEST_CASE(GetSigOpCount_p2sh)
{
    // input script, protocol, expected_count, expected_error
    using test_data_type = tuple< vector<uint8_t>, ProtocolEra, uint64_t, bool >;
    vector<test_data_type> test_data {
        { {71, MULTISIG_LOCKING_2}, ProtocolEra::PreGenesis, 2, false }, 
        { {71, MULTISIG_LOCKING_2}, ProtocolEra::PostGenesis, 0, false }, 
        
        { {OP_PUSHDATA1, 139, MULTISIG_LOCKING_4}, ProtocolEra::PreGenesis, 4, false }, 
        { {OP_PUSHDATA1, 139, MULTISIG_LOCKING_4}, ProtocolEra::PostGenesis, 0, false }, 
        
        { {OP_PUSHDATA2, 0x13, 0x1, MULTISIG_LOCKING_8}, ProtocolEra::PreGenesis, 8, false }, 
        { {OP_PUSHDATA2, 0x13, 0x1, MULTISIG_LOCKING_8}, ProtocolEra::PostGenesis, 0, false }, 
        
        { {OP_PUSHDATA2, 0x23, 0x2, MULTISIG_LOCKING_16}, ProtocolEra::PreGenesis, 16, false }, 
        { {OP_PUSHDATA2, 0x23, 0x2, MULTISIG_LOCKING_16}, ProtocolEra::PostGenesis, 0, false }, 
       
        // Note: MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS = 20
        { {OP_PUSHDATA2, 0xac, 0x2, MULTISIG_LOCKING_20}, ProtocolEra::PreGenesis, 20, false }, 
        { {OP_PUSHDATA2, 0xac, 0x2, MULTISIG_LOCKING_20}, ProtocolEra::PostGenesis, 0, false }, 
        
        { {OP_PUSHDATA2, 0xce, 0x2, MULTISIG_LOCKING_21}, ProtocolEra::PreGenesis, 20, false }, 
        { {OP_PUSHDATA2, 0xce, 0x2, MULTISIG_LOCKING_21}, ProtocolEra::PostGenesis, 0, false }, 

        { {OP_PUSHDATA2, 0x44, 0x4, MULTISIG_LOCKING_32}, ProtocolEra::PreGenesis, 20, false }, 
        { {OP_PUSHDATA2, 0x44, 0x4, MULTISIG_LOCKING_32}, ProtocolEra::PostGenesis, 0, false }, 
 
        { {74, MULTISIG_2_IF_LOCKING}, ProtocolEra::PreGenesis, 3, false }, 
        { {74, MULTISIG_2_IF_LOCKING}, ProtocolEra::PostGenesis, 0, false }, 
    };
    vector<uint8_t> v{P2SH_LOCKING};
    const CScript p2sh_script(begin(v), end(v));
    for(const auto& [ip, protocol, exp_n, exp_error] : test_data)
    {
        const CScript redeem_script{begin(ip), end(ip)};
        bool error{false};
        const auto n = p2sh_script.GetSigOpCount(redeem_script, protocol, error);
        BOOST_CHECK_EQUAL(exp_n, n);
        BOOST_CHECK_EQUAL(exp_error, error);
    }
}

BOOST_AUTO_TEST_SUITE_END()

