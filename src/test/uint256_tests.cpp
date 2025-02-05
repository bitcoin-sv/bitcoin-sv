// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "arith_uint256.h"
#include "test/test_bitcoin.h"
#include "uint256.h"
#include "version.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(uint256_tests, BasicTestingSetup)

constexpr std::array<uint8_t, 32> R1Array = {
    0x9c, 0x52, 0x4a, 0xdb, 0xcf, 0x56, 0x11, 0x12, 0x2b, 0x29, 0x12,
    0x5e, 0x5d, 0x35, 0xd2, 0xd2, 0x22, 0x81, 0xaa, 0xb5, 0x33, 0xf0,
    0x08, 0x32, 0xd5, 0x56, 0xb1, 0xf9, 0xea, 0xe5, 0x1d, 0x7d};
constexpr char R1ArrayHex[] =
    "7D1DE5EAF9B156D53208F033B5AA8122D2d2355d5e12292b121156cfdb4a529c";
const uint256 R1L = uint256(std::vector<uint8_t>(R1Array.begin(), R1Array.end()));
const uint160 R1S = uint160(std::vector<uint8_t>(R1Array.begin(), R1Array.begin() + 20));

constexpr std::array<uint8_t, 32> R2Array = {
    0x70, 0x32, 0x1d, 0x7c, 0x47, 0xa5, 0x6b, 0x40, 0x26, 0x7e, 0x0a,
    0xc3, 0xa6, 0x9c, 0xb6, 0xbf, 0x13, 0x30, 0x47, 0xa3, 0x19, 0x2d,
    0xda, 0x71, 0x49, 0x13, 0x72, 0xf0, 0xb4, 0xca, 0x81, 0xd7};
const uint256 R2L = uint256(std::vector<uint8_t>(R2Array.begin(), R2Array.end()));
const uint160 R2S = uint160(std::vector<uint8_t>(R2Array.begin(), R2Array.begin() + 20));

constexpr std::array<uint8_t, 32> ZeroArray{};
const uint256 ZeroL = uint256(std::vector<uint8_t>(ZeroArray.begin(), ZeroArray.end()));
const uint160 ZeroS = uint160(std::vector<uint8_t>(ZeroArray.begin(), ZeroArray.begin() + 20));

constexpr std::array OneArray = []{
    std::array<uint8_t, 32> a{};
    a[0] = 1;
    return a;
}();
const uint256 OneL = uint256(std::vector<uint8_t>(OneArray.begin(), OneArray.end()));
const uint160 OneS = uint160(std::vector<uint8_t>(OneArray.begin(), OneArray.begin() + 20));

constexpr std::array MaxArray = []{
    std::array<uint8_t, 32> a{};
    a.fill(0xff);
    return a;
}();

const uint256 MaxL = uint256(std::vector<uint8_t>(MaxArray.begin(), MaxArray.end()));
const uint160 MaxS = uint160(std::vector<uint8_t>(MaxArray.begin(), MaxArray.begin() + 20));

static std::string ArrayToString(const std::span<const uint8_t> a)
{
    std::stringstream Stream;
    Stream << std::hex;
    for (unsigned int i = 0; i < a.size(); ++i) {
        Stream << std::setw(2) << std::setfill('0')
               << (unsigned int)a[a.size() - i - 1];
    }
    return Stream.str();
}

