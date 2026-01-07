// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/script_flags.h"

static_assert(!IsChronicle(0));
static_assert(IsChronicle(SCRIPT_CHRONICLE));

static_assert(!IsGenesis(0));
static_assert(IsGenesis(SCRIPT_GENESIS));

static_assert(!IsUtxoAfterChronicle(0));
static_assert(IsUtxoAfterChronicle(SCRIPT_UTXO_AFTER_CHRONICLE));

static_assert(!IsUtxoAfterGenesis(0));
static_assert(IsUtxoAfterGenesis(SCRIPT_UTXO_AFTER_GENESIS));

static_assert(!IsDiscourageUpgradableNops(0));
static_assert(IsDiscourageUpgradableNops(SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS));

static_assert(!VerifyMinimalIf(0));
static_assert(VerifyMinimalIf(SCRIPT_VERIFY_MINIMALIF));

static_assert(!VerifyNullDummy(0));
static_assert(VerifyNullDummy(SCRIPT_VERIFY_NULLDUMMY));

static_assert(!VerifyNullFail(0));
static_assert(VerifyNullFail(SCRIPT_VERIFY_NULLFAIL));

static_assert(!VerifyMinimalData(0));
static_assert(VerifyMinimalData(SCRIPT_VERIFY_MINIMALDATA));

static_assert(!VerifyCleanStack(0));
static_assert(VerifyCleanStack(SCRIPT_VERIFY_CLEANSTACK));

static_assert(!VerifySigPushOnly(0));
static_assert(VerifySigPushOnly(SCRIPT_VERIFY_SIGPUSHONLY));
