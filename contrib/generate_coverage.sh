#!/usr/bin/env bash

# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, 
# see the accompanying file LICENSE.

set -euo pipefail

llvm_path="/usr/lib/llvm-19/bin"

function Display_Help
{
    cat <<EoH

Uses llvm-profdata and llvm-cov to generate coverage reports for the specified binary.
Presumes that the binary has been compiled with coverage instrumentation and that 
default.profraw file exists in the current directory.

    usage: $PROGRAM executable

    e.g. "$PROGRAM test_bitcoin"

    Options:
      -h = Show this help message and exit
EoH
    exit 0
}

PROGRAM=${0##*/}
BINARY=$1

while getopts 'h' VAL; do
    case $VAL in
        h ) Display_Help;;
    esac
done
shift $((OPTIND -1))

(($# != 1)) && Display_Help

FILE=default.profraw
if [[ ! -f "$FILE" ]]; then
    echo "'$FILE' does not exist."
    exit 1
elif [[ ! -s "$FILE" ]]; then
    echo "'$FILE' is empty."
    exit 1
fi

$llvm_path/llvm-profdata merge -sparse default.profraw -o default.profdata
$llvm_path/llvm-cov show -instr-profile=default.profdata $BINARY > ${BINARY}_show.txt
$llvm_path/llvm-cov report -instr-profile=default.profdata $BINARY > ${BINARY}_report.txt