// constructors, equality, inequality
BOOST_AUTO_TEST_CASE(basics) {
    BOOST_CHECK(1 == 0 + 1);
    // constructor uint256(vector<char>):
    BOOST_CHECK_EQUAL(R1L.ToString(), ArrayToString(R1Array));
    BOOST_CHECK_EQUAL(R1S.ToString(), ArrayToString(std::span{R1Array.begin(), 20}));
    BOOST_CHECK_EQUAL(R2L.ToString(), ArrayToString(R2Array));
    BOOST_CHECK_EQUAL(R2S.ToString(), ArrayToString(std::span{R2Array.begin(), 20}));
    BOOST_CHECK_EQUAL(ZeroL.ToString(), ArrayToString(ZeroArray));
    BOOST_CHECK_EQUAL(ZeroS.ToString(), ArrayToString(std::span{ZeroArray.begin(), 20}));
    BOOST_CHECK_EQUAL(OneL.ToString(), ArrayToString(OneArray));
    BOOST_CHECK_EQUAL(OneS.ToString(), ArrayToString(std::span{OneArray.begin(), 20}));
    BOOST_CHECK_EQUAL(MaxL.ToString(), ArrayToString(MaxArray));
    BOOST_CHECK_EQUAL(MaxS.ToString(), ArrayToString(std::span{MaxArray.begin(), 20}));
    BOOST_CHECK_NE(OneL.ToString(), ArrayToString(ZeroArray));
    BOOST_CHECK_NE(OneS.ToString(), ArrayToString(std::span{ZeroArray.begin(), 20}));

    // == and !=
    BOOST_CHECK(R1L != R2L && R1S != R2S);
    BOOST_CHECK(ZeroL != OneL && ZeroS != OneS);
    BOOST_CHECK(OneL != ZeroL && OneS != ZeroS);
    BOOST_CHECK(MaxL != ZeroL && MaxS != ZeroS);

    // String Constructor and Copy Constructor
    BOOST_CHECK(uint256S("0x" + R1L.ToString()) == R1L);
    BOOST_CHECK(uint256S("0x" + R2L.ToString()) == R2L);
    BOOST_CHECK(uint256S("0x" + ZeroL.ToString()) == ZeroL);
    BOOST_CHECK(uint256S("0x" + OneL.ToString()) == OneL);
    BOOST_CHECK(uint256S("0x" + MaxL.ToString()) == MaxL);
    BOOST_CHECK(uint256S(R1L.ToString()) == R1L);
    BOOST_CHECK(uint256S("   0x" + R1L.ToString() + "   ") == R1L);
    BOOST_CHECK(uint256S("") == ZeroL);
    BOOST_CHECK(R1L == uint256S(R1ArrayHex));
    BOOST_CHECK(uint256(R1L) == R1L);
    BOOST_CHECK(uint256(ZeroL) == ZeroL);
    BOOST_CHECK(uint256(OneL) == OneL);

    BOOST_CHECK(uint160S("0x" + R1S.ToString()) == R1S);
    BOOST_CHECK(uint160S("0x" + R2S.ToString()) == R2S);
    BOOST_CHECK(uint160S("0x" + ZeroS.ToString()) == ZeroS);
    BOOST_CHECK(uint160S("0x" + OneS.ToString()) == OneS);
    BOOST_CHECK(uint160S("0x" + MaxS.ToString()) == MaxS);
    BOOST_CHECK(uint160S(R1S.ToString()) == R1S);
    BOOST_CHECK(uint160S("   0x" + R1S.ToString() + "   ") == R1S);
    BOOST_CHECK(uint160S("") == ZeroS);
    BOOST_CHECK(R1S == uint160S(R1ArrayHex));

    BOOST_CHECK(uint160(R1S) == R1S);
    BOOST_CHECK(uint160(ZeroS) == ZeroS);
    BOOST_CHECK(uint160(OneS) == OneS);
}

