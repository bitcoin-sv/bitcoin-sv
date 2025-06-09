#!/usr/bin/env bash

# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, 
# see the accompanying file LICENSE.

set -euo pipefail

PROGRAM=${0##*/}

function Display_Help
{
    cat <<EoH

    Produces html coverage reports for a given binary using lcov or llvm-cov.

    usage e.g.
      "$PROGRAM -t gcc test_bitcoin"

    Options:
      -t = Toolset [clang|gcc] (default: clang)
      -h = Show this help message and exit
EoH
    exit 0
}

(($# < 1)) && Display_Help
TOOLSET=clang

while getopts 'ht:' VAL; do
    case $VAL in
        h ) Display_Help;;
        t ) TOOLSET=$OPTARG;;
    esac
done
shift $((OPTIND -1))
    
(($# < 1)) && Display_Help
BINARY=$1

if [ $TOOLSET = 'gcc' ]; then
    lcov --capture \
         --directory . \
         --exclude='/usr/*' \
         -o coverage.lcov

elif [ $TOOLSET = 'clang' ]; then
    FILE=default.profraw
    if [[ ! -f "$FILE" ]]; then
        echo "'$FILE' does not exist."
        exit 1
    elif [[ ! -s "$FILE" ]]; then
        echo "'$FILE' is empty."
        exit 1
    fi

    llvm_path="/usr/lib/llvm-19/bin"
    $llvm_path/llvm-profdata merge -sparse default.profraw -o default.profdata
    $llvm_path/llvm-cov export --format=lcov -instr-profile=default.profdata $BINARY > coverage.lcov
    
else
    echo "$TOOLSET is not a valid toolset. Use 'clang' or 'gcc'."
    exit 1
fi

genhtml coverage.lcov --output-directory ${BINARY}_cov
