// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "arith_uint256.h"
#include "test/test_bitcoin.h"
#include "uint256.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(arith_uint256_tests, BasicTestingSetup)

/// Convert vector to arith_uint256, via uint256 blob
inline arith_uint256 arith_uint256V(const std::vector<uint8_t> &vch) {
    return UintToArith256(uint256(vch));
}

constexpr std::array<uint8_t, 32> R1Array = {
    0x9c, 0x52, 0x4a, 0xdb, 0xcf, 0x56, 0x11, 0x12, 0x2b, 0x29, 0x12,
    0x5e, 0x5d, 0x35, 0xd2, 0xd2, 0x22, 0x81, 0xaa, 0xb5, 0x33, 0xf0,
    0x08, 0x32, 0xd5, 0x56, 0xb1, 0xf9, 0xea, 0xe5, 0x1d, 0x7d};
const char R1ArrayHex[] =
    "7D1DE5EAF9B156D53208F033B5AA8122D2d2355d5e12292b121156cfdb4a529c";
const double R1Ldouble =
    0.4887374590559308955; // R1L equals roughly R1Ldouble * 2^256
const arith_uint256 R1L =
    arith_uint256V(std::vector<uint8_t>(R1Array.begin(), R1Array.end()));
const uint64_t R1LLow64 = 0x121156cfdb4a529cULL;

constexpr std::array<uint8_t, 32> R2Array = {
    0x70, 0x32, 0x1d, 0x7c, 0x47, 0xa5, 0x6b, 0x40, 0x26, 0x7e, 0x0a,
    0xc3, 0xa6, 0x9c, 0xb6, 0xbf, 0x13, 0x30, 0x47, 0xa3, 0x19, 0x2d,
    0xda, 0x71, 0x49, 0x13, 0x72, 0xf0, 0xb4, 0xca, 0x81, 0xd7};

const arith_uint256 R2L =
    arith_uint256V(std::vector<uint8_t>(R2Array.begin(), R2Array.end()));

const char R1LplusR2L[] =
    "549FB09FEA236A1EA3E31D4D58F1B1369288D204211CA751527CFC175767850C";

constexpr std::array<uint8_t, 32> ZeroArray{};
const arith_uint256 ZeroL =
    arith_uint256V(std::vector<uint8_t>(ZeroArray.begin(), ZeroArray.end()));

constexpr std::array OneArray = []{
    std::array<uint8_t, 32> a{};
    a[0] = 1;
    return a;
}();

const arith_uint256 OneL =
    arith_uint256V(std::vector<uint8_t>(OneArray.begin(), OneArray.end()));

constexpr std::array MaxArray = [] {
    std::array<uint8_t, 32> a{};
    a.fill(0xff);
    return a;
}();

const arith_uint256 MaxL =
    arith_uint256V(std::vector<uint8_t>(MaxArray.begin(), MaxArray.end()));

const arith_uint256 HalfL = (OneL << 255);

std::string ArrayToString(const std::span<const uint8_t> a)
{
    std::stringstream Stream;
    Stream << std::hex;
    for (unsigned int i = 0; i < a.size(); ++i) {
        Stream << std::setw(2) << std::setfill('0')
               << (unsigned int)a[a.size() - i - 1];
    }
    return Stream.str();
}

