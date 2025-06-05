#!/usr/bin/env bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

BUILD_TYPE=${1,,}

args=()
args+=(-DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
args+=(-DCMAKE_CXX_FLAGS=-Werror\ -fno-omit-frame-pointer)
args+=(-DENABLE_CLANG_TIDY=ON)

if [[ $BUILD_TYPE == debug ]];
then
    args+=(-Denable_debug=ON)
fi

for option in ${@:2}
do
    case ${option,,} in
        asan) args+=(-Denable_asan=ON); args+=(-DCRYPTO_USE_ASM=OFF) ;;
        cov)  args+=(-D ENABLE_COVERAGE=ON) ;;
        tsan) args+=(-Denable_tsan=ON) ;;
        usan) args+=(-Denable_ubsan=ON) ;;
    esac
done

for arg in "${args[@]}"; do
    echo "$arg"
done

