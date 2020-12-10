// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"
#include "key.h"
#include "mempooltxdb.h"
#include "script/script_num.h"

#define LOGSTATS(x) // x

static void do_WriteToDBdataTx() {
    CMempoolTxDB txdb {GetDataDir() / "benchMempoolTxDB", 100000000, false};
    LOGSTATS(std::cout << "Data tx write to db (duration in seconds)\n");
    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout = COutPoint();
    t.vin[0].scriptSig << std::vector<uint8_t>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = CENT;

    std::vector<uint8_t> data1MB(1000000);
    std::vector<uint8_t> data10MB(10000000);
    // std::vector<uint8_t> data100MB(100000000);
    // std::vector<uint8_t> data1GB(1000000000);
    // std::vector<uint8_t> data2GB(2000000000);
    std::vector<CTransactionRef> txs;
    for (std::vector<uint8_t> data :
         {data1MB, data10MB}) //, data100MB, data1GB, data2GB})
    {
        CScript opFalseOpReturnScript = CScript()
                                        << OP_FALSE << OP_RETURN << data;
        t.vout[0].scriptPubKey = opFalseOpReturnScript;
        txs.emplace_back(MakeTransactionRef(CTransaction{t}));
    }
    LOGSTATS(auto startTime = GetTimeMillis());
    txdb.AddTransactions(txs);
    LOGSTATS(std::cout << (GetTimeMillis() - startTime) / 1000.0 << std::endl);
}

static void do_WriteToDBmultisig() {
    CMempoolTxDB txdb {GetDataDir() / "benchMempoolTxDB", 100000000, false};
    LOGSTATS(std::cout << "Multisig tx write (duration in seconds)\n");
    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout = COutPoint();
    t.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    t.vout.resize(1);
    t.vout[0].nValue = CENT;

    std::vector<CKey> keys(10000);
    for (auto &key : keys) {
        key.MakeNewKey(false);
    }
    // Create scriptPubKey with 200 public keys
    CScript scriptPubKey1000;
    CScript scriptPubKey10000;
    // CScript scriptPubKey100000;
    // CScript scriptPubKey1000000;
    std::vector<size_t> keys_sizes = {1000, 10000, 100000, 1000000};
    std::vector<CScript> scriptPubKeys = {
        scriptPubKey1000,
        scriptPubKey10000}; //, scriptPubKey100000, scriptPubKey1000000};
    for (auto &key : scriptPubKeys) {
        key << OP_1;
    }
    uint64_t i;
    uint64_t j;
    CKey key;
    for (i = 0; i < keys.size(); i++) {
        key = keys[i];
        for (j = 0; j < scriptPubKeys.size(); j++) {
            if (i < keys_sizes[j]) {
                scriptPubKeys[j] << ToByteVector(key.GetPubKey());
            }
        }
    }
    std::vector<CTransactionRef> txs;
    for (i = 0; i < scriptPubKeys.size(); i++) {
        scriptPubKeys[i] << CScriptNum(keys_sizes[i]) << OP_CHECKMULTISIG;
        scriptPubKeys[i] << OP_1;

        t.vout[0].scriptPubKey = scriptPubKeys[i];
        txs.emplace_back(MakeTransactionRef(CTransaction{t}));
    }
    LOGSTATS(auto startTime = GetTimeMillis());
    txdb.AddTransactions(txs);
    LOGSTATS(std::cout << (GetTimeMillis() - startTime) / 1000.0 << std::endl);
}

static void do_WriteToFileDataTx() {
    fs::path filename = "transactions";
    FILE *filestr{fsbridge::fopen(GetDataDir() / filename, "wb")};
    CAutoFile file{filestr, SER_DISK, CLIENT_VERSION};

    LOGSTATS(std::cout << "Data tx write to file (duration in seconds)\n");
    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout = COutPoint();
    t.vin[0].scriptSig << std::vector<uint8_t>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = CENT;

    std::vector<uint8_t> data1MB(1000000);
    std::vector<uint8_t> data10MB(10000000);
    // std::vector<uint8_t> data100MB(100000000);
    // std::vector<uint8_t> data1GB(1000000000);
    // std::vector<uint8_t> data2GB(2000000000);
    for (std::vector<uint8_t> data :
         {data1MB, data10MB}) //, data100MB, data1GB, data2GB})
    {
        CScript opFalseOpReturnScript = CScript()
                                        << OP_FALSE << OP_RETURN << data;
        t.vout[0].scriptPubKey = opFalseOpReturnScript;

        CTransaction tx {t};
        LOGSTATS(auto startTime = GetTimeMillis());
        file << tx;
        LOGSTATS(std::cout << (GetTimeMillis() - startTime) / 1000.0 << std::endl);
    }
    FileCommit(file.Get());
    file.fclose();
}

static void do_WriteToSeparateFilesDatatx() {
    LOGSTATS(std::cout << "Data tx write to separate files (duration in seconds)\n");
    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout = COutPoint();
    t.vin[0].scriptSig << std::vector<uint8_t>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = CENT;

    std::vector<uint8_t> data1MB(1000000);
    std::vector<uint8_t> data10MB(10000000);
    // std::vector<uint8_t> data100MB(100000000);
    // std::vector<uint8_t> data1GB(1000000000);
    // std::vector<uint8_t> data2GB(2000000000);
    uint64_t i = 0;
    for (std::vector<uint8_t> data :
         {data1MB, data10MB}) //, data100MB, data1GB, data2GB})
    {
        CScript opFalseOpReturnScript = CScript()
                                        << OP_FALSE << OP_RETURN << data;
        t.vout[0].scriptPubKey = opFalseOpReturnScript;

        CTransaction tx {t};
        fs::path filename = "transaction." + std::to_string(i);
        FILE *filestr{fsbridge::fopen(GetDataDir() / filename, "wb")};
        CAutoFile file{filestr, SER_DISK, CLIENT_VERSION};

        LOGSTATS(auto startTime = GetTimeMillis());
        file << tx;
        LOGSTATS(std::cout << (GetTimeMillis() - startTime) / 1000.0 << std::endl);
        FileCommit(file.Get());
        file.fclose();
        i++;
    }
}

static void WriteToDBdataTx(benchmark::State &state) {
    while (state.KeepRunning()) {
        do_WriteToDBdataTx();
    }
}

static void WriteToDBmultisig(benchmark::State &state) {
    while (state.KeepRunning()) {
        do_WriteToDBmultisig();
    }
}

static void WriteToFileDataTx(benchmark::State &state) {
    while (state.KeepRunning()) {
        do_WriteToFileDataTx();
    }
}

static void WriteToSeparateFilesDatatx(benchmark::State &state) {
    while (state.KeepRunning()) {
        do_WriteToSeparateFilesDatatx();
    }
}

BENCHMARK(WriteToDBdataTx);
BENCHMARK(WriteToDBmultisig);
BENCHMARK(WriteToFileDataTx);
BENCHMARK(WriteToSeparateFilesDatatx);
