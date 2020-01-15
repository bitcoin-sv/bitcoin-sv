// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_POLICY_POLICY_H
#define BITCOIN_POLICY_POLICY_H

#include "consensus/consensus.h"
#include "script/interpreter.h"
#include "script/standard.h"

#include <optional>
#include <string>

class Config;
class CCoinsViewCache;

namespace task{class CCancellationToken;}

/** Defaults for -excessiveblocksize and -blockmaxsize. The changed when we reach blocksize activation time.
 *
 * DEFAULT_MAX_GENERATED_BLOCK_SIZE_* represents default for -blockmaxsize, 
 * which controls the maximum size of block the mining code will create 
 * 
 * DEFAULT_MAX_BLOCK_SIZE_* represents default for -excessiveblocksize, which specifies the 
 * maximum allowed size for a block, in bytes. This is actually a consenus rule - if a node sets
 * this to a value lower than  -blockmaxsize of another node, it will start rejecting 
 * big another node. 
 * 
 * Values bellow are also parsed by cdefs.py.
 */


/** Default max block size parameters
 */
static const uint64_t MAIN_DEFAULT_MAX_BLOCK_SIZE = INT64_MAX; 
static const uint64_t REGTEST_DEFAULT_MAX_BLOCK_SIZE = INT64_MAX;
static const uint64_t TESTNET_DEFAULT_MAX_BLOCK_SIZE = INT64_MAX;
static const uint64_t STN_DEFAULT_MAX_BLOCK_SIZE = INT64_MAX;


/** Default before and after max generated block size parameters and their activation times.
 */
static const uint64_t MAIN_NEW_BLOCKSIZE_ACTIVATION_TIME = 1563976800; // 2019-07-24T14:00:00
static const uint64_t MAIN_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = 32 * ONE_MEGABYTE;
static const uint64_t MAIN_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = 128 * ONE_MEGABYTE;

static const uint64_t REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME = 1563976800; // 2019-07-24T14:00:00 
static const uint64_t REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = 32 * ONE_MEGABYTE;
static const uint64_t REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = 128 * ONE_MEGABYTE;

static const uint64_t TESTNET_NEW_BLOCKSIZE_ACTIVATION_TIME = 1563976800; // 2019-07-24T14:00:00 
static const uint64_t TESTNET_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = 32 * ONE_MEGABYTE;
static const uint64_t TESTNET_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = 128 * ONE_MEGABYTE;

static const uint64_t STN_NEW_BLOCKSIZE_ACTIVATION_TIME = 1558360800;   // 2019-05-20T14:00:00
static const uint64_t STN_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = 32 * ONE_MEGABYTE;
static const uint64_t STN_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = 128 * ONE_MEGABYTE;


/** Default for -blockprioritypercentage, define the amount of block space
 * reserved to high priority transactions **/
static const uint64_t DEFAULT_BLOCK_PRIORITY_PERCENTAGE = 5;
/** Default for -blockmintxfee, which sets the minimum feerate for a transaction
 * in blocks created by mining code **/
static const Amount DEFAULT_BLOCK_MIN_TX_FEE(500);
/** The maximum size for transactions we're willing to relay/mine - before genesis*/
static const uint64_t MAX_TX_SIZE_POLICY_BEFORE_GENESIS = 100000 - 1; // -1 because pre genesis policy validation was >=
/** The default size for transactions we're willing to relay/mine */
static const uint64_t DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS = 10 * ONE_MEGABYTE;
/** Maximum number of signature check operations in an IsStandard() P2SH script
 */
static const unsigned int MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx before Genesis */
static const unsigned int MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS = MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS / 5;
/** The maximum number of sigops we're willing to relay/mine in a single tx after Genesis */
static const unsigned int MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS = UINT32_MAX;
/** Default policy value for -maxtxsigopscountspolicy, maximum number of sigops we're willing to relay/mine in a single tx after Genesis */
static const unsigned int DEFAULT_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS = MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS;
/** Default for -maxmempool, maximum megabytes of mempool memory usage */
static const unsigned int DEFAULT_MAX_MEMPOOL_SIZE = 1000;
/** Default for -maxnonfinalmempool, maximum megabytes of non-final mempool memory usage */
static const unsigned int DEFAULT_MAX_NONFINAL_MEMPOOL_SIZE = 50;
/** Default for -incrementalrelayfee, which sets the minimum feerate increase
 * for mempool limiting or BIP 125 replacement **/
