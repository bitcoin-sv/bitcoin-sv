#!/usr/bin/env bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

TOOLSET=${1,,}
BUILD_TYPE=${2,,}

args=()
args+=(-DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
cxx_extra=""
if [[ $TOOLSET == gcc ]]; then
    if c++ -dumpversion 2>/dev/null | grep -q '^13'; then
        cxx_extra="-Wno-stringop-overread"
    fi
fi
args+=(-DCMAKE_CXX_FLAGS=-Werror\ -fno-omit-frame-pointer\ ${cxx_extra})

# Unity builds default ON, but disable for clang-tidy
unity_build=ON

if [[ $TOOLSET == clang ]] && [[ $BUILD_TYPE == debug ]]; then
    args+=(-DENABLE_CLANG_TIDY=ON)
    unity_build=OFF
fi

args+=(-DCMAKE_UNITY_BUILD=$unity_build)

if [[ $BUILD_TYPE == debug ]];
then
    args+=(-Denable_debug=ON)
fi

for option in ${@:3}
do
    case ${option,,} in
        asan) args+=(-Denable_asan=ON); args+=(-DCRYPTO_USE_ASM=OFF) ;;
        cov)  args+=(-DENABLE_COVERAGE=ON) ;;
        tsan) args+=(-Denable_tsan=ON) ;;
        usan) args+=(-Denable_ubsan=ON) ;;
    esac
done

for arg in "${args[@]}"; do
    echo "$arg"
done

