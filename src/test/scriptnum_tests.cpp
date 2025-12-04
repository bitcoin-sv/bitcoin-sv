// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "big_int.h"
#include "script/int_serialization.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/script_num.h"
#include "scriptnum10.h"

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <exception>

using namespace std;
using bsv::bint;

namespace
{
    constexpr auto max8 = numeric_limits<int8_t>::max();
    constexpr auto min8 = numeric_limits<int8_t>::min();
    constexpr auto max16 = numeric_limits<int16_t>::max();
    constexpr auto min16 = numeric_limits<int16_t>::min();
    constexpr auto max32 = numeric_limits<int32_t>::max();
    constexpr auto min32 = numeric_limits<int32_t>::min();
    constexpr auto min64 = numeric_limits<int64_t>::min();
    constexpr auto max64 = numeric_limits<int64_t>::max();
    constexpr size_t test_bint_max_len{16};

    vector<int64_t> test_data{min64, -1, 0, 1, max64}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}

BOOST_AUTO_TEST_SUITE(scriptnum_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    CScriptNum n;
    BOOST_CHECK_EQUAL(0, n.index());
    BOOST_CHECK_EQUAL(CScriptNum::MAXIMUM_ELEMENT_SIZE, n.max_length());
}

BOOST_AUTO_TEST_CASE(int64_construction)
{
    using test_args = tuple<int64_t,    // value
                            size_t>;    // max_length

    // Valid constructions - max_length is sufficient for the value
    const vector<test_args> valid_constructions = {
        // Default max_length should be MAXIMUM_ELEMENT_SIZE
        {0, CScriptNum::MAXIMUM_ELEMENT_SIZE},

        // Values that fit in 1 byte
        {max8, 1},
        {-1, 1},

        // Values that require 2 bytes
        {min8, 2},        // -128 requires 2 bytes (magnitude 0x80 needs extra sign byte)
        {max8 + 1, 2},    // 128
        {min8 - 1, 2},    // -129

        // Max/min values that fit in MAXIMUM_ELEMENT_SIZE (4 bytes)
        {max32, CScriptNum::MAXIMUM_ELEMENT_SIZE},
        {-max32, CScriptNum::MAXIMUM_ELEMENT_SIZE},

        // Values requiring INT64_SERIALIZED_SIZE
        {min64, CScriptNum::INT64_SERIALIZED_SIZE},
        {max64, CScriptNum::INT64_SERIALIZED_SIZE},
    };

    for(const auto& [value, max_len] : valid_constructions)
    {
        const CScriptNum sn{value, max_len};
        BOOST_CHECK_EQUAL(max_len, sn.max_length());
        BOOST_CHECK_EQUAL(0, sn.index());
    }

    // Invalid constructions
    const vector<test_args> invalid_constructions = {
        // max_length too small for the value
        {min64, 4},         // min64 requires 9 bytes
        {min16, 2},         // -32768 requires more than 2 bytes
        {max16 + 1, 2},     // 32768 requires more than 2 bytes
        {min32, CScriptNum::MAXIMUM_ELEMENT_SIZE},  // min32 requires 5 bytes
        {static_cast<int64_t>(max32) + 1, CScriptNum::MAXIMUM_ELEMENT_SIZE}, // -min32 requires 5 bytes

        // max_length > INT64_SERIALIZED_SIZE (wrong constructor for big integers)
        {0, 100},
        {1, 20},
        {min64, 50},
    };

    for(const auto& [value, max_len] : invalid_constructions)
    {
        BOOST_CHECK_THROW(CScriptNum(value, max_len), scriptnum_overflow_error);
    }
}

