// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"
#include "key.h"
#include "mempooltxdb.h"
#include "script/script_num.h"

#define LOGSTATS(x) // x

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
    for (std::vector<uint8_t> data : // NOLINT (performance-for-range-copy)
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
    file.reset();
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
    for (std::vector<uint8_t> data : // NOLINT (performance-for-range-copy)
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
        file.reset();
        i++;
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

// NOLINTBEGIN (cert-err58-cpp)
BENCHMARK(WriteToFileDataTx);
BENCHMARK(WriteToSeparateFilesDatatx);
// NOLINTEND