// <= >= < >
BOOST_AUTO_TEST_CASE(comparison) {
    uint256 LastL;
    for (int i = 255; i >= 0; --i) {
        uint256 TmpL;
        *(TmpL.begin() + (i >> 3)) |= 1 << (7 - (i & 7)); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        BOOST_CHECK(LastL < TmpL);
        LastL = TmpL;
    }

    BOOST_CHECK(ZeroL < R1L);
    BOOST_CHECK(R2L < R1L);
    BOOST_CHECK(ZeroL < OneL);
    BOOST_CHECK(OneL < MaxL);
    BOOST_CHECK(R1L < MaxL);
    BOOST_CHECK(R2L < MaxL);

    uint160 LastS;
    for (int i = 159; i >= 0; --i) {
        uint160 TmpS;
        *(TmpS.begin() + (i >> 3)) |= 1 << (7 - (i & 7)); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        BOOST_CHECK(LastS < TmpS);
        LastS = TmpS;
    }
    BOOST_CHECK(ZeroS < R1S);
    BOOST_CHECK(R2S < R1S);
    BOOST_CHECK(ZeroS < OneS);
    BOOST_CHECK(OneS < MaxS);
    BOOST_CHECK(R1S < MaxS);
    BOOST_CHECK(R2S < MaxS);
}

