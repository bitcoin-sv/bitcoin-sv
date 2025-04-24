// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_SCRIPT_INTERPRETER_H
#define BITCOIN_SCRIPT_INTERPRETER_H

#include "primitives/transaction.h"
#include "script/script_flags.h"
#include "script_error.h"
#include "sighashtype.h"
#include "limitedstack.h"
#include "malleability_status.h"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

class CPubKey;
class CScript;
class CTransaction;
class uint256;

namespace task
{
  class CCancellationToken;
}

std::variant<ScriptError, malleability::status> CheckSignatureEncoding(
    const std::vector<uint8_t>& sig,
    uint32_t flags);

uint256 SignatureHash(const CScript &scriptCode, const CTransaction &txTo,
                      unsigned int nIn, SigHashType sigHashType,
                      const Amount amount,
                      const PrecomputedTransactionData *cache = nullptr,
                      bool enabledSighashForkid = true);

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class BaseSignatureChecker {
public:
    virtual bool CheckSig(const std::vector<uint8_t> &scriptSig,
                          const std::vector<uint8_t> &vchPubKey,
                          const CScript &scriptCode, bool enabledSighashForkid) const {
        return false;
    }

    virtual bool CheckLockTime(const CScriptNum &nLockTime) const {
        return false;
    }

    virtual bool CheckSequence(const CScriptNum &nSequence) const {
        return false;
    }
    
    virtual std::int32_t Version() const
    {
        return 0;
    }

    virtual ~BaseSignatureChecker() {}
};

class TransactionSignatureChecker : public BaseSignatureChecker {
private:
    const CTransaction *txTo;
    unsigned int nIn;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const Amount amount;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const PrecomputedTransactionData *txdata;

protected:
    virtual bool VerifySignature(const std::vector<uint8_t> &vchSig,
                                 const CPubKey &vchPubKey,
                                 const uint256 &sighash) const;

public:
    TransactionSignatureChecker(const CTransaction* txToIn,
                                unsigned int nInIn,
                                const Amount& amountIn)
        : txTo(txToIn),
          nIn(nInIn),
          amount(amountIn),
          txdata(nullptr)
    {
    }

    TransactionSignatureChecker(const CTransaction* txToIn,
                                unsigned int nInIn,
                                const Amount& amountIn,
                                const PrecomputedTransactionData& txdataIn)
        : txTo(txToIn),
          nIn(nInIn),
          amount(amountIn),
          txdata(&txdataIn)
    {
    }

    bool CheckSig(const std::vector<uint8_t> &scriptSig,
                  const std::vector<uint8_t> &vchPubKey,
                  const CScript &scriptCode, bool enabledSighashForkid) const override;
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
    uint64_t maxScriptNumLength_;
    uint64_t maxScriptSize_;
    uint64_t maxPubKeysPerMultiSig_;

public:
    constexpr eval_script_params(uint64_t maxOpsPerScript,
                                 uint64_t maxScriptNumLength,
                                 uint64_t maxScriptSize,
                                 uint64_t maxPubKeysPerMultiSig)
        : maxOpsPerScript_{maxOpsPerScript},
          maxScriptNumLength_{maxScriptNumLength},
          maxScriptSize_{maxScriptSize},
          maxPubKeysPerMultiSig_{maxPubKeysPerMultiSig}
    {
    }

    constexpr uint64_t MaxOpsPerScript() const { return maxOpsPerScript_; }
    constexpr uint64_t MaxScriptNumLength() const { return maxScriptNumLength_; }
    constexpr uint64_t MaxScriptSize() const { return maxScriptSize_; }
    constexpr uint64_t MaxPubKeysPerMultiSig() const { return maxPubKeysPerMultiSig_; }

    constexpr bool operator==(const eval_script_params& other) const = default;
};
static_assert(eval_script_params(1, 2, 3, 4).MaxOpsPerScript() == 1);
static_assert(eval_script_params(1, 2, 3, 4).MaxScriptNumLength() == 2);
static_assert(eval_script_params(1, 2, 3, 4).MaxScriptSize() == 3);
static_assert(eval_script_params(1, 2, 3, 4).MaxPubKeysPerMultiSig() == 4);

eval_script_params make_eval_script_params(const Config&,
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

verify_script_params make_verify_script_params(const Config&,
                                               uint32_t flags,
                                               bool consensus);

/**
* EvalScript function evaluates scripts against predefined limits that are
* set by either policy rules or consensus rules. Consensus parameter determines if
* consensus rules (value=true) must be used or if policy rules(value=false) should be used.
* Consensus should be true when validating scripts of transactions that are part of block
* and it should be false when validating scripts of transactions that are validated for acceptance to mempool
*/
std::optional<std::variant<ScriptError, malleability::status>> EvalScript(
    const eval_script_params&,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    uint32_t flags,
    const BaseSignatureChecker& checker,
    LimitedStack& altstack,
    long& ipc,
    std::vector<bool>& vfExec,
    std::vector<bool>& vfElse);

std::optional<std::variant<ScriptError, malleability::status>> EvalScript(
    const eval_script_params&,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    uint32_t flags,
    const BaseSignatureChecker& checker);

std::optional<std::pair<bool, ScriptError>> VerifyScript(
    const verify_script_params&,
    const task::CCancellationToken&,
    const CScript& scriptSig,
    const CScript& scriptPubKey,
    uint32_t flags,
    const BaseSignatureChecker&,
    std::atomic<malleability::status>&);

#endif // BITCOIN_SCRIPT_INTERPRETER_H
