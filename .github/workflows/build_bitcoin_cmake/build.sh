#!/usr/bin/env bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

build_type=${1,,}

args="-D CMAKE_BUILD_TYPE=$build_type"
args+=' -D CMAKE_CXX_FLAGS=-Werror'
    
if [[ $build_type == debug ]];
then
    args+=' -D enable_debug=ON'
fi

for sanitizer in ${@:2}
do
    case ${sanitizer,,} in
        asan) args+=' -D enable_asan=ON -D CRYPTO_USE_ASM=OFF' ;;
        tsan) args+=' -D enable_tsan=ON' ;;
        usan) args+=' -D enable_ubsan=ON' ;;
    esac
done

echo "$args"