BOOST_AUTO_TEST_CASE(basics) // constructors, equality, inequality
{
    BOOST_CHECK(1 == 0 + 1);
                         
    // constructor arith_uint256(vector<char>):
    BOOST_CHECK_EQUAL(R1L.ToString(), ArrayToString(R1Array));
    BOOST_CHECK_EQUAL(R2L.ToString(), ArrayToString(R2Array));
    BOOST_CHECK_EQUAL(ZeroL.ToString(), ArrayToString(ZeroArray));
    BOOST_CHECK_EQUAL(OneL.ToString(), ArrayToString(OneArray));
    BOOST_CHECK_EQUAL(MaxL.ToString(), ArrayToString(MaxArray));
    BOOST_CHECK_NE(OneL.ToString(), ArrayToString(ZeroArray));
                         
    // == and !=
    BOOST_CHECK(R1L != R2L);
    BOOST_CHECK(ZeroL != OneL);
    BOOST_CHECK(OneL != ZeroL);
    BOOST_CHECK(MaxL != ZeroL);
    BOOST_CHECK(~MaxL == ZeroL);
    BOOST_CHECK(((R1L ^ R2L) ^ R1L) == R2L);

    uint64_t Tmp64 = 0xc4dab720d9c7acaaULL;
    for (unsigned int i = 0; i < 256; ++i) {
        BOOST_CHECK(ZeroL != (OneL << i));
        BOOST_CHECK((OneL << i) != ZeroL);
        BOOST_CHECK(R1L != (R1L ^ (OneL << i)));
        BOOST_CHECK(((arith_uint256(Tmp64) ^ (OneL << i)) != Tmp64));
    }
    BOOST_CHECK(ZeroL == (OneL << 256));

    // String Constructor and Copy Constructor
    BOOST_CHECK(arith_uint256("0x" + R1L.ToString()) == R1L);
    BOOST_CHECK(arith_uint256("0x" + R2L.ToString()) == R2L);
    BOOST_CHECK(arith_uint256("0x" + ZeroL.ToString()) == ZeroL);
    BOOST_CHECK(arith_uint256("0x" + OneL.ToString()) == OneL);
    BOOST_CHECK(arith_uint256("0x" + MaxL.ToString()) == MaxL);
    BOOST_CHECK(arith_uint256(R1L.ToString()) == R1L);
    BOOST_CHECK(arith_uint256("   0x" + R1L.ToString() + "   ") == R1L);
    BOOST_CHECK(arith_uint256("") == ZeroL);
    BOOST_CHECK(R1L == arith_uint256(R1ArrayHex)); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    BOOST_CHECK(arith_uint256(R1L) == R1L);
    BOOST_CHECK((arith_uint256(R1L ^ R2L) ^ R2L) == R1L);
    BOOST_CHECK(arith_uint256(ZeroL) == ZeroL);
    BOOST_CHECK(arith_uint256(OneL) == OneL);

    // uint64_t constructor
    BOOST_CHECK((R1L & arith_uint256("0xffffffffffffffff")) ==
                arith_uint256(R1LLow64));
    BOOST_CHECK(ZeroL == arith_uint256(0));
    BOOST_CHECK(OneL == arith_uint256(1));
    BOOST_CHECK(arith_uint256("0xffffffffffffffff") ==
                arith_uint256(0xffffffffffffffffULL));

    // Assignment (from base_uint)
    arith_uint256 tmpL = ~ZeroL;
    BOOST_CHECK(tmpL == ~ZeroL);
    tmpL = ~OneL;
    BOOST_CHECK(tmpL == ~OneL);
    tmpL = ~R1L;
    BOOST_CHECK(tmpL == ~R1L);
    tmpL = ~R2L;
    BOOST_CHECK(tmpL == ~R2L);
    tmpL = ~MaxL;
    BOOST_CHECK(tmpL == ~MaxL);
}

void shiftArrayRight(uint8_t* to,
                     const std::span<const uint8_t> from,
                     unsigned int bitsToShift)
{
    for(unsigned int T = 0; T < from.size(); ++T)
    {
        unsigned int F = (T + bitsToShift / 8);
        if(F < from.size())
            to[T] = from[F] >> (bitsToShift % 8);
        else
            to[T] = 0; 
        if(F + 1 < from.size())
            to[T] |= from[(F + 1)] << (8 - bitsToShift % 8);
    }
}

void shiftArrayLeft(uint8_t* to,
                    const std::span<const uint8_t> from,
                    unsigned int bitsToShift)
{
    for(unsigned int T = 0; T < from.size(); ++T)
    {
        if(T >= bitsToShift / 8)
        {
            unsigned int F = T - bitsToShift / 8;
            to[T] = from[F] << (bitsToShift % 8);
            if(T >= bitsToShift / 8 + 1)
                to[T] |= from[F - 1] >> (8 - bitsToShift % 8);
        }
        else
        {
            to[T] = 0;
        }
    }
}

