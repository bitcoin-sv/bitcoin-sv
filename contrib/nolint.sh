#!/usr/bin/env bash
# Copyright (c) 2025 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

find ../src -type f -regextype egrep -regex '.*[ch](pp)?' \
    | xargs ../contrib/filter_nolint \
    | sort \
    | uniq -c \
    | sort -n
