// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/int_serialization.h"
#include "script/script.h"
#include "script/script_num.h"
#include "scriptnum10.h"

#include <boost/test/unit_test.hpp>

#include "big_int.h"

using namespace std;
using bsv::bint;

namespace
{
    constexpr auto min64 = numeric_limits<int64_t>::min();
    constexpr auto max64 = numeric_limits<int64_t>::max();

    vector<int64_t> test_data{min64, -1, 0, 1, max64};
}

BOOST_AUTO_TEST_SUITE(scriptnum_tests)

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
            CScriptNum actual{v, false, max_size, big_int};
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
            CScriptNum actual{v, false, max_size, big_int};
            BOOST_FAIL("should throw");
        }
        catch(...)
        {
        }
    }
}

BOOST_AUTO_TEST_CASE(insertion_op)
{
    for(const int64_t n : test_data)
    {
        CScriptNum a{n};
        ostringstream actual;
        actual << a;

        ostringstream expected;
        expected << n;
        BOOST_CHECK_EQUAL(expected.str(), actual.str());
    }

    for(const int64_t n : test_data)
    {
        CScriptNum a{bint{n}};
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
        CScriptNum a{n};
        CScriptNum b{n};
        BOOST_CHECK_EQUAL(a, a);
        BOOST_CHECK_EQUAL(a, b);
        BOOST_CHECK_EQUAL(b, a);
    }

    for(const int64_t n : test_data)
    {
        bint bn{n};
        bn *= bint{10}; // *10 so we are testing outside of range of int64_t
        CScriptNum a{bn};
        CScriptNum b{bn};
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
        CScriptNum a{n};
        CScriptNum b{m};
        BOOST_CHECK_LT(a, b);
        BOOST_CHECK_LE(a, a);
        BOOST_CHECK_GE(a, a);
        BOOST_CHECK_GT(b, a);
    }

    for(const auto& [n, m] : test_data)
    {
        // n *= 10; // *10 so we are testing outside of range of int64_t
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        BOOST_CHECK_LT(a, b);
        BOOST_CHECK_LE(a, a);
        BOOST_CHECK_GE(a, a);
        BOOST_CHECK_GT(b, a);
    }

    for(const auto& [n, m] : test_data)
    {
        CScriptNum a{n};
        CScriptNum b{bint{m}};
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
        CScriptNum a{n};
        CScriptNum b{m};
        CScriptNum c{o};
        BOOST_CHECK_EQUAL(c, a + b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int + big int
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        CScriptNum c{bint{o}};
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
        CScriptNum a{n};
        CScriptNum b{m};
        CScriptNum c{o};
        BOOST_CHECK_EQUAL(c, a - b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int - big int
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        CScriptNum c{bint{o}};
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
        CScriptNum a{n};
        CScriptNum b{m};
        CScriptNum c{o};
        BOOST_CHECK_EQUAL(c, a * b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int * big int
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        CScriptNum c{bint{o}};
        BOOST_CHECK_EQUAL(c, a * b);
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
        CScriptNum a{n};
        CScriptNum b{m};
        CScriptNum c{o};
        BOOST_CHECK_EQUAL(c, a / b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int / big int
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        CScriptNum c{bint{o}};
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
        CScriptNum a{n};
        CScriptNum b{m};
        CScriptNum c{o};
        BOOST_CHECK_EQUAL(c, a % b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int % big int
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        CScriptNum c{bint{o}};
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
        CScriptNum a{n};
        CScriptNum b{m};
        CScriptNum c{o};
        BOOST_CHECK_EQUAL(c, a & b);
    }

    for(const auto& [n, m, o] : test_data)
    {
        // big int & big int
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        CScriptNum c{bint{o}};
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
        CScriptNum a{n};
        CScriptNum b{m};
        BOOST_CHECK_EQUAL(b, -a);
    }

    for(const auto& [n, m] : test_data)
    {
        // big int & big int
        CScriptNum a{bint{n}};
        CScriptNum b{bint{m}};
        BOOST_CHECK_EQUAL(b, -a);
    }
}

BOOST_AUTO_TEST_CASE(getint)
{
    constexpr int min_int{numeric_limits<int>::min()};
    constexpr int max_int{numeric_limits<int>::max()};

    const bint max64{max_int};
    CScriptNum max{max64 + 1};
    BOOST_CHECK_EQUAL(max_int, max.getint());

    const bint min64{min_int};
    CScriptNum min{min64 - 1};
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

    BOOST_CHECK_EQUAL(size_t_min, CScriptNum{bint{size_t_min}}.to_size_t_limited());
    BOOST_CHECK_EQUAL(1U, CScriptNum{bint{1}}.to_size_t_limited());
    BOOST_CHECK_EQUAL(size_t_max, CScriptNum{bint{size_t_max}}.to_size_t_limited());
}

// clang-format off
/** A selection of numbers that do not trigger int64_t overflow
 *  when added/subtracted. */
static const int64_t values[] = {0,
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

static const int64_t offsets[] = {1,      0x79,   0x80,   0x81,   0xFF,
                                  0x7FFF, 0x8000, 0xFFFF, 0x10000};

static bool verify(const CScriptNum10 &bignum, const CScriptNum &scriptnum) {
    return bignum.getvch() == scriptnum.getvch() &&
           bignum.getint() == scriptnum.getint();
}

static void CheckCreateVch(const int64_t &num) {
    CScriptNum10 bignum(num);
    CScriptNum scriptnum(num);
    BOOST_CHECK(verify(bignum, scriptnum));

    std::vector<uint8_t> vch = bignum.getvch();

    CScriptNum10 bignum2(bignum.getvch(), false);
    vch = scriptnum.getvch();
    CScriptNum scriptnum2(scriptnum.getvch(), false);
    BOOST_CHECK(verify(bignum2, scriptnum2));

    CScriptNum10 bignum3(scriptnum2.getvch(), false);
    CScriptNum scriptnum3(bignum2.getvch(), false);
    BOOST_CHECK(verify(bignum3, scriptnum3));
}

static void CheckCreateInt(const int64_t &num) {
    CScriptNum10 bignum(num);
    CScriptNum scriptnum(num);
    BOOST_CHECK(verify(bignum, scriptnum));
    BOOST_CHECK(
        verify(CScriptNum10(bignum.getint()), CScriptNum(scriptnum.getint())));
    BOOST_CHECK(
        verify(CScriptNum10(scriptnum.getint()), CScriptNum(bignum.getint())));
    BOOST_CHECK(verify(CScriptNum10(CScriptNum10(scriptnum.getint()).getint()),
                       CScriptNum(CScriptNum(bignum.getint()).getint())));
}

static void CheckAdd(const int64_t &num1, const int64_t &num2) {
    const CScriptNum10 bignum1(num1);
    const CScriptNum10 bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);
    CScriptNum10 bignum3(num1);
    CScriptNum10 bignum4(num1);
    CScriptNum scriptnum3(num1);
    CScriptNum scriptnum4(num1);

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
    const CScriptNum10 bignum(num);
    const CScriptNum scriptnum(num);

    // -INT64_MIN is undefined
    if (num != std::numeric_limits<int64_t>::min())
        BOOST_CHECK(verify(-bignum, -scriptnum));
}

static void CheckSubtract(const int64_t &num1, const int64_t &num2) {
    const CScriptNum10 bignum1(num1);
    const CScriptNum10 bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);
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
    const CScriptNum10 bignum1(num1);
    const CScriptNum10 bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);

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
    CScriptNum scriptnum(num);
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

BOOST_AUTO_TEST_CASE(creation) {
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
            RunCreate(values[i]);
            RunCreate(values[i] + offsets[j]);
            RunCreate(values[i] - offsets[j]);
        }
    }
}

BOOST_AUTO_TEST_CASE(operators) {
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j) {
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

BOOST_AUTO_TEST_SUITE_END()
