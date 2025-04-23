// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "interpreter_adapter.h"

std::optional<std::variant<ScriptError, malleability::status>> EvalScript(
    const CScriptConfig& config,
    const bool consensus,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    const uint32_t flags,
    const BaseSignatureChecker& checker)
{
    const eval_script_params params{make_eval_script_params(config, flags, consensus)};
    return EvalScript(params,
                      token,
                      stack,
                      script,
                      flags,
                      checker);
}

std::optional<std::variant<ScriptError, malleability::status>> EvalScript(
    const CScriptConfig& config,
    const bool consensus,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    const uint32_t flags,
    const BaseSignatureChecker& checker,
    LimitedStack& altstack,
    long& ipc,
    std::vector<bool>& vfExec,
    std::vector<bool>& vfElse)
{
    const eval_script_params params{make_eval_script_params(config, flags, consensus)};
    return EvalScript(params,
                      token,
                      stack,
                      script,
                      flags,
                      checker,
                      altstack,
                      ipc,
                      vfExec,
                      vfElse);
}

std::optional<std::pair<bool, ScriptError>> VerifyScript(
    const CScriptConfig& config,
    const bool consensus,
    const task::CCancellationToken& token,
    const CScript& scriptSig,
    const CScript& scriptPubKey,
    const uint32_t flags,       
    const BaseSignatureChecker& checker, 
    std::atomic<malleability::status>& malleability)
{
    const verify_script_params params{make_verify_script_params(config, flags, consensus)};
    return VerifyScript(params,
                        token,
                        scriptSig,
                        scriptPubKey,
                        flags,
                        checker,
                        malleability);
}

