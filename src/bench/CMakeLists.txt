# Copyright (c) 2018 The Bitcoin SV developers

add_executable(bench_bitcoin
        bench_bitcoin.cpp
        base58.cpp
        bench.cpp
        ccoins_caching.cpp
        checkblock.cpp
        checkqueue.cpp
        coin_selection.cpp
        crypto_hash.cpp
        lockedpool.cpp
        mempool_eviction.cpp
        perf.cpp
        rollingbloom.cpp)

target_link_libraries(bench_bitcoin
        server
        common
        util
        bitcoinconsensus
        crypto
        leveldb
        memenv
        secp256k1
        univalue)
