// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#pragma once

#include "script/interpreter.h"

// Wrapper functions to aid migration away from Config

std::optional<std::variant<ScriptError, malleability::status>> EvalScript(
    const Config&,
    bool consensus,
    const task::CCancellationToken&,
    LimitedStack& stack,
    const CScript&,
    uint32_t flags,
    const BaseSignatureChecker&,
    LimitedStack& altstack,
    long& ipc,
    std::vector<bool>& vfExec,
    std::vector<bool>& vfElse);

std::optional<std::variant<ScriptError, malleability::status>> EvalScript(
    const Config&,
    bool consensus,
    const task::CCancellationToken&,
    LimitedStack&,
    const CScript&,
    uint32_t flags,
    const BaseSignatureChecker&);

std::optional<std::pair<bool, ScriptError>> VerifyScript(
    const Config&,
    bool consensus,
    const task::CCancellationToken&,
    const CScript& scriptSig,
    const CScript& scriptPubKey,
    uint32_t flags,
    const BaseSignatureChecker&,
    std::atomic<malleability::status>&);

