// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/sighashtype.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <set>

// default constructor
static_assert(SigHashType{}.isDefined());

static_assert(!SigHashType{}.hasRelax());
static_assert(SigHashType{}.withRelax().hasRelax());
static_assert(!SigHashType{}.withRelax(false).hasRelax());

static_assert(!SigHashType{}.hasForkId());
static_assert(SigHashType{}.withForkId().hasForkId());
static_assert(!SigHashType{}.withForkId(false).hasForkId());

static_assert(!SigHashType{}.hasAnyoneCanPay());
static_assert(SigHashType{}.withAnyoneCanPay().hasAnyoneCanPay());
static_assert(!SigHashType{}.withAnyoneCanPay(false).hasAnyoneCanPay());

static_assert(SigHashType{}.getBaseType() == BaseSigHashType::ALL);

static_assert(SigHashType{}.getRawSigHashType() == SIGHASH_ALL);

static_assert(SigHashType{}.getForkValue() == 0);

// constructor with argument
static_assert(!SigHashType{0}.isDefined());
static_assert(SigHashType{1}.isDefined());
static_assert(SigHashType{2}.isDefined());
static_assert(SigHashType{3}.isDefined());
static_assert(!SigHashType{4}.isDefined());
static_assert(!SigHashType{0x11}.isDefined());
static_assert(SigHashType{0x21}.isDefined());
static_assert(SigHashType{0x41}.isDefined());
static_assert(SigHashType{0x81}.isDefined());

static_assert(SigHashType{0x20}.hasRelax());
static_assert(SigHashType{0x20}.withRelax().hasRelax());
static_assert(!SigHashType{0x20}.withRelax(false).hasRelax());

static_assert(SigHashType{0x40}.hasForkId());
static_assert(SigHashType{0x40}.withForkId().hasForkId());
static_assert(!SigHashType{0x40}.withForkId(false).hasForkId());

static_assert(SigHashType{0x12345678}.getForkValue() == 0x123456);

static_assert(SigHashType{0x80}.hasAnyoneCanPay());
static_assert(SigHashType{0x80}.withAnyoneCanPay().hasAnyoneCanPay());
static_assert(!SigHashType{0x80}.withAnyoneCanPay(false).hasAnyoneCanPay());

static_assert(SigHashType{0xe0}.getBaseType() == BaseSigHashType::UNSUPPORTED);
static_assert(SigHashType{0xe1}.getBaseType() == BaseSigHashType::ALL);
static_assert(SigHashType{0xe2}.getBaseType() == BaseSigHashType::NONE);
static_assert(SigHashType{0xe3}.getBaseType() == BaseSigHashType::SINGLE);

static_assert(SigHashType{0x81}.getRawSigHashType() == (SIGHASH_ANYONECANPAY | SIGHASH_ALL));
static_assert(SigHashType{0x42}.getRawSigHashType() == (SIGHASH_FORKID | SIGHASH_NONE));
static_assert(SigHashType{0x23}.getRawSigHashType() == (SIGHASH_RELAX | SIGHASH_SINGLE));

BOOST_FIXTURE_TEST_SUITE(sighashtype_tests, BasicTestingSetup)

static void CheckSigHashType(SigHashType t,
                             BaseSigHashType baseType,
                             bool isDefined,
                             uint32_t forkValue,
                             bool hasRelax,
                             bool hasForkId,
                             bool hasAnyoneCanPay)
{
    BOOST_CHECK(t.getBaseType() == baseType);
    BOOST_CHECK_EQUAL(t.isDefined(), isDefined);
    BOOST_CHECK_EQUAL(t.getForkValue(), forkValue);
    BOOST_CHECK_EQUAL(t.hasRelax(), hasRelax);
    BOOST_CHECK_EQUAL(t.hasForkId(), hasForkId);
    BOOST_CHECK_EQUAL(t.hasAnyoneCanPay(), hasAnyoneCanPay);
}

