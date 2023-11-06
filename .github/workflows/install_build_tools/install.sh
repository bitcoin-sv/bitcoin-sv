#!/usr/bin/env bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

if (($# != 2))
then
    echo "usage: $0 toolset version-number"
    exit
fi

toolset=${1,,}
version=$2

apt-get update && apt-get -y install build-essential

case ${toolset} in
    gcc)   apt-get -y install gcc-$version g++-$version;;
    clang) apt-get -y install $toolset-$version
esac