// GetHex SetHex begin() end() size() GetLow64 GetSerializeSize, Serialize,
// Unserialize
BOOST_AUTO_TEST_CASE(methods) {
    BOOST_CHECK(R1L.GetHex() == R1L.ToString());
    BOOST_CHECK(R2L.GetHex() == R2L.ToString());
    BOOST_CHECK(OneL.GetHex() == OneL.ToString());
    BOOST_CHECK(MaxL.GetHex() == MaxL.ToString());
    uint256 TmpL(R1L);
    BOOST_CHECK(TmpL == R1L);
    TmpL.SetHex(R2L.ToString());
    BOOST_CHECK(TmpL == R2L);
    TmpL.SetHex(ZeroL.ToString());
    BOOST_CHECK(TmpL == uint256());

    TmpL.SetHex(R1L.ToString());
    BOOST_CHECK(memcmp(R1L.begin(), R1Array.data(), 32) == 0);
    BOOST_CHECK(memcmp(TmpL.begin(), R1Array.data(), 32) == 0);
    BOOST_CHECK(memcmp(R2L.begin(), R2Array.data(), 32) == 0);
    BOOST_CHECK(memcmp(ZeroL.begin(), ZeroArray.data(), 32) == 0);
    BOOST_CHECK(memcmp(OneL.begin(), OneArray.data(), 32) == 0);
    BOOST_CHECK(R1L.size() == sizeof(R1L));
    BOOST_CHECK(sizeof(R1L) == 32);
    BOOST_CHECK(R1L.size() == 32);
    BOOST_CHECK(R2L.size() == 32);
    BOOST_CHECK(ZeroL.size() == 32);
    BOOST_CHECK(MaxL.size() == 32);
    BOOST_CHECK(R1L.begin() + 32 == R1L.end());
    BOOST_CHECK(R2L.begin() + 32 == R2L.end());
    BOOST_CHECK(OneL.begin() + 32 == OneL.end());
    BOOST_CHECK(MaxL.begin() + 32 == MaxL.end());
    BOOST_CHECK(TmpL.begin() + 32 == TmpL.end());
    BOOST_CHECK(GetSerializeSize(R1L, 0, PROTOCOL_VERSION) == 32);
    BOOST_CHECK(GetSerializeSize(ZeroL, 0, PROTOCOL_VERSION) == 32);

    CDataStream ss(0, PROTOCOL_VERSION);
    ss << R1L;
    BOOST_CHECK(ss.str() == std::string(R1Array.begin(), R1Array.end()));
    ss >> TmpL;
    BOOST_CHECK(R1L == TmpL);
    ss.clear();
    ss << ZeroL;
    BOOST_CHECK(ss.str() == std::string(ZeroArray.begin(), ZeroArray.end()));
    ss >> TmpL;
    BOOST_CHECK(ZeroL == TmpL);
    ss.clear();
    ss << MaxL;
    BOOST_CHECK(ss.str() == std::string(MaxArray.begin(), MaxArray.end()));
    ss >> TmpL;
    BOOST_CHECK(MaxL == TmpL);
    ss.clear();

    BOOST_CHECK(R1S.GetHex() == R1S.ToString());
    BOOST_CHECK(R2S.GetHex() == R2S.ToString());
    BOOST_CHECK(OneS.GetHex() == OneS.ToString());
    BOOST_CHECK(MaxS.GetHex() == MaxS.ToString());
    uint160 TmpS(R1S);
    BOOST_CHECK(TmpS == R1S);
    TmpS.SetHex(R2S.ToString());
    BOOST_CHECK(TmpS == R2S);
    TmpS.SetHex(ZeroS.ToString());
    BOOST_CHECK(TmpS == uint160());

    TmpS.SetHex(R1S.ToString());
    BOOST_CHECK(memcmp(R1S.begin(), R1Array.data(), 20) == 0);
    BOOST_CHECK(memcmp(TmpS.begin(), R1Array.data(), 20) == 0);
    BOOST_CHECK(memcmp(R2S.begin(), R2Array.data(), 20) == 0);
    BOOST_CHECK(memcmp(ZeroS.begin(), ZeroArray.data(), 20) == 0);
    BOOST_CHECK(memcmp(OneS.begin(), OneArray.data(), 20) == 0);
    BOOST_CHECK(R1S.size() == sizeof(R1S));
    BOOST_CHECK(sizeof(R1S) == 20);
    BOOST_CHECK(R1S.size() == 20);
    BOOST_CHECK(R2S.size() == 20);
    BOOST_CHECK(ZeroS.size() == 20);
    BOOST_CHECK(MaxS.size() == 20);
    BOOST_CHECK(R1S.begin() + 20 == R1S.end());
    BOOST_CHECK(R2S.begin() + 20 == R2S.end());
    BOOST_CHECK(OneS.begin() + 20 == OneS.end());
    BOOST_CHECK(MaxS.begin() + 20 == MaxS.end());
    BOOST_CHECK(TmpS.begin() + 20 == TmpS.end());
    BOOST_CHECK(GetSerializeSize(R1S, 0, PROTOCOL_VERSION) == 20);
    BOOST_CHECK(GetSerializeSize(ZeroS, 0, PROTOCOL_VERSION) == 20);

    ss << R1S;
    BOOST_CHECK(ss.str() == std::string(R1Array.begin(), R1Array.begin() + 20));
    ss >> TmpS;
    BOOST_CHECK(R1S == TmpS);
    ss.clear();
    ss << ZeroS;
    BOOST_CHECK(ss.str() == std::string(ZeroArray.begin(), ZeroArray.begin() + 20));
    ss >> TmpS;
    BOOST_CHECK(ZeroS == TmpS);
    ss.clear();
    ss << MaxS;
    BOOST_CHECK(ss.str() == std::string(MaxArray.begin(), MaxArray.begin() + 20));
    ss >> TmpS;
    BOOST_CHECK(MaxS == TmpS);
    ss.clear();
}

BOOST_AUTO_TEST_CASE(conversion) {
    BOOST_CHECK(ArithToUint256(UintToArith256(ZeroL)) == ZeroL);
    BOOST_CHECK(ArithToUint256(UintToArith256(OneL)) == OneL);
    BOOST_CHECK(ArithToUint256(UintToArith256(R1L)) == R1L);
    BOOST_CHECK(ArithToUint256(UintToArith256(R2L)) == R2L);
    BOOST_CHECK(UintToArith256(ZeroL) == 0);
    BOOST_CHECK(UintToArith256(OneL) == 1);
    BOOST_CHECK(ArithToUint256(0) == ZeroL);
    BOOST_CHECK(ArithToUint256(1) == OneL);
    BOOST_CHECK(arith_uint256(R1L.GetHex()) == UintToArith256(R1L));
    BOOST_CHECK(arith_uint256(R2L.GetHex()) == UintToArith256(R2L));
    BOOST_CHECK(R1L.GetHex() == UintToArith256(R1L).GetHex());
    BOOST_CHECK(R2L.GetHex() == UintToArith256(R2L).GetHex());
}

BOOST_AUTO_TEST_SUITE_END()
