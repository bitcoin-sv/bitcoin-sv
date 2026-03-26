// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_SCRIPT_INTERPRETER_H
#define BITCOIN_SCRIPT_INTERPRETER_H

#include "conditional_tracker.h"
#include "configscriptpolicy.h"
#include "limitedstack.h"
#include "primitives/transaction.h"
#include "script_error.h"
#include "sighashtype.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

class CPubKey;
class CScript;
class CTransaction;
class uint256;

namespace task
{
  class CCancellationToken;
}

ScriptError CheckSignatureEncoding(
    const std::vector<uint8_t>& sig,
    uint32_t flags,
    int32_t txnVersion);

uint256 SignatureHash(const CScript &scriptCode, const CTransaction &txTo,
                      unsigned int nIn, SigHashType sigHashType,
                      const Amount amount,
                      const PrecomputedTransactionData *cache = nullptr,
                      bool enabledSighashForkid = true);

uint256 SignatureHashOriginal(const CScript &scriptCode, const CTransaction &txTo,
                              unsigned int nIn, SigHashType sigHashType);

uint256 SignatureHashBIP143(const CScript &scriptCode, const CTransaction &txTo,
                         unsigned int nIn, SigHashType sigHashType,
                         const Amount amount,
                         const PrecomputedTransactionData *cache = nullptr);

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class BaseSignatureChecker {
public:
    virtual bool CheckSig(const std::vector<uint8_t>& /*scriptSig*/,
                          const std::vector<uint8_t>& /*vchPubKey*/,
                          const CScript& /*scriptCode*/,
                          bool /*enabledSighashForkid*/) const {
        return false;
    }

    virtual bool CheckLockTime(const CScriptNum& /*nLockTime*/) const {
        return false;
    }

    virtual bool CheckSequence(const CScriptNum& /*nSequence*/) const {
        return false;
    }
    
    virtual std::int32_t Version() const
    {
        return 0;
    }

    virtual ~BaseSignatureChecker() {}
};

class TransactionSignatureChecker : public BaseSignatureChecker
{
    const CTransaction* txTo_;
    unsigned int nIn_;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const Amount amount_;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const PrecomputedTransactionData* txdata_{nullptr};

protected:
    virtual bool VerifySignature(const std::vector<uint8_t> &vchSig,
                                 const CPubKey &vchPubKey,
                                 const uint256 &sighash) const;

public:
    TransactionSignatureChecker(const CTransaction* txTo,
                                unsigned int nIn,
                                const Amount& amount)
        : txTo_{txTo},
          nIn_{nIn},
          amount_{amount}
    {
    }

    TransactionSignatureChecker(const CTransaction* txTo,
                                unsigned int nIn,
                                const Amount& amount,
                                const PrecomputedTransactionData& txdata)
        : txTo_{txTo},
          nIn_{nIn},
          amount_{amount},
          txdata_{&txdata}
    {
    }

    bool CheckSig(const std::vector<uint8_t> &scriptSig,
                  const std::vector<uint8_t> &vchPubKey,
                  const CScript &scriptCode,
                  bool enabledSighashForkid) const override;
    bool CheckLockTime(const CScriptNum &nLockTime) const override;
    bool CheckSequence(const CScriptNum &nSequence) const override;
    int32_t Version() const override;
};

class MutableTransactionSignatureChecker : public TransactionSignatureChecker {
private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const CTransaction txTo;

public:
    MutableTransactionSignatureChecker(const CMutableTransaction* txToIn,
                                       unsigned int nInIn,
                                       const Amount& amount)
        : TransactionSignatureChecker(&txTo, nInIn, amount),
          txTo(*txToIn)
    {
    }
};

class eval_script_params
{
    uint64_t maxOpsPerScript_;
    size_t maxScriptNumLength_;
    uint64_t maxScriptSize_;
    uint64_t maxPubKeysPerMultiSig_;

public:
    constexpr eval_script_params(uint64_t maxOpsPerScript,
                                 uint64_t maxScriptNumLength,
                                 uint64_t maxScriptSize,
                                 uint64_t maxPubKeysPerMultiSig)
        : maxOpsPerScript_{maxOpsPerScript},
          maxScriptNumLength_{[&] {
              if (!std::in_range<size_t>(maxScriptNumLength)) {
                  throw std::range_error("MaxScriptNumLength exceeds size_t range");
              }
              return static_cast<size_t>(maxScriptNumLength);
          }()},
          maxScriptSize_{maxScriptSize},
          maxPubKeysPerMultiSig_{maxPubKeysPerMultiSig}
    {
    }

    constexpr uint64_t MaxOpsPerScript() const { return maxOpsPerScript_; }
    constexpr size_t MaxScriptNumLength() const { return maxScriptNumLength_; }
    constexpr uint64_t MaxScriptSize() const { return maxScriptSize_; }
    constexpr uint64_t MaxPubKeysPerMultiSig() const { return maxPubKeysPerMultiSig_; }

    constexpr bool operator==(const eval_script_params& other) const = default;
};
static_assert(eval_script_params(1, 2, 3, 4).MaxOpsPerScript() == 1);
static_assert(eval_script_params(1, 2, 3, 4).MaxScriptNumLength() == 2);
static_assert(eval_script_params(1, 2, 3, 4).MaxScriptSize() == 3);
static_assert(eval_script_params(1, 2, 3, 4).MaxPubKeysPerMultiSig() == 4);

eval_script_params make_eval_script_params(const ConfigScriptPolicy& policySettings,
                                           uint32_t flags,
                                           bool consensus);

class verify_script_params
{
    eval_script_params eval_script_params_;
    uint64_t maxStackMemoryUsage_;

public:
    constexpr verify_script_params(const class eval_script_params& eval_script_params,
                                   uint64_t maxStackMemoryUsage)
        : eval_script_params_{eval_script_params},
          maxStackMemoryUsage_{maxStackMemoryUsage}
    {
    }

    constexpr const eval_script_params& EvalScriptParams() const { return eval_script_params_; }
    constexpr uint64_t MaxStackMemoryUsage() const { return maxStackMemoryUsage_; }
};
static_assert(verify_script_params(eval_script_params{1, 2, 3, 4}, 5).EvalScriptParams()
              == eval_script_params{1, 2, 3, 4});
static_assert(verify_script_params(eval_script_params{1, 2, 3, 4}, 5).MaxStackMemoryUsage() == 5);

verify_script_params make_verify_script_params(const ConfigScriptPolicy& policySettings,
                                               uint32_t flags,
                                               bool consensus);

/**
* EvalScript function evaluates scripts against predefined limits that are
* set by either policy rules or consensus rules. Consensus parameter determines if
* consensus rules (value=true) must be used or if policy rules(value=false) should be used.
* Consensus should be true when validating scripts of transactions that are part of block
* and it should be false when validating scripts of transactions that are validated for acceptance to mempool
*/
std::optional<ScriptError> EvalScript(
    const eval_script_params&,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    const CScript* checksigData,
    uint32_t flags,
    const BaseSignatureChecker& checker,
    LimitedStack& altstack,
    long& ipc,
    conditional_tracker& conditions);

std::optional<ScriptError> EvalScript(
    const eval_script_params&,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    uint32_t flags,
    const BaseSignatureChecker& checker);

std::optional<ScriptError> VerifyScript(
    const verify_script_params&,
    const task::CCancellationToken&,
    const CScript& scriptSig,
    const CScript& scriptPubKey,
    uint32_t flags,
    const BaseSignatureChecker&);

#endif // BITCOIN_SCRIPT_INTERPRETER_H
