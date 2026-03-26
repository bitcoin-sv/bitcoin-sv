#!/usr/bin/env bash

# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, 
# see the accompanying file LICENSE.

set -euo pipefail

PROGRAM=${0##*/}

function Display_Help
{
    cat <<EoH

    Generates coverage.lcov from profraw (clang) or gcov (gcc) data.

    usage e.g.
      "$PROGRAM -t gcc test_bitcoin"
      "$PROGRAM -t clang test_bitcoin"
      "$PROGRAM -t clang -s test_bitcoin"

    Options:
      -t = Toolset [clang|gcc] (default: clang)
      -s = Use llvm-cov show for development (interactive terminal output)
      -h = Show this help message and exit
EoH
    exit 0
}

(($# < 1)) && Display_Help
TOOLSET=clang
SHOW_MODE=false

while getopts 'hst:' VAL; do
    case $VAL in
        h ) Display_Help;;
        s ) SHOW_MODE=true;;
        t ) TOOLSET=$OPTARG;;
    esac
done
shift $((OPTIND -1))

(($# < 1)) && Display_Help
BINARY=$1

if [ $TOOLSET = 'gcc' ]; then
    if [ "$SHOW_MODE" = true ]; then
        echo "Error: -s (show mode) is only supported with clang toolset."
        exit 1
    fi

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

    llvm_path="/usr/lib/llvm-21/bin"
    $llvm_path/llvm-profdata merge -sparse default.profraw -o default.profdata

    if [ "$SHOW_MODE" = true ]; then
        $llvm_path/llvm-cov show -instr-profile=default.profdata $BINARY
        exit 0
    fi

    $llvm_path/llvm-cov export --format=lcov -instr-profile=default.profdata $BINARY > coverage.lcov

else
    echo "$TOOLSET is not a valid toolset. Use 'clang' or 'gcc'."
    exit 1
fi

