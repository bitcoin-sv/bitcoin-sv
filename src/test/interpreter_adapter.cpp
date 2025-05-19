// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "interpreter_adapter.h"

std::optional<std::pair<bool, ScriptError>> VerifyScript(
    const Config& config,
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

