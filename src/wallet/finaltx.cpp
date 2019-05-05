// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "chainparams.h"
#include "config.h"
#include "consensus/validation.h"
#include "primitives/transaction.h"
#include "validation.h"

bool CheckFinalTx(const CTransaction &tx, int flags = -1) {
    auto &config = GlobalConfig::GetConfig();
    CValidationState state;
    return ContextualCheckTransactionForCurrentBlock(config, tx, state, flags);
}
