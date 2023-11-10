// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_TEST_BITCOIN_H
#define BITCOIN_TEST_TEST_BITCOIN_H

#include "chainparamsbase.h"
#include "fs.h"
#include "key.h"
#include "pubkey.h"
#include "random.h"
#include "txdb.h"
#include "txmempool.h"
#include "mining/factory.h"

#include "test/testutil.h"

#include <boost/thread.hpp>

// install boost test formatters for the popular durations
namespace std { namespace chrono {
    inline std::ostream& boost_test_print_type(std::ostream& ostr, std::chrono::microseconds const& us) {
        return ostr << us.count() << "us";
    }
    inline std::ostream& boost_test_print_type(std::ostream& ostr, std::chrono::milliseconds const& ms) {
        return ostr << ms.count() << "ms";
    }
}}



extern const uint256 insecure_rand_seed;
extern FastRandomContext insecure_rand_ctx;

static inline uint32_t insecure_rand() {
    return insecure_rand_ctx.rand32();
}
static inline uint64_t InsecureRand64() {
    return insecure_rand_ctx.rand64();
}
static inline uint256 InsecureRand256() {
    return insecure_rand_ctx.rand256();
}
static inline uint64_t InsecureRandBits(int bits) {
    return insecure_rand_ctx.randbits(bits);
}
static inline uint64_t InsecureRandRange(uint64_t range) {
    return insecure_rand_ctx.randrange(range);
}
static inline bool InsecureRandBool() {
    return insecure_rand_ctx.randbool();
}
static inline std::vector<uint8_t> InsecureRandBytes(size_t len) {
    return insecure_rand_ctx.randbytes(len);
}
class ConfigInit;

void ResetGlobalRandomContext();

/**
 * Basic testing setup.
 * This just configures logging and chain parameters.
 */
struct BasicTestingSetup {
    ConfigInit& testConfig;
    fs::path pathTemp;

    BasicTestingSetup(const std::string &chainName = CBaseChainParams::MAIN);
    ~BasicTestingSetup();
};

/** Testing setup that configures a complete environment.
 * Included are data directory, coins database, script check threads setup.
 */
class CConnman;
struct TestingSetup : public BasicTestingSetup {
    boost::thread_group threadGroup;
    CConnman *connman = nullptr;

    TestingSetup(const std::string &chainName = CBaseChainParams::MAIN, 
                 mining::CMiningFactory::BlockAssemblerType assemblerType = mining::CMiningFactory::BlockAssemblerType::JOURNALING);
    ~TestingSetup();
};

class CBlock;
class CMutableTransaction;
class CScript;

//
// Testing fixture that pre-creates a
// 100-block REGTEST-mode block chain
//
struct TestChain100Setup : public TestingSetup {
    TestChain100Setup();

    // Create a new block with just given transactions, coinbase paying to
    // scriptPubKey, and try to add it to the current chain.
    CBlock CreateAndProcessBlock(const std::vector<CMutableTransaction> &txns,
                                 const CScript &scriptPubKey);

    ~TestChain100Setup();

    // For convenience, coinbase transactions.
    std::vector<CTransaction> coinbaseTxns;
    // private/public key needed to spend coinbase transactions.
    CKey coinbaseKey;
};

class CTxMemPoolEntry;
class CTxMemPool;

static constexpr Amount DEFAULT_TEST_TX_FEE{10000};

struct TestMemPoolEntryHelper {
    // Default values
    Amount nFee {0};
    int64_t nTime {0};
    unsigned int nHeight {1};
    bool spendsCoinbase {false};
    LockPoints lp;

    // Default constructor just uses the default values
    TestMemPoolEntryHelper() = default;

    // Set the default fee to something other than 0
    explicit TestMemPoolEntryHelper(const Amount& fee)
        : nFee{fee}
    {}

    CTxMemPoolEntry FromTx(const CMutableTransaction &tx,
                           CTxMemPool *pool = nullptr);
    CTxMemPoolEntry FromTx(const CTransaction &tx, CTxMemPool *pool = nullptr);

    // Change the default value
    TestMemPoolEntryHelper &Fee(Amount _fee) {
        nFee = _fee;
        return *this;
    }
    TestMemPoolEntryHelper &Time(int64_t _time) {
        nTime = _time;
        return *this;
    }
    TestMemPoolEntryHelper &Height(unsigned int _height) {
        nHeight = _height;
        return *this;
    }
    TestMemPoolEntryHelper &SpendsCoinbase(bool _flag) {
        spendsCoinbase = _flag;
        return *this;
    }
};
#endif