static const CFeeRate MEMPOOL_FULL_FEE_INCREMENT(Amount(1000));
/** Default for -maxscriptsizepolicy **/
static const unsigned int DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS = 10000;
/**
 * Min feerate for defining dust. Historically this has been the same as the
 * minRelayTxFee, however changing the dust limit changes which transactions are
 * standard and should be done with care and ideally rarely. It makes sense to
 * only increase the dust limit after prior releases were already not creating
 * outputs below the new threshold.
 */
static const Amount DUST_RELAY_TX_FEE(1000);

/*
* Number of blocks around GENESIS activation (72 blocks before and 72 blocks after) when
* nodes will not be banned if they send a script that is not valid. That means if a node
* sends a GENESIS only valid transaction before GENESIS is activated it will not be baned.
* Same applies for a node that sent a PRE-GENESIS only valid transaction after GENESIS
* is activated
*/
static const int DEFAULT_GENESIS_GRACEFULL_ACTIVATION_PERIOD = 72;

/*
* Maximum number of blocks for Genesis graceful period on either side of the Genesis 
* activation block (span of ~100 days)
*/
static const int MAX_GENESIS_GRACEFULL_ACTIVATION_PERIOD = 7200;

// Default policy value for maximum number of non-push operations per script
static const int DEFAULT_OPS_PER_SCRIPT_POLICY_AFTER_GENESIS = UINT32_MAX;

// Default policy value for maximum number of public keys per multisig after GENESIS
static const uint64_t DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS = UINT32_MAX;

/** Maximum stack memory usage (used instead of MAX_SCRIPT_ELEMENT_SIZE and MAX_STACK_ELEMENTS) after Genesis. **/
static const uint64_t DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS = 100 * ONE_MEGABYTE;

// Default policy value for script number length after Genesis
static const uint64_t DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS = 250 * ONE_KILOBYTE;

/**
 * Standard script verification flags that standard transactions will comply
 * with. However scripts violating these flags may still be present in valid
 * blocks and we must accept those blocks.
 */
static const unsigned int STANDARD_SCRIPT_VERIFY_FLAGS =
    MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_DERSIG |
    SCRIPT_VERIFY_MINIMALDATA | SCRIPT_VERIFY_NULLDUMMY |
    SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS | SCRIPT_VERIFY_CLEANSTACK |
    SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;

/** For convenience, standard but not mandatory verify flags. */
static const unsigned int STANDARD_NOT_MANDATORY_VERIFY_FLAGS =
    STANDARD_SCRIPT_VERIFY_FLAGS & ~MANDATORY_SCRIPT_VERIFY_FLAGS;

/** returns flags for "standard" script*/
inline unsigned int StandardScriptVerifyFlags(bool genesisEnabled,
                                       bool utxoAfterGenesis) {
    unsigned int scriptFlags = STANDARD_SCRIPT_VERIFY_FLAGS;
    if (utxoAfterGenesis) {
        scriptFlags |= SCRIPT_UTXO_AFTER_GENESIS;
    }
    if (genesisEnabled) {
        scriptFlags |= SCRIPT_GENESIS;
        scriptFlags |= SCRIPT_VERIFY_SIGPUSHONLY;
    }
    return scriptFlags;
}

/** Get the flags to use for non-final transaction checks */
inline unsigned int StandardNonFinalVerifyFlags(bool genesisEnabled)
{
    unsigned int flags { LOCKTIME_MEDIAN_TIME_PAST };
    if(!genesisEnabled) {
        flags |= LOCKTIME_VERIFY_SEQUENCE;
    }
    return flags;
}

bool IsStandard(const Config &config, const CScript &scriptPubKey, int nScriptPubKeyHeight, txnouttype &whichType);

/**
 * Check for standard transaction types
 * @param[in] nHeight represents the height that transactions was mined or the height that
 * we expect transcation will be mined in (in case transcation is being added to mempool)
 * @return True if all outputs (scriptPubKeys) use only standard transaction
 * forms
 */
bool IsStandardTx(const Config &config, const CTransaction &tx, int nHeight, std::string &reason);

/**
 * Check for standard transaction types
 * @param[in] mapInputs    Map of previous transactions that have outputs we're
 * spending
 * @return True if all inputs (scriptSigs) use only standard transaction forms
 */
std::optional<bool> AreInputsStandard(
    const task::CCancellationToken& token,
    const Config& config,
    const CTransaction& tx,
    const CCoinsViewCache &mapInputs,
    const int mempoolHeight);

extern CFeeRate incrementalRelayFee;
extern CFeeRate dustRelayFee;

#endif // BITCOIN_POLICY_POLICY_H
