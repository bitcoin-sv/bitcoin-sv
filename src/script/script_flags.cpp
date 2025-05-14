// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/script_flags.h"

static_assert(!IsChronicle(0));
static_assert(IsChronicle(SCRIPT_CHRONICLE));

static_assert(!IsUtxoAfterGenesis(0));
static_assert(IsUtxoAfterGenesis(SCRIPT_UTXO_AFTER_GENESIS));

static_assert(!IsUtxoAfterChronicle(0));
static_assert(IsUtxoAfterChronicle(SCRIPT_UTXO_AFTER_CHRONICLE));

static_assert(!VerifyNullDummy(0));
static_assert(VerifyNullDummy(SCRIPT_VERIFY_NULLDUMMY));

static_assert(!VerifyNullFail(0));
static_assert(VerifyNullFail(SCRIPT_VERIFY_NULLFAIL));

