#!/usr/bin/env bash

# Copyright (c) 2026 BSV Blockchain Association
# Distributed under the Open BSV software license,
# see the accompanying file LICENSE.

# Generates an HTML coverage report from an lcov file.
#
# usage e.g.
#   coverage_html.sh coverage.lcov
#   coverage_html.sh -o output_dir -n "My Report" coverage.lcov

set -euo pipefail

PROGRAM=${0##*/}

function Display_Help
{
    cat <<EoH

    Generates an HTML coverage report from an lcov file.

    usage: $PROGRAM [options] <lcov-file>

    Options:
      -o = Output directory (default: coverage_html)
      -n = Title for the HTML report (default: Coverage)
      -h = Show this help message and exit
EoH
    exit 0
}

(($# < 1)) && Display_Help
TITLE="Coverage"
OUTPUT_DIR="coverage_html"

while getopts 'hn:o:' VAL; do
    case $VAL in
        h ) Display_Help;;
        n ) TITLE=$OPTARG;;
        o ) OUTPUT_DIR=$OPTARG;;
    esac
done
shift $((OPTIND -1))

(($# < 1)) && Display_Help
LCOV_FILE=$1

if [[ ! -f "$LCOV_FILE" ]]; then
    echo "'$LCOV_FILE' does not exist."
    exit 1
fi

genhtml "$LCOV_FILE" \
    --output-directory "$OUTPUT_DIR" \
    --title "$TITLE" \
    --function-coverage \
    --branch-coverage \
    --synthesize-missing \
    --ignore-errors source,source \
    --ignore-errors category,category