BOOST_AUTO_TEST_CASE(sighash_construction_test)
{
    // Check default values.
    CheckSigHashType(SigHashType(), BaseSigHashType::ALL, true, 0, false, false, false);

    // Check all possible permutations.
    std::set<BaseSigHashType> baseTypes{
        BaseSigHashType::UNSUPPORTED, BaseSigHashType::ALL,
        BaseSigHashType::NONE, BaseSigHashType::SINGLE};
    std::set<uint32_t> forkValues{0, 1, 0x123456, 0xfedcba, 0xffffff};
    std::set<bool> relaxFlagValues{false, true};
    std::set<bool> forkIdFlagValues{false, true};
    std::set<bool> anyoneCanPayFlagValues{false, true};

    for (BaseSigHashType baseType : baseTypes) {
        for (uint32_t forkValue : forkValues) {
            for (bool hasRelax : relaxFlagValues) {
                for (bool hasForkId : forkIdFlagValues) {
                    for (bool hasAnyoneCanPay : anyoneCanPayFlagValues) {
                        const SigHashType t =
                            SigHashType()
                                .withBaseType(baseType)
                                .withForkValue(forkValue)
                                .withRelax(hasRelax)
                                .withForkId(hasForkId)
                                .withAnyoneCanPay(hasAnyoneCanPay);

                        bool isDefined = baseType != BaseSigHashType::UNSUPPORTED;
                        CheckSigHashType(t, baseType, isDefined, forkValue,
                                         hasRelax, hasForkId, hasAnyoneCanPay);

                        // Also check all possible alterations.
                        CheckSigHashType(t.withRelax(hasRelax), baseType,
                                         isDefined, forkValue, hasRelax, hasForkId,
                                         hasAnyoneCanPay);
                        CheckSigHashType(t.withRelax(!hasRelax), baseType,
                                         isDefined, forkValue, !hasRelax, hasForkId,
                                         hasAnyoneCanPay);
                        CheckSigHashType(t.withForkId(hasForkId), baseType,
                                         isDefined, forkValue, hasRelax, hasForkId,
                                         hasAnyoneCanPay);
                        CheckSigHashType(t.withForkId(!hasForkId), baseType,
                                         isDefined, forkValue, hasRelax, !hasForkId,
                                         hasAnyoneCanPay);
                        CheckSigHashType(t.withAnyoneCanPay(hasAnyoneCanPay),
                                         baseType, isDefined, forkValue, hasRelax, hasForkId,
                                         hasAnyoneCanPay);
                        CheckSigHashType(t.withAnyoneCanPay(!hasAnyoneCanPay),
                                         baseType, isDefined, forkValue, hasRelax, hasForkId,
                                         !hasAnyoneCanPay);

                        for (BaseSigHashType newBaseType : baseTypes) {
                            bool isNewDefined = newBaseType != BaseSigHashType::UNSUPPORTED;
                            CheckSigHashType(t.withBaseType(newBaseType),
                                             newBaseType, isNewDefined, forkValue,
                                             hasRelax, hasForkId, hasAnyoneCanPay);
                        }

                        for (uint32_t newForkValue : forkValues) {
                            CheckSigHashType(t.withForkValue(newForkValue),
                                             baseType, isDefined, newForkValue,
                                             hasRelax, hasForkId, hasAnyoneCanPay);
                        }
                    }
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(sighash_serialization_test)
{
    std::set<uint32_t> forkValues{0, 1, 0xab1fe9, 0xc81eea, 0xffffff};

    // Test all possible sig hash values embeded in signatures.
    for (uint32_t sigHashType = 0x00; sigHashType <= 0xff; sigHashType++) {
        for (uint32_t forkValue : forkValues) {
            uint32_t rawType = sigHashType | (forkValue << 8);

            uint32_t baseType = rawType & 0x1f;
            bool hasRelax = (rawType & SIGHASH_RELAX) != 0;
            bool hasForkId = (rawType & SIGHASH_FORKID) != 0;
            bool hasAnyoneCanPay = (rawType & SIGHASH_ANYONECANPAY) != 0;

            uint32_t noflag = sigHashType & ~(SIGHASH_RELAX | SIGHASH_FORKID | SIGHASH_ANYONECANPAY);
            bool isDefined = (noflag != 0) && (noflag <= SIGHASH_SINGLE);

            const SigHashType tbase(rawType);

            // Check deserialization.
            CheckSigHashType(tbase, BaseSigHashType(baseType), isDefined,
                             forkValue, hasRelax, hasForkId, hasAnyoneCanPay);

            // Check raw value.
            BOOST_CHECK_EQUAL(tbase.getRawSigHashType(), rawType);

            // Check serialization/deserialization.
            uint32_t unserializedOutput; // NOLINT(cppcoreguidelines-init-variables)
            (CDataStream(SER_DISK, 0) << tbase) >> unserializedOutput;
            BOOST_CHECK_EQUAL(unserializedOutput, rawType);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
