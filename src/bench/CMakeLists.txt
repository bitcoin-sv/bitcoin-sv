# Copyright (c) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

find_package(PythonInterp 3 REQUIRED)

add_custom_command(
  OUTPUT data/block413567.raw.h
  COMMAND ${CMAKE_COMMAND} -E make_directory data
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/data/hexhdr.py ${CMAKE_CURRENT_SOURCE_DIR}/data/block413567.raw data/block413567.raw.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/block413567.raw
  )

add_executable(bench_bitcoin
        bench_bitcoin.cpp
        base58.cpp
        bench.cpp
        ccoins_caching.cpp
        checkblock.cpp
        checkqueue.cpp
        $<$<BOOL:${BUILD_BITCOIN_WALLET}>:coin_selection.cpp>
        crypto_hash.cpp
        interpreter.cpp
        lockedpool.cpp
        mempool_eviction.cpp
        mempooltxdb.cpp
        perf.cpp
        rollingbloom.cpp
        serialisation.cpp
        thread_safe_queue.cpp
        data/block413567.raw.h)

target_link_libraries(bench_bitcoin
        server
        rpcclient
        common
        util
        bitcoinconsensus
        crypto
        leveldb
        univalue
        $<$<PLATFORM_ID:Linux>:rt>) # use librt.so for aio on Linux
