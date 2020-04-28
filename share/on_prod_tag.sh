#!/bin/bash
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Are we in a git repo?
if ! [ -d .git ]
then
    exit 1
fi

# Do we have access to git?
if ! [ -x "$(command -v git)" ]
then
    exit 1
fi

# Determine if we are being built from a production git tag
if [[ `git describe --tags` =~ ^v[0-9]+\.[0-9]+\.[0-9]$ ]]
then
    # On production tag
    exit 0
fi

exit 1