BOOST_AUTO_TEST_CASE(shifts) // "<<"  ">>"  "<<="  ">>="
{
    std::array<uint8_t, 32> TmpArray{};
    arith_uint256 TmpL;
    for (unsigned int i = 0; i < 256; ++i)
    {
        shiftArrayLeft(TmpArray.data(), OneArray, i);
        BOOST_CHECK(arith_uint256V(std::vector<uint8_t>(TmpArray.begin(),
                                                        TmpArray.end())) == (OneL << i));
        TmpL = OneL;
        TmpL <<= i;
        BOOST_CHECK(TmpL == (OneL << i));
        BOOST_CHECK((HalfL >> (255 - i)) == (OneL << i));
        TmpL = HalfL;
        TmpL >>= (255 - i);
        BOOST_CHECK(TmpL == (OneL << i));

        shiftArrayLeft(TmpArray.data(), R1Array, i);
        BOOST_CHECK(arith_uint256V(std::vector<uint8_t>(TmpArray.begin(),
                                                        TmpArray.end())) == (R1L << i));
        TmpL = R1L;
        TmpL <<= i;
        BOOST_CHECK(TmpL == (R1L << i));

        shiftArrayRight(TmpArray.data(), R1Array, i);
        BOOST_CHECK(arith_uint256V(std::vector<uint8_t>(TmpArray.begin(),
                                                        TmpArray.end())) == (R1L >> i));
        TmpL = R1L;
        TmpL >>= i;
        BOOST_CHECK(TmpL == (R1L >> i));

        shiftArrayLeft(TmpArray.data(), MaxArray, i);
        BOOST_CHECK(arith_uint256V(std::vector<uint8_t>(TmpArray.begin(),
                                                        TmpArray.end())) == (MaxL << i));
        TmpL = MaxL;
        TmpL <<= i;
        BOOST_CHECK(TmpL == (MaxL << i));

        shiftArrayRight(TmpArray.data(), MaxArray, i);
        BOOST_CHECK(arith_uint256V(std::vector<uint8_t>(TmpArray.begin(),
                                                        TmpArray.end())) == (MaxL >> i));
        TmpL = MaxL;
        TmpL >>= i;
        BOOST_CHECK(TmpL == (MaxL >> i));
    }
    arith_uint256 c1L = arith_uint256(0x0123456789abcdefULL);
    arith_uint256 c2L = c1L << 128;
    for (unsigned int i = 0; i < 128; ++i) {
        BOOST_CHECK((c1L << i) == (c2L >> (128 - i)));
    }
    for (unsigned int i = 128; i < 256; ++i) {
        BOOST_CHECK((c1L << i) == (c2L << (i - 128)));
    }
}

BOOST_AUTO_TEST_CASE(unaryOperators) // !    ~    -
{
    BOOST_CHECK(!ZeroL);
    BOOST_CHECK(!(!OneL));
    for (unsigned int i = 0; i < 256; ++i)
        BOOST_CHECK(!(!(OneL << i)));
    BOOST_CHECK(!(!R1L));
    BOOST_CHECK(!(!MaxL));

    BOOST_CHECK(~ZeroL == MaxL);

    std::array<uint8_t, 32> TmpArray{};
    std::ranges::transform(R1Array, TmpArray.begin(),
                           [](uint8_t c) { return ~c; });

    BOOST_CHECK(arith_uint256V(std::vector<uint8_t>(TmpArray.begin(), TmpArray.end())) ==
                (~R1L));

    BOOST_CHECK(-ZeroL == ZeroL);
    BOOST_CHECK(-R1L == (~R1L) + 1);
    for (unsigned int i = 0; i < 256; ++i)
        BOOST_CHECK(-(OneL << i) == (MaxL << i));
}

