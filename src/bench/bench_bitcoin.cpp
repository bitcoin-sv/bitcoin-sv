// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "bench.h"

#include "crypto/sha256.h"
#include "key.h"
#include "random.h"
#include "util.h"
#include "validation.h"

int main(int argc, char **argv) {
    SHA256AutoDetect();
    RandomInit();
    ECC_Start();
    SetupEnvironment();

    // don't want to write to bitcoind.log file
    GetLogger().fPrintToDebugLog = false;

    benchmark::BenchRunner::RunAll();

    ECC_Stop();
}
