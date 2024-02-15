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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class CPubKey;
class CScript;
class CScriptConfig;
class CTransaction;
class uint256;

namespace task
{
  class CCancellationToken;
}

bool CheckSignatureEncoding(const std::vector<uint8_t> &vchSig, uint32_t flags,
                            ScriptError *serror);

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
    TransactionSignatureChecker(const CTransaction *txToIn, unsigned int nInIn,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
                                const Amount amountIn)
        : txTo(txToIn), nIn(nInIn), amount(amountIn), txdata(nullptr) {}
    TransactionSignatureChecker(const CTransaction *txToIn, unsigned int nInIn,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
                                const Amount amountIn,
                                const PrecomputedTransactionData &txdataIn)
        : txTo(txToIn), nIn(nInIn), amount(amountIn), txdata(&txdataIn) {}
    bool CheckSig(const std::vector<uint8_t> &scriptSig,
                  const std::vector<uint8_t> &vchPubKey,
                  const CScript &scriptCode, bool enabledSighashForkid) const override;
    bool CheckLockTime(const CScriptNum &nLockTime) const override;
    bool CheckSequence(const CScriptNum &nSequence) const override;
};

class MutableTransactionSignatureChecker : public TransactionSignatureChecker {
private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const CTransaction txTo;

public:
    MutableTransactionSignatureChecker(const CMutableTransaction *txToIn,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
                                       unsigned int nInIn, const Amount amount)
        : TransactionSignatureChecker(&txTo, nInIn, amount), txTo(*txToIn) {}
};

/**
* EvalScript function evaluates scripts against predefined limits that are
* set by either policy rules or consensus rules. Consensus parameter determines if
* consensus rules (value=true) must be used or if policy rules(value=false) should be used.
* Consensus should be true when validating scripts of transactions that are part of block
* and it should be false when validating scripts of transactions that are validated for acceptance to mempool
*/
std::optional<bool> EvalScript(
    const CScriptConfig& config,
    bool consensus,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    uint32_t flags,
    const BaseSignatureChecker& checker,
    LimitedStack& altstack,
    long& ipc,
    std::vector<bool>& vfExec,
    std::vector<bool>& vfElse,
    ScriptError* error = nullptr);
std::optional<bool> EvalScript(
    const CScriptConfig& config,
    bool consensus,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    uint32_t flags,
    const BaseSignatureChecker& checker,
    ScriptError* error = nullptr);
std::optional<bool> VerifyScript(
    const CScriptConfig& config,
    bool consensus,
    const task::CCancellationToken& token,
    const CScript& scriptSig,
    const CScript& scriptPubKey,
    uint32_t flags,
    const BaseSignatureChecker& checker,
    ScriptError* serror = nullptr);

#endif // BITCOIN_SCRIPT_INTERPRETER_H