// Check if doing _A_ _OP_ _B_ results in the same as applying _OP_ onto each
// element of Aarray and Barray, and then converting the result into a
// arith_uint256.
#define CHECKBITWISEOPERATOR(_A_, _B_, _OP_)                                             \
    for(unsigned int i = 0; i < 32; ++i)                                                 \
    {                                                                                    \
        TmpArray[i] = _A_##Array[i] _OP_ _B_##Array[i];                                  \
    }                                                                                    \
    BOOST_CHECK(                                                                         \
        arith_uint256V(std::vector<uint8_t>(TmpArray.begin(), TmpArray.end())) ==        \
        (_A_##L _OP_ _B_##L));

#define CHECKASSIGNMENTOPERATOR(_A_, _B_, _OP_)                                \
    TmpL = _A_##L;                                                             \
    TmpL _OP_## = _B_##L;                                                      \
    BOOST_CHECK(TmpL == (_A_##L _OP_ _B_##L));

BOOST_AUTO_TEST_CASE(bitwiseOperators) {
    std::array<uint8_t, 32> TmpArray{};
    CHECKBITWISEOPERATOR(R1, R2, |)
    CHECKBITWISEOPERATOR(R1, R2, ^)
    CHECKBITWISEOPERATOR(R1, R2, &)
    CHECKBITWISEOPERATOR(R1, Zero, |)
    CHECKBITWISEOPERATOR(R1, Zero, ^)
    CHECKBITWISEOPERATOR(R1, Zero, &)
    CHECKBITWISEOPERATOR(R1, Max, |)
    CHECKBITWISEOPERATOR(R1, Max, ^)
    CHECKBITWISEOPERATOR(R1, Max, &)
    CHECKBITWISEOPERATOR(Zero, R1, |)
    CHECKBITWISEOPERATOR(Zero, R1, ^)
    CHECKBITWISEOPERATOR(Zero, R1, &)
    CHECKBITWISEOPERATOR(Max, R1, |)
    CHECKBITWISEOPERATOR(Max, R1, ^)
    CHECKBITWISEOPERATOR(Max, R1, &)
    arith_uint256 TmpL;
    CHECKASSIGNMENTOPERATOR(R1, R2, |)
    CHECKASSIGNMENTOPERATOR(R1, R2, ^)
    CHECKASSIGNMENTOPERATOR(R1, R2, &)
    CHECKASSIGNMENTOPERATOR(R1, Zero, |)
    CHECKASSIGNMENTOPERATOR(R1, Zero, ^)
    CHECKASSIGNMENTOPERATOR(R1, Zero, &)
    CHECKASSIGNMENTOPERATOR(R1, Max, |)
    CHECKASSIGNMENTOPERATOR(R1, Max, ^)
    CHECKASSIGNMENTOPERATOR(R1, Max, &)
    CHECKASSIGNMENTOPERATOR(Zero, R1, |)
    CHECKASSIGNMENTOPERATOR(Zero, R1, ^)
    CHECKASSIGNMENTOPERATOR(Zero, R1, &)
    CHECKASSIGNMENTOPERATOR(Max, R1, |)
    CHECKASSIGNMENTOPERATOR(Max, R1, ^)
    CHECKASSIGNMENTOPERATOR(Max, R1, &)

    uint64_t Tmp64 = 0xe1db685c9a0b47a2ULL;
    TmpL = R1L;
    TmpL |= Tmp64;
    BOOST_CHECK(TmpL == (R1L | arith_uint256(Tmp64)));
    TmpL = R1L;
    TmpL |= 0;
    BOOST_CHECK(TmpL == R1L);
    TmpL ^= 0;
    BOOST_CHECK(TmpL == R1L);
    TmpL ^= Tmp64;
    BOOST_CHECK(TmpL == (R1L ^ arith_uint256(Tmp64)));
}

BOOST_AUTO_TEST_CASE(comparison) // <= >= < >
{
    arith_uint256 TmpL;
    for(int i = 0; i < 256; ++i)
    {
        TmpL = OneL << i;
        BOOST_CHECK(TmpL >= ZeroL && TmpL > ZeroL && ZeroL < TmpL &&
                    ZeroL <= TmpL);
        BOOST_CHECK(TmpL >= 0 && TmpL > 0 && 0 < TmpL && 0 <= TmpL);
        TmpL |= R1L;
        BOOST_CHECK(TmpL >= R1L);
        BOOST_CHECK((TmpL == R1L) != (TmpL > R1L));
        BOOST_CHECK((TmpL == R1L) || !(TmpL <= R1L));
        BOOST_CHECK(R1L <= TmpL);
        BOOST_CHECK((R1L == TmpL) != (R1L < TmpL));
        BOOST_CHECK((TmpL == R1L) || !(R1L >= TmpL));
        BOOST_CHECK(!(TmpL < R1L));
        BOOST_CHECK(!(R1L > TmpL));
    }
}

BOOST_AUTO_TEST_CASE(plusMinus) {
    arith_uint256 TmpL = 0;
    BOOST_CHECK(R1L + R2L == arith_uint256(R1LplusR2L)); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    TmpL += R1L;
    BOOST_CHECK(TmpL == R1L);
    TmpL += R2L;
    BOOST_CHECK(TmpL == R1L + R2L);
    BOOST_CHECK(OneL + MaxL == ZeroL);
    BOOST_CHECK(MaxL + OneL == ZeroL);
    for(int i = 1; i < 256; ++i)
    {
        BOOST_CHECK((MaxL >> i) + OneL == (HalfL >> (i - 1)));
        BOOST_CHECK(OneL + (MaxL >> i) == (HalfL >> (i - 1)));
        TmpL = (MaxL >> i);
        TmpL += OneL;
        BOOST_CHECK(TmpL == (HalfL >> (i - 1)));
        TmpL = (MaxL >> i);
        TmpL += 1;
        BOOST_CHECK(TmpL == (HalfL >> (i - 1)));
        TmpL = (MaxL >> i);
        BOOST_CHECK(TmpL++ == (MaxL >> i));
        BOOST_CHECK(TmpL == (HalfL >> (i - 1)));
    }
    BOOST_CHECK(arith_uint256(0xbedc77e27940a7ULL) + 0xee8d836fce66fbULL ==
                arith_uint256(0xbedc77e27940a7ULL + 0xee8d836fce66fbULL));
    TmpL = arith_uint256(0xbedc77e27940a7ULL);
    TmpL += 0xee8d836fce66fbULL;
    BOOST_CHECK(TmpL ==
                arith_uint256(0xbedc77e27940a7ULL + 0xee8d836fce66fbULL));
    TmpL -= 0xee8d836fce66fbULL;
    BOOST_CHECK(TmpL == 0xbedc77e27940a7ULL);
    TmpL = R1L;
    BOOST_CHECK(++TmpL == R1L + 1);

    BOOST_CHECK(R1L - (-R2L) == R1L + R2L);
    BOOST_CHECK(R1L - (-OneL) == R1L + OneL);
    BOOST_CHECK(R1L - OneL == R1L + (-OneL));
    for(int i = 1; i < 256; ++i)
    {
        BOOST_CHECK((MaxL >> i) - (-OneL) == (HalfL >> (i - 1)));
        BOOST_CHECK((HalfL >> (i - 1)) - OneL == (MaxL >> i));
        TmpL = (HalfL >> (i - 1));
        BOOST_CHECK(TmpL-- == (HalfL >> (i - 1)));
        BOOST_CHECK(TmpL == (MaxL >> i));
        TmpL = (HalfL >> (i - 1));
        BOOST_CHECK(--TmpL == (MaxL >> i));
    }
    TmpL = R1L;
    BOOST_CHECK(--TmpL == R1L - 1);
}

BOOST_AUTO_TEST_CASE(multiply) {
    BOOST_CHECK(
        (R1L * R1L).ToString() ==
        "62a38c0486f01e45879d7910a7761bf30d5237e9873f9bff3642a732c4d84f10");
    BOOST_CHECK(
        (R1L * R2L).ToString() ==
        "de37805e9986996cfba76ff6ba51c008df851987d9dd323f0e5de07760529c40");
    BOOST_CHECK((R1L * ZeroL) == ZeroL);
    BOOST_CHECK((R1L * OneL) == R1L);
    BOOST_CHECK((R1L * MaxL) == -R1L);
    BOOST_CHECK((R2L * R1L) == (R1L * R2L));
    BOOST_CHECK(
        (R2L * R2L).ToString() ==
        "ac8c010096767d3cae5005dec28bb2b45a1d85ab7996ccd3e102a650f74ff100");
    BOOST_CHECK((R2L * ZeroL) == ZeroL);
    BOOST_CHECK((R2L * OneL) == R2L);
    BOOST_CHECK((R2L * MaxL) == -R2L);

    BOOST_CHECK(MaxL * MaxL == OneL);

    BOOST_CHECK((R1L * 0) == 0);
    BOOST_CHECK((R1L * 1) == R1L);
    BOOST_CHECK(
        (R1L * 3).ToString() ==
        "7759b1c0ed14047f961ad09b20ff83687876a0181a367b813634046f91def7d4");
    BOOST_CHECK(
        (R2L * 0x87654321UL).ToString() ==
        "23f7816e30c4ae2017257b7a0fa64d60402f5234d46e746b61c960d09a26d070");
}

BOOST_AUTO_TEST_CASE(divide) {
    arith_uint256 D1L("AD7133AC1977FA2B7");
    arith_uint256 D2L("ECD751716");
    BOOST_CHECK(
        (R1L / D1L).ToString() ==
        "00000000000000000b8ac01106981635d9ed112290f8895545a7654dde28fb3a");
    BOOST_CHECK(
        (R1L / D2L).ToString() ==
        "000000000873ce8efec5b67150bad3aa8c5fcb70e947586153bf2cec7c37c57a");
    BOOST_CHECK(R1L / OneL == R1L);
    BOOST_CHECK(R1L / MaxL == ZeroL);
    BOOST_CHECK(MaxL / R1L == 2);
    BOOST_CHECK_THROW(R1L / ZeroL, uint_error);
    BOOST_CHECK(
        (R2L / D1L).ToString() ==
        "000000000000000013e1665895a1cc981de6d93670105a6b3ec3b73141b3a3c5");
    BOOST_CHECK(
        (R2L / D2L).ToString() ==
        "000000000e8f0abe753bb0afe2e9437ee85d280be60882cf0bd1aaf7fa3cc2c4");
    BOOST_CHECK(R2L / OneL == R2L);
    BOOST_CHECK(R2L / MaxL == ZeroL);
    BOOST_CHECK(MaxL / R2L == 1);
    BOOST_CHECK_THROW(R2L / ZeroL, uint_error);
}

bool almostEqual(double d1, double d2) {
    return fabs(d1 - d2) <=
           4 * fabs(d1) * std::numeric_limits<double>::epsilon();
}

BOOST_AUTO_TEST_CASE(methods) // GetHex SetHex size() GetLow64 GetSerializeSize,
                              // Serialize, Unserialize
{
    BOOST_CHECK(R1L.GetHex() == R1L.ToString());
    BOOST_CHECK(R2L.GetHex() == R2L.ToString());
    BOOST_CHECK(OneL.GetHex() == OneL.ToString());
    BOOST_CHECK(MaxL.GetHex() == MaxL.ToString());
    arith_uint256 TmpL(R1L);
    BOOST_CHECK(TmpL == R1L);
    TmpL.SetHex(R2L.ToString());
    BOOST_CHECK(TmpL == R2L);
    TmpL.SetHex(ZeroL.ToString());
    BOOST_CHECK(TmpL == 0);
    TmpL.SetHex(HalfL.ToString());
    BOOST_CHECK(TmpL == HalfL);

    TmpL.SetHex(R1L.ToString());
    BOOST_CHECK(R1L.size() == 32);
    BOOST_CHECK(R2L.size() == 32);
    BOOST_CHECK(ZeroL.size() == 32);
    BOOST_CHECK(MaxL.size() == 32);
    BOOST_CHECK(R1L.GetLow64() == R1LLow64);
    BOOST_CHECK(HalfL.GetLow64() == 0x0000000000000000ULL);
    BOOST_CHECK(OneL.GetLow64() == 0x0000000000000001ULL);

    for (unsigned int i = 0; i < 255; ++i) {
        BOOST_CHECK((OneL << i).getdouble() == ldexp(1.0, i));
    }
    BOOST_CHECK(ZeroL.getdouble() == 0.0);
    for (int i = 256; i > 53; --i)
        BOOST_CHECK(
            almostEqual((R1L >> (256 - i)).getdouble(), ldexp(R1Ldouble, i)));
    uint64_t R1L64part = (R1L >> 192).GetLow64();
    for (int i = 53; i > 0;
         --i) // doubles can store all integers in {0,...,2^54-1} exactly
    {
        BOOST_CHECK((R1L >> (256 - i)).getdouble() ==
                    (double)(R1L64part >> (64 - i)));
    }
}

BOOST_AUTO_TEST_CASE(setcompact_test)
{
    using input = std::tuple<uint32_t, bool, bool, std::string>;
    const std::vector<input> inputs
    { 
        std::make_tuple(0x00123456, false, false, "0"),
        std::make_tuple(0x01123456, false, false, "12"),
        std::make_tuple(0x02123456, false, false, "1234"),
        std::make_tuple(0x03123456, false, false, "123456"),
        std::make_tuple(0x04123456, false, false, "12345600"),
        std::make_tuple(0x20123456, false, false, "12345600000000000000000000000000"
                                                  "00000000000000000000000000000000"),
        std::make_tuple(0x21123456, false, true,  "34560000000000000000000000000000"
                                                  "00000000000000000000000000000000"),

        std::make_tuple(0x00923456, false, false, "0"),
        std::make_tuple(0x01923456, true, false, "12"),
        std::make_tuple(0x02923456, true, false, "1234"),
        std::make_tuple(0x03923456, true, false, "123456"),
        std::make_tuple(0x04923456, true, false, "12345600"),
        std::make_tuple(0x20923456, true, false, "12345600000000000000000000000000"
                                                 "00000000000000000000000000000000"),
        std::make_tuple(0x21923456, true, true,  "34560000000000000000000000000000"
                                                 "00000000000000000000000000000000"),
    }; 

    for(const auto& [input, exp_negative, exp_overflow, exp] : inputs)
    {
        bool is_negative{};
        bool is_overflow{};
        arith_uint256 a;
        const auto b = a.SetCompact(input, &is_negative, &is_overflow);
        BOOST_CHECK_EQUAL(exp_negative, is_negative);
        BOOST_CHECK_EQUAL(exp_overflow, is_overflow);
        BOOST_CHECK_EQUAL(arith_uint256{exp}, a);
        BOOST_CHECK_EQUAL(a, b);
    }
}

BOOST_AUTO_TEST_CASE(bignum_SetCompact) {
    arith_uint256 num;
    bool fNegative; // NOLINT(cppcoreguidelines-init-variables)
    bool fOverflow; // NOLINT(cppcoreguidelines-init-variables)
    num.SetCompact(0, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x00123456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x01003456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x02000056, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x03000000, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x04000000, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x00923456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x01803456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x02800056, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x03800000, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x04800000, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x01123456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000000012");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0x01120000U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    // Make sure that we don't generate compacts with the 0x00800000 bit set
    num = 0x80;
    BOOST_CHECK_EQUAL(num.GetCompact(), 0x02008000U);

    num.SetCompact(0x01fedcba, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "000000000000000000000000000000000000000000000000000000000000007e");
    BOOST_CHECK_EQUAL(num.GetCompact(true), 0x01fe0000U);
    BOOST_CHECK_EQUAL(fNegative, true);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x02123456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000001234");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0x02123400U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x03123456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000000123456");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0x03123456U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x04123456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000012345600");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0x04123456U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x04923456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000012345600");
    BOOST_CHECK_EQUAL(num.GetCompact(true), 0x04923456U);
    BOOST_CHECK_EQUAL(fNegative, true);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x05009234, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "0000000000000000000000000000000000000000000000000000000092340000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0x05009234U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0x20123456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(
        num.GetHex(),
        "1234560000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(num.GetCompact(), 0x20123456U);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, false);

    num.SetCompact(0xff123456, &fNegative, &fOverflow);
    BOOST_CHECK_EQUAL(fNegative, false);
    BOOST_CHECK_EQUAL(fOverflow, true);
}

BOOST_AUTO_TEST_CASE(
    getmaxcoverage) // some more tests just to get 100% coverage
{
    // ~R1L give a base_uint<256>
    BOOST_CHECK((~~R1L >> 10) == (R1L >> 10));
    BOOST_CHECK((~~R1L << 10) == (R1L << 10));
    BOOST_CHECK(!(~~R1L < R1L));
    BOOST_CHECK(~~R1L <= R1L);
    BOOST_CHECK(!(~~R1L > R1L));
    BOOST_CHECK(~~R1L >= R1L);
    BOOST_CHECK(!(R1L < ~~R1L));
    BOOST_CHECK(R1L <= ~~R1L);
    BOOST_CHECK(!(R1L > ~~R1L));
    BOOST_CHECK(R1L >= ~~R1L);

    BOOST_CHECK(~~R1L + R2L == R1L + ~~R2L);
    BOOST_CHECK(~~R1L - R2L == R1L - ~~R2L);
    BOOST_CHECK(~R1L != R1L);
    BOOST_CHECK(R1L != ~R1L);
    std::array<uint8_t, 32> TmpArray{};
    CHECKBITWISEOPERATOR(~R1, R2, |)
    CHECKBITWISEOPERATOR(~R1, R2, ^)
    CHECKBITWISEOPERATOR(~R1, R2, &)
    CHECKBITWISEOPERATOR(R1, ~R2, |)
    CHECKBITWISEOPERATOR(R1, ~R2, ^)
    CHECKBITWISEOPERATOR(R1, ~R2, &)
}

BOOST_AUTO_TEST_SUITE_END()