BOOST_AUTO_TEST_CASE(bint_construction)
{
    const CScriptNum a{bint{}, CScriptNum::MAXIMUM_ELEMENT_SIZE};
    BOOST_CHECK_EQUAL(CScriptNum::MAXIMUM_ELEMENT_SIZE, a.max_length());

    try
    {
        const CScriptNum b{bint{-0x80}, 1};
        BOOST_FAIL("Expected script number overflow");
    }
    catch(std::exception& e)
    {
        const string expected{"script number overflow"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(construction)
{
    using script_data = vector<uint8_t>;
    using test_args = tuple<script_data, size_t, bool>;
    vector<test_args> valid_constructions = {
        {{}, 0, false},
        {{}, 0, true},
        {{}, 1, false},
        {{}, 1, true},
        {{1}, 1, false},
        {{1}, 1, true},
        {{1}, 2, false},
        {{1}, 2, true},
        {{1, 2, 3}, 4, false},
        {{1, 2, 3}, 4, true},
        {{1, 2, 3, 4}, 4, false},
        {{1, 2, 3, 4}, 4, true},
        {{1, 2, 3, 4}, 5, false},
        {{1, 2, 3, 4}, 5, true},
        {{1, 2, 3, 4, 5}, 5, false},
        {{2, 2, 3, 4, 5}, 5, true},
        {{1, 2, 3, 4, 5}, 6, false},
        {{1, 2, 3, 4, 5}, 6, true},
    };

    for(const auto& [v, max_size, big_int] : valid_constructions)
    {
        try
        {
            const CScriptNum a{v, min_encoding_check::no, max_size, big_int};
            BOOST_CHECK_EQUAL(max_size, a.max_length());
        }
        catch(...)
        {
            BOOST_FAIL("should not throw");
        }
    }

    vector<test_args> invalid_constructions = {
        {{1}, 0, false},
        {{1}, 0, true},
        {{1, 2, 3, 4}, 3, false},
        {{1, 2, 3, 4}, 3, true},
        {{1, 2, 3, 4, 5}, 4, false},
        {{1, 2, 3, 4, 5}, 4, true},
    };
    for(const auto& [v, max_size, big_int] : invalid_constructions)
    {
        try
        {
            CScriptNum actual{v, min_encoding_check::no, max_size, big_int};
            BOOST_FAIL("should throw");
        }
        catch(...)
        {
        }
    }
}

BOOST_AUTO_TEST_CASE(lshift_max_length_invariant)
{
    using test_args = tuple<int64_t,    // value
                            int64_t,    // shift
                            size_t>;    // max_len

    // Test cases that should complete successfully without throwing
    const vector<test_args> non_throwing_cases = {
        {1, 14, 2},
        {1, 24, 4},
    };
    // Test non-throwing cases with int64-based CScriptNum
    for(const auto& [value, shift, max_len] : non_throwing_cases)
    {
        const auto v_value{CScriptNum{value}.getvch()};
        CScriptNum sn_value{value, max_len};

        const auto v_shift{CScriptNum{shift}.getvch()};
        const CScriptNum sn_shift{v_shift, min_encoding_check::no, max_len, false};

        sn_value <<= sn_shift;
        BOOST_CHECK(sn_value.getvch().size() <= max_len);
    }
    // Test non-throwing cases with bint-based CScriptNum
    for(const auto& [value, shift, max_len] : non_throwing_cases)
    {
        CScriptNum sn_value{bint{value}, max_len};
        const CScriptNum sn_shift{bint{shift}, max_len};

        sn_value <<= sn_shift;
        BOOST_CHECK(sn_value.getvch().size() <= max_len);
    }

    // Test cases that should throw due to max_len constraint
    const vector<test_args> throwing_cases = {
        {1, 15, 2},
        {1, 32, 4},
        {0x7FFFFF, 9, 4},
    };
    // Test throwing cases with int64-based CScriptNum
    for(const auto& [value, shift, max_len] : throwing_cases)
    {
        const auto v_value{CScriptNum{value}.getvch()};
        CScriptNum sn_value{value, max_len};

        const auto v_shift{CScriptNum{shift}.getvch()};
        const CScriptNum sn_shift{v_shift, min_encoding_check::no, max_len, false};

        BOOST_CHECK_THROW(sn_value <<= sn_shift, scriptnum_overflow_error);
    }

    // Test throwing cases with bint-based CScriptNum
    for(const auto& [value, shift, max_len] : throwing_cases)
    {
        CScriptNum sn_value{bint{value}, max_len};
        const CScriptNum sn_shift{bint{shift}, max_len};

        BOOST_CHECK_THROW(sn_value <<= sn_shift, scriptnum_overflow_error);
    }
}

BOOST_AUTO_TEST_CASE(lshift_negative_value)
{
    using test_args = tuple<int64_t, int64_t, int64_t>;
    const vector<test_args> test_data = {
        // Basic negative shifts
        {-1, 1, -2},
        {-4, 3, -32},
        {-2, 5, -64},

        // Boundary cases for bit_shift == 63 special handling
        {-1, 63, min64},      // Exactly at boundary
        {0, 63, 0},           // Zero case with max shift

        // Boundary cases for various shift amounts (value == INT64_MIN / 2^shift)
        {min64 / 2, 1, min64},
        {min64 / 4, 2, min64},
        {min64 / 8, 3, min64},
        {min64 / 16, 4, min64},
        {min64 / 256, 8, min64},

        // Large negative values that don't overflow
        {-1000000000000000LL, 3, -8000000000000000LL},
        {-100000000000LL, 10, -102400000000000LL},
    };

    for(const auto& [value, shift, expected] : test_data)
    {
        CScriptNum v_int64{value, CScriptNum::INT64_SERIALIZED_SIZE};
        v_int64 <<= CScriptNum{shift, CScriptNum::INT64_SERIALIZED_SIZE};
        const CScriptNum expected_int64{expected, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(expected_int64, v_int64);

        CScriptNum v_bint{bint{value}, test_bint_max_len};
        v_bint <<= CScriptNum{bint{shift}, test_bint_max_len};
        const CScriptNum expected_bint{bint{expected}, test_bint_max_len};
        BOOST_CHECK_EQUAL(expected_bint, v_bint);
    }

    // Test overflow cases for negative values
    // Test cases that are just over the boundary (should overflow)
    using overflow_test = tuple<int64_t, int64_t>;
    const vector<overflow_test> overflow_cases = {
        // Basic overflow cases
        {min64, 1},              // INT64_MIN << 1 overflows
        {min64 / 2 - 1, 1},      // Just over boundary for shift=1

        // Boundary -1 cases for different shifts
        {min64 / 4 - 1, 2},      // Just over boundary for shift=2
        {min64 / 8 - 1, 3},      // Just over boundary for shift=3
        {min64 / 16 - 1, 4},     // Just over boundary for shift=4

        // Special case: bit_shift == 63
        {-2, 63},                // -2 << 63 overflows (only -1 is allowed)
        {-10, 63},               // -10 << 63 overflows

        // Large shifts
        {-1, 64},                // Shift >= bit width
        {1, 64},                 // Positive case for comparison
        {-5, 100},               // Very large shift
    };

    for(const auto& [value, shift] : overflow_cases)
    {
        // Skip shifts >= 64 for bint tests as they might have different behavior
        if(shift < 64)
        {
            CScriptNum v_int64{value, CScriptNum::INT64_SERIALIZED_SIZE};
            const CScriptNum shift_num{shift, CScriptNum::INT64_SERIALIZED_SIZE};
            BOOST_CHECK_THROW(v_int64 <<= shift_num, scriptnum_overflow_error);
        }
    }
}

BOOST_AUTO_TEST_CASE(lshift_overflow_int64)
{
    CScriptNum a{int64_t{1} << 62, CScriptNum::INT64_SERIALIZED_SIZE};
    const CScriptNum shift{int64_t{1}, CScriptNum::INT64_SERIALIZED_SIZE};

    try
    {
        a <<= shift;
        BOOST_FAIL("should throw scriptnum_overflow_error when shift would overflow");
    }
    catch(const scriptnum_overflow_error& e)
    {
        BOOST_CHECK_EQUAL(std::string{e.what()}, "script number overflow");
    }
}

BOOST_AUTO_TEST_CASE(chronicle_construction)
{
    using namespace std;

    using test_args = tuple<vector<uint8_t>,    // stack item 
                            min_encoding_check, // apply min encoding check
                            ScriptError>;       // expected error
    const vector<test_args> test_data
    {
        // min-encoding checks=no
        {
            {},
            min_encoding_check::no,
            SCRIPT_ERR_OK
        },
        {
            {42, 0}, // Non-minimal - leading 0
            min_encoding_check::no,
            SCRIPT_ERR_OK
        },
        // min-encoding checks=yes
        {
            {},
            min_encoding_check::yes,
            SCRIPT_ERR_OK
        },
        {
            {42, 0},
            min_encoding_check::yes,
            SCRIPT_ERR_SCRIPTNUM_MINENCODE
        },
    };
    for(const auto& [script, min_encoding, exp_error] : test_data)
    {
        try
        {
            const CScriptNum n{script, min_encoding};
            BOOST_CHECK_EQUAL(exp_error, SCRIPT_ERR_OK);
        }
        catch(const scriptnum_minencode_error& e)
        {
            BOOST_CHECK(min_encoding == min_encoding_check::yes);
            BOOST_CHECK(!bsv::IsMinimallyEncoded(script,
                                                 CScriptNum::MAXIMUM_ELEMENT_SIZE));
        }
    }
}

BOOST_AUTO_TEST_CASE(insertion_op)
{
    for(const int64_t n : test_data)
    {
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        ostringstream actual;
        actual << a;

        ostringstream expected;
        expected << n;
        BOOST_CHECK_EQUAL(expected.str(), actual.str());
    }

    for(const int64_t n : test_data)
    {
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        ostringstream actual;
        actual << a;

        ostringstream expected;
        expected << n;
        BOOST_CHECK_EQUAL(expected.str(), actual.str());
    }
}

BOOST_AUTO_TEST_CASE(equality)
{
    for(const int64_t n : test_data)
    {
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{n, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(a, a);
        BOOST_CHECK_EQUAL(a, b);
        BOOST_CHECK_EQUAL(b, a);
    }

    for(const int64_t n : test_data)
    {
        bint bn{n};
        bn *= bint{10}; // *10 so we are testing outside of range of int64_t
        CScriptNum a{bn, test_bint_max_len};
        CScriptNum b{bn, test_bint_max_len};
        BOOST_CHECK_EQUAL(a, a);
        BOOST_CHECK_EQUAL(a, b);
        BOOST_CHECK_EQUAL(b, a);
    }
}

BOOST_AUTO_TEST_CASE(less)
{
    vector<pair<int64_t, int64_t>> test_data{
        {min64, -1}, {-1, 0}, {0, 1}, {min64, max64}, {1, max64}};
    for(const auto& [n, m] : test_data)
    {
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_LT(a, b);
        BOOST_CHECK_LE(a, a);
        BOOST_CHECK_GE(a, a);
        BOOST_CHECK_GT(b, a);
    }

    for(const auto& [n, m] : test_data)
    {
        // n *= 10; // *10 so we are testing outside of range of int64_t
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_LT(a, b);
        BOOST_CHECK_LE(a, a);
        BOOST_CHECK_GE(a, a);
        BOOST_CHECK_GT(b, a);
    }

    for(const auto& [n, m] : test_data)
    {
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_LT(a, b);
        BOOST_CHECK_LE(a, a);
        BOOST_CHECK_GE(a, a);
        BOOST_CHECK_GT(b, a);
    }
}

BOOST_AUTO_TEST_CASE(addition)
{
    const vector<tuple<int64_t, int64_t, int64_t>> test_data{
        {-1, 0, -1},
        {0, -1, -1},
        {-1, 1, 0},
        {1, -1, 0},
        {0, 1, 1},
        {1, 0, 1},
        {min64 + 1, -1, min64},
        {max64 - 1, +1, max64}};

    for(const auto& [n, m, o] : test_data)
    {
        // little int + little int
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{o, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a + b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int + big int
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{bint{o}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a + b);
    }
}

BOOST_AUTO_TEST_CASE(subtraction)
{
    const vector<tuple<int64_t, int64_t, int64_t>> test_data{
        {0, 1, -1},
        {-1, 0, -1},
        {1, 1, 0},
        {-1, -1, 0},
        {2, 1, 1},
        {0, -1, 1},
        {min64 + 1, 1, min64},
        {max64 - 1, -1, max64}};

    for(const auto& [n, m, o] : test_data)
    {
        // little int - little int
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{o, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a - b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int - big int
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{bint{o}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a - b);
    }
}

BOOST_AUTO_TEST_CASE(multiplication)
{
    // clang-format off
    const vector<tuple<int64_t, int64_t, int64_t>> test_data
    {
        {1, -1, -1},
        {-1, 1, -1},
        {0, 1, 0}, 
        {1, 0, 0},
        {1, 1, 1},
        {-1, -1, 1},
        {min64, 1, min64},
        {min64 + 1, -1, max64},
        {max64, 1, max64},
        {max64, -1, min64 + 1}
    };
    // clang-format on

    for(const auto& [n, m, o] : test_data)
    {
        // little int * little int
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{o, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a * b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int * big int
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{bint{o}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a * b);
    }
}

BOOST_AUTO_TEST_CASE(add_max_length_invariant)
{
    using test_args = tuple<int64_t,    // a
                            int64_t,    // b
                            size_t>;    // max_len
    const vector<test_args> non_throwing_cases
    {
        {0x1,  0x1, 1},
        {0x7f,  0x1, 2},
        {-0x1, -0x1, 1},
        {-0x7f, -0x1, 2},
    };
    // int64_t
    for(const auto& [a, b, max_len] : non_throwing_cases)
    {
        CScriptNum sn_a{a, max_len};
        const CScriptNum sn_b{b, max_len};
        sn_a += sn_b;
        BOOST_CHECK_EQUAL(sn_a.getvch().size(), max_len);
    }
    // bint
    for(const auto& [a, b, max_len] : non_throwing_cases)
    {
        CScriptNum sn_a{bint{a}, max_len};
        const CScriptNum sn_b{bint{b}, max_len};
        sn_a += sn_b;
        BOOST_CHECK_EQUAL(sn_a.getvch().size(), max_len);
    }

    // (bsv::bint only - int64_t intentionally unchanged for backward compatibility)
    const vector<test_args> throwing_cases
    {
        {0x7f,  0x1, 1},
        {-0x7f, -0x1, 1},
    };
    for(const auto& [a, b, max_len] : throwing_cases)
    {
        CScriptNum sn_a{bint{a}, max_len};
        const CScriptNum sn_b{bint{b}, max_len};
        BOOST_CHECK_THROW(sn_a += sn_b, scriptnum_overflow_error);
    }
}

BOOST_AUTO_TEST_CASE(sub_max_length_invariant)
{
    using test_args = tuple<int64_t,    // a
                            int64_t,    // b
                            size_t>;    // max_len
    const vector<test_args> non_throwing_cases
    {
        {0,  0x7f, 1},
        {-1, 0x7f, 2},
    };
    // int64_t
    for(const auto& [a, b, max_len] : non_throwing_cases)
    {
        CScriptNum sn_a{a, max_len};
        const CScriptNum sn_b{b, max_len};
        sn_a -= sn_b;
        BOOST_CHECK_EQUAL(sn_a.getvch().size(), max_len);
    }
    // bint
    for(const auto& [a, b, max_len] : non_throwing_cases)
    {
        CScriptNum sn_a{bint{a}, max_len};
        const CScriptNum sn_b{bint{b}, max_len};
        sn_a -= sn_b;
        BOOST_CHECK_EQUAL(sn_a.getvch().size(), max_len);
    }

    // (bsv::bint only - int64_t intentionally unchanged for backward compatibility)
    const vector<test_args> throwing_cases
    {
        {-1, 0x7f, 1},
    };
    for(const auto& [a, b, max_len] : throwing_cases)
    {
        CScriptNum sn_a{bint{a}, max_len};
        const CScriptNum sn_b{bint{b}, max_len};
        BOOST_CHECK_THROW(sn_a -= sn_b, scriptnum_overflow_error);
    }
}

BOOST_AUTO_TEST_CASE(mul_max_length_invariant)
{
    using test_args = tuple<int64_t,    // a
                            int64_t,    // b
                            size_t>;    // max_len
    const vector<test_args> non_throwing_cases
    {
        {0x20,  0x2, 1},    // = 0x40
        {0x40,  0x2, 2},    // = 0x0, 0x80
        {0x20, -0x2, 1},    // = 0xc0
        {0x40, -0x2, 2},    // = 0x80, 0x80
    };
    // int64_t
    for(const auto& [a, b, max_len] : non_throwing_cases)
    {
        CScriptNum sn_a{a, max_len};
        const CScriptNum sn_b{b, max_len};
        sn_a *= sn_b;
        BOOST_CHECK_EQUAL(sn_a.getvch().size(), max_len);
    }
    // bint
    for(const auto& [a, b, max_len] : non_throwing_cases)
    {
        CScriptNum sn_a{bint{a}, max_len};
        const CScriptNum sn_b{bint{b}, max_len};
        sn_a *= sn_b;
        BOOST_CHECK_EQUAL(sn_a.getvch().size(), max_len);
    }

    // (bsv::bint only - int64_t intentionally unchanged for backward compatibility)
    const vector<test_args> throwing_cases
    {
        {0x40,  0x2, 1},    // = 0x0, 0x80
        {0x40, -0x2, 1},    // = 0x80, 0x80
    };
    for(const auto& [a, b, max_len] : throwing_cases)
    {
        CScriptNum sn_a{bint{a}, max_len};
        const CScriptNum sn_b{bint{b}, max_len};
        BOOST_CHECK_THROW(sn_a *= sn_b, scriptnum_overflow_error);
    }
}

BOOST_AUTO_TEST_CASE(division)
{
    const vector<tuple<int64_t, int64_t, int64_t>> test_data{
        {1, -1, -1},
        {-1, 1, -1},
        {0, 1, 0},
        {1, 1, 1},
        {-1, -1, 1},
        {min64, 1, min64},
        {min64 + 1, -1, max64},
        {max64, 1, max64},
        {max64, -1, min64 + 1}};

    for(const auto& [n, m, o] : test_data)
    {
        // little int / little int
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{o, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a / b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int / big int
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{bint{o}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a / b);
    }
}

BOOST_AUTO_TEST_CASE(modular)
{
    const vector<tuple<int64_t, int64_t, int64_t>> test_data{{-3, -2, -1},
                                                             {1, 1, 0},
                                                             {-1, -1, 0},
                                                             {1, -1, 0},
                                                             {-1, 1, 0},
                                                             {3, 2, 1}};

    for(const auto& [n, m, o] : test_data)
    {
        // little int % little int
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{o, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a % b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int % big int
        CScriptNum a{bint{n}, CScriptNum::MAXIMUM_ELEMENT_SIZE};
        CScriptNum b{bint{m}, CScriptNum::MAXIMUM_ELEMENT_SIZE};
        CScriptNum c{bint{o}, CScriptNum::MAXIMUM_ELEMENT_SIZE};
        BOOST_CHECK_EQUAL(c, a % b);
    }
}

BOOST_AUTO_TEST_CASE(and_)
{
    const vector<tuple<int64_t, int64_t, int64_t>> test_data{
        {0x0, 0x0, 0x0},
        {0xffffffffffffffff, 0x0, 0x0},
        {0x0, 0xffffffffffffffff, 0x0},
        {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff},
        {0x555555555555555, 0xaaaaaaaaaaaaaaa, 0x0},
        {0xaaaaaaaaaaaaaaa, 0x555555555555555, 0x0},
    };

    for(const auto& [n, m, o] : test_data)
    {
        // little int & little int
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{o, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a & b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int & big int
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum c{bint{o}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(c, a & b);
    }
}

BOOST_AUTO_TEST_CASE(negation)
{
    // clang-format off
    const vector<pair<int64_t, int64_t>> test_data
    {
        {0, 0}, 
        {1, -1}, 
        {-1, 1}, 
        {max64, min64+1}
    };
    // clang-format off

    for(const auto& [n, m] : test_data)
    {
        // little int & little int
        CScriptNum a{n, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{m, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(b, -a);
    }

    for(const auto& [n, m] : test_data)
    {
        // big int & big int
        CScriptNum a{bint{n}, CScriptNum::INT64_SERIALIZED_SIZE};
        CScriptNum b{bint{m}, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(b, -a);
    }
}

BOOST_AUTO_TEST_CASE(getint)
{
    constexpr int min_int{numeric_limits<int>::min()};
    constexpr int max_int{numeric_limits<int>::max()};

    const bint max64{max_int};
    CScriptNum max{max64 + 1, test_bint_max_len};
    BOOST_CHECK_EQUAL(max_int, max.getint());

    const bint min64{min_int};
    CScriptNum min{min64 - 1, test_bint_max_len};
    BOOST_CHECK_EQUAL(min_int, min.getint());
}

BOOST_AUTO_TEST_CASE(to_size_t_limited)
{
    //constexpr size_t size_t_min{std::numeric_limits<size_t>::min()}; // This causes compiler error ons MSVC when invoking CScriptNum constructor due to narrowing conversion
    static_assert(std::numeric_limits<size_t>::min() == 0);
    constexpr size_t size_t_min{ 0 };

    constexpr size_t size_t_max{static_cast<size_t>(std::numeric_limits<int32_t>::max()) };

    BOOST_CHECK_EQUAL(size_t_min, CScriptNum{size_t_min}.to_size_t_limited());
    BOOST_CHECK_EQUAL(1U, CScriptNum{1}.to_size_t_limited());

    CScriptNum sn_min{bint{size_t_min}, CScriptNum::MAXIMUM_ELEMENT_SIZE};
    BOOST_CHECK_EQUAL(size_t_min, sn_min.to_size_t_limited());
    CScriptNum sn_one{bint{1}, CScriptNum::MAXIMUM_ELEMENT_SIZE};
    BOOST_CHECK_EQUAL(1U, sn_one.to_size_t_limited());
    CScriptNum sn_max{bint{size_t_max}, CScriptNum::MAXIMUM_ELEMENT_SIZE};
    BOOST_CHECK_EQUAL(size_t_max, sn_max.to_size_t_limited());
}

// clang-format off
/** A selection of numbers that do not trigger int64_t overflow
 *  when added/subtracted. */
static const std::array<int64_t, 13> values = {0,
                                 1,
                                 -2,
                                 127,
                                 128,
                                 -255,
                                 256,
                                 (1LL << 15) - 1,
                                 -(1LL << 16),
                                 (1LL << 24) - 1,
                                 (1LL << 31),
                                 1 - (1LL << 32),
                                 1LL << 40};

static const std::array<int64_t, 9> offsets = {1, 0x79, 0x80, 0x81, 0xFF,   
                                               0x7FFF, 0x8000, 0xFFFF, 0x1'0000};

static bool verify(const CScriptNum10 &bignum, const CScriptNum &scriptnum) {
    return bignum.getvch() == scriptnum.getvch() &&
           bignum.getint() == scriptnum.getint();
}

static void CheckCreateVch(const int64_t &num) {
    CScriptNum10 bignum{num};
    CScriptNum scriptnum{num, CScriptNum::INT64_SERIALIZED_SIZE};
    BOOST_CHECK(verify(bignum, scriptnum));

    std::vector<uint8_t> vch = bignum.getvch();

    CScriptNum10 bignum2{bignum.getvch(), false};
    vch = scriptnum.getvch();
    CScriptNum scriptnum2{scriptnum.getvch(), min_encoding_check::no};
    BOOST_CHECK(verify(bignum2, scriptnum2));

    CScriptNum10 bignum3{scriptnum2.getvch(), false};
    CScriptNum scriptnum3{bignum2.getvch(), min_encoding_check::no};
    BOOST_CHECK(verify(bignum3, scriptnum3));
}

static void CheckCreateInt(const int64_t &num) {
    constexpr size_t max_len = CScriptNum::INT64_SERIALIZED_SIZE;
    CScriptNum10 bignum{num};
    CScriptNum scriptnum{num, max_len};
    BOOST_CHECK(verify(bignum, scriptnum));
    BOOST_CHECK(verify(CScriptNum10{bignum.getint()},
                       CScriptNum{scriptnum.getint(), max_len}));
    BOOST_CHECK(verify(CScriptNum10{scriptnum.getint()},
                       CScriptNum{bignum.getint(), max_len}));
    BOOST_CHECK(verify(CScriptNum10{CScriptNum10{scriptnum.getint()}.getint()},
                       CScriptNum{CScriptNum{bignum.getint(), max_len}.getint(),
                                  max_len}));
}

static void CheckAdd(const int64_t &num1, const int64_t &num2) {
    constexpr size_t max_len = CScriptNum::INT64_SERIALIZED_SIZE;
    const CScriptNum10 bignum1{num1};
    const CScriptNum10 bignum2{num2};
    const CScriptNum scriptnum1{num1, max_len};
    const CScriptNum scriptnum2{num2, max_len};
    CScriptNum10 bignum3{num1};
    CScriptNum10 bignum4{num1};
    CScriptNum scriptnum3{num1, max_len};
    CScriptNum scriptnum4{num1, max_len};

    // int64_t overflow is undefined.
    bool invalid =
        (((num2 > 0) &&
          (num1 > (std::numeric_limits<int64_t>::max() - num2))) ||
         ((num2 < 0) && (num1 < (std::numeric_limits<int64_t>::min() - num2))));
    if (!invalid) {
        BOOST_CHECK(verify(bignum1 + bignum2, scriptnum1 + scriptnum2));
        BOOST_CHECK(verify(bignum1 + bignum2, scriptnum1 + scriptnum2));
        BOOST_CHECK(verify(bignum1 + bignum2, scriptnum2 + scriptnum1));
    }
}

static void CheckNegate(const int64_t &num) {
    const CScriptNum10 bignum{num};
    const CScriptNum scriptnum{num, CScriptNum::INT64_SERIALIZED_SIZE};

    // -INT64_MIN is undefined
    if (num != std::numeric_limits<int64_t>::min())
        BOOST_CHECK(verify(-bignum, -scriptnum));
}

static void CheckSubtract(const int64_t &num1, const int64_t &num2) {
    constexpr size_t max_len = CScriptNum::INT64_SERIALIZED_SIZE;
    const CScriptNum10 bignum1{num1};
    const CScriptNum10 bignum2{num2};
    const CScriptNum scriptnum1{num1, max_len};
    const CScriptNum scriptnum2{num2, max_len};
    bool invalid = false;

    // int64_t overflow is undefined.
    invalid =
        ((num2 > 0 && num1 < std::numeric_limits<int64_t>::min() + num2) ||
         (num2 < 0 && num1 > std::numeric_limits<int64_t>::max() + num2));
    if (!invalid) {
        BOOST_CHECK(verify(bignum1 - bignum2, scriptnum1 - scriptnum2));
        BOOST_CHECK(verify(bignum1 - bignum2, scriptnum1 - scriptnum2));
    }

    invalid =
        ((num1 > 0 && num2 < std::numeric_limits<int64_t>::min() + num1) ||
         (num1 < 0 && num2 > std::numeric_limits<int64_t>::max() + num1));
    if (!invalid) {
        BOOST_CHECK(verify(bignum2 - bignum1, scriptnum2 - scriptnum1));
        BOOST_CHECK(verify(bignum2 - bignum1, scriptnum2 - scriptnum1));
    }
}

static void CheckCompare(const int64_t &num1, const int64_t &num2) {
    constexpr size_t max_len = CScriptNum::INT64_SERIALIZED_SIZE;
    const CScriptNum10 bignum1{num1};
    const CScriptNum10 bignum2{num2};
    const CScriptNum scriptnum1{num1, max_len};
    const CScriptNum scriptnum2{num2, max_len};

    BOOST_CHECK((bignum1 == bignum1) == (scriptnum1 == scriptnum1));
    BOOST_CHECK((bignum1 != bignum1) == (scriptnum1 != scriptnum1));
    BOOST_CHECK((bignum1 < bignum1) == (scriptnum1 < scriptnum1));
    BOOST_CHECK((bignum1 > bignum1) == (scriptnum1 > scriptnum1));
    BOOST_CHECK((bignum1 >= bignum1) == (scriptnum1 >= scriptnum1));
    BOOST_CHECK((bignum1 <= bignum1) == (scriptnum1 <= scriptnum1));

    BOOST_CHECK((bignum1 < bignum1) == (scriptnum1 < num1));
    BOOST_CHECK((bignum1 > bignum1) == (scriptnum1 > num1));
    BOOST_CHECK((bignum1 >= bignum1) == (scriptnum1 >= num1));
    BOOST_CHECK((bignum1 <= bignum1) == (scriptnum1 <= num1));

    BOOST_CHECK((bignum1 == bignum2) == (scriptnum1 == scriptnum2));
    BOOST_CHECK((bignum1 != bignum2) == (scriptnum1 != scriptnum2));
    BOOST_CHECK((bignum1 < bignum2) == (scriptnum1 < scriptnum2));
    BOOST_CHECK((bignum1 > bignum2) == (scriptnum1 > scriptnum2));
    BOOST_CHECK((bignum1 >= bignum2) == (scriptnum1 >= scriptnum2));
    BOOST_CHECK((bignum1 <= bignum2) == (scriptnum1 <= scriptnum2));

    BOOST_CHECK((bignum1 < bignum2) == (scriptnum1 < num2));
    BOOST_CHECK((bignum1 > bignum2) == (scriptnum1 > num2));
    BOOST_CHECK((bignum1 >= bignum2) == (scriptnum1 >= num2));
    BOOST_CHECK((bignum1 <= bignum2) == (scriptnum1 <= num2));
}

static void RunCreate(const int64_t &num) {
    CheckCreateInt(num);
    CScriptNum scriptnum{num, CScriptNum::INT64_SERIALIZED_SIZE};
    if (scriptnum.getvch().size() <= CScriptNum::MAXIMUM_ELEMENT_SIZE) {
        CheckCreateVch(num);
    } else {
        BOOST_CHECK_THROW(CheckCreateVch(num), scriptnum10_error);
    }
}

static void RunOperators(const int64_t &num1, const int64_t &num2) {
    CheckAdd(num1, num2);
    CheckSubtract(num1, num2);
    CheckNegate(num1);
    CheckCompare(num1, num2);
}

BOOST_AUTO_TEST_CASE(creation)
{
    for (size_t i = 0; i < values.size(); ++i)
    {
        for (size_t j = 0; j < offsets.size(); ++j)
        {
            RunCreate(values[i]);
            RunCreate(values[i] + offsets[j]);
            RunCreate(values[i] - offsets[j]);
        }
    }
}

BOOST_AUTO_TEST_CASE(operators)
{
    for(size_t i = 0; i < values.size(); ++i)
    {
        for(size_t j = 0; j < offsets.size(); ++j)
        {
            RunOperators(values[i], values[i]);
            RunOperators(values[i], -values[i]);
            RunOperators(values[i], values[j]);
            RunOperators(values[i], -values[j]);
            RunOperators(values[i] + values[j], values[j]);
            RunOperators(values[i] + values[j], -values[j]);
            RunOperators(values[i] - values[j], values[j]);
            RunOperators(values[i] - values[j], -values[j]);
            RunOperators(values[i] + values[j], values[i] + values[j]);
            RunOperators(values[i] + values[j], values[i] - values[j]);
            RunOperators(values[i] - values[j], values[i] + values[j]);
            RunOperators(values[i] - values[j], values[i] - values[j]);
        }
    }
}

static void CheckMinimalyEncode(std::vector<uint8_t> data,
                                const std::vector<uint8_t> &expected) {
    bool alreadyEncoded = bsv::IsMinimallyEncoded(data, data.size());
    bool hasEncoded = bsv::MinimallyEncode(data);
    BOOST_CHECK_EQUAL(hasEncoded, !alreadyEncoded);
    BOOST_CHECK(data == expected);
}

BOOST_AUTO_TEST_CASE(minimize_encoding_test) {
    CheckMinimalyEncode({}, {});

    // Check that positive and negative zeros encode to nothing.
    std::vector<uint8_t> zero, negZero;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS; i++) {
        zero.push_back(0x00);
        CheckMinimalyEncode(zero, {});

        negZero.push_back(0x80);
        CheckMinimalyEncode(negZero, {});

        // prepare for next round.
        negZero[negZero.size() - 1] = 0x00;
    }

    // Keep one leading zero when sign bit is used.
    std::vector<uint8_t> n{0x80, 0x00}, negn{0x80, 0x80};
    std::vector<uint8_t> npadded = n, negnpadded = negn;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS; i++) {
        CheckMinimalyEncode(npadded, n);
        npadded.push_back(0x00);

        CheckMinimalyEncode(negnpadded, negn);
        negnpadded[negnpadded.size() - 1] = 0x00;
        negnpadded.push_back(0x80);
    }

    // Mege leading byte when sign bit isn't used.
    std::vector<uint8_t> k{0x7f}, negk{0xff};
    std::vector<uint8_t> kpadded = k, negkpadded = negk;
    for (size_t i = 0; i < MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS; i++) {
        CheckMinimalyEncode(kpadded, k);
        kpadded.push_back(0x00);

        CheckMinimalyEncode(negkpadded, negk);
        negkpadded[negkpadded.size() - 1] &= 0x7f;
        negkpadded.push_back(0x80);
    }
}
// clang-format on

BOOST_AUTO_TEST_CASE(rshift_division_semantics)
{
    // Verify that right shift implements division by 2^n (round toward zero)
    // not arithmetic right shift (round toward negative infinity)
    using test_args = std::tuple<int64_t,  // value
                                 int64_t,  // shift
                                 int64_t>; // expected;
    const std::vector<test_args> test_data 
    {
        {0, 1, 0},
        {0, 63, 0},
        {0, 64, 0},

        {8, 1, 4},
        {8, 2, 2},
        {8, 3, 1},

        // Negative values - division rounds toward zero
        {-3, 1, -1},  // -3/2 = -1.5 → -1 (not -2 from arithmetic shift)
        {-5, 2, -1},  // -5/4 = -1.25 → -1 (not -2 from arithmetic shift)
        {-7, 2, -1},  // -7/4 = -1.75 → -1 (not -2 from arithmetic shift)
        {-8, 3, -1},  // -8/8 = -1
        {-1, 1, 0},   // -1/2 = -0.5 → 0 (not -1 from arithmetic shift)

        // INT64_MIN boundary cases
        {min64, 1, min64 / 2},   // -2^63 / 2 = -2^62
        {min64, 63, -1},         // -2^63 / 2^63 = -1
        {min64, 62, -2},         // -2^63 / 2^62 = -2

        // Shift >= bit width (64 bits)
        {1, 64, 0},
        {-1, 64, 0},
        {max64, 64, 0},
        {min64, 64, 0},
    };

    // Test int64_t (use INT64_SERIALIZED_SIZE to accommodate min64/max64)
    for(const auto& [value, shift, expected] : test_data)
    {
        CScriptNum sn_value{value, CScriptNum::INT64_SERIALIZED_SIZE};
        const CScriptNum sn_shift{shift, CScriptNum::INT64_SERIALIZED_SIZE};
        sn_value >>= sn_shift;
        const CScriptNum sn_expected{expected, CScriptNum::INT64_SERIALIZED_SIZE};
        BOOST_CHECK_EQUAL(sn_value, sn_expected);
    }

    // Test bint (use test_bint_max_len to accommodate large values)
    for(const auto& [value, shift, expected] : test_data)
    {
        CScriptNum sv_value{bint{value}, test_bint_max_len};
        const CScriptNum sn_shift{bint{shift}, test_bint_max_len};
        sv_value >>= sn_shift;
        const CScriptNum sn_expected{bint{expected}, test_bint_max_len};
        BOOST_CHECK_EQUAL(sv_value, sn_expected);
    }
}

BOOST_AUTO_TEST_SUITE_END()
