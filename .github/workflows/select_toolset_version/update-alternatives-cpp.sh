#!/usr/bin/env bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

REMOVE=0

while getopts 'r' VAL; do
    case $VAL in
        r ) REMOVE=1;;
    esac
done
shift $((OPTIND -1))

if (($# != 2))
then
    echo "usage: $0 toolset version-number"
    exit
fi

toolset=${1,,}
version=$2
priority=$version

case ${toolset} in
    clang) ((priority += 100));;
esac

if (( $REMOVE )) 
then
    update-alternatives --remove-all cc
    update-alternatives --remove-all c++
fi

update-alternatives --install /usr/bin/c++ c++ /usr/bin/${toolset/gcc/g++}-$version $priority \
                    --slave   /usr/bin/cc  cc  /usr/bin/$toolset-$version

update-alternatives --set c++ /usr/bin/${toolset/gcc/g++}-$version
