#!/usr/bin/env bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

basename=${0##*/}
os=${1,,}
os_version=${2,,}

if (($# != 2))
then
    echo "usage: $basename os os-version, e.g. ubuntu 22.04"
    exit
fi

org=nchain
repo=svn_dev
tag="${os}_${os_version}"

docker run -it $org/$repo:$tag /bin/bash

