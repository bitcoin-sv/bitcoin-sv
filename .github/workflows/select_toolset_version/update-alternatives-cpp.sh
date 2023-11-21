#!/usr/bin/env bash
# Copyright (c) 2023 Bitcoin Association Distributed under the Open BSV
# software license, see the accompanying file LICENSE

set -eu

function Display_Help
{
    cat <<EoH

Uses update-alternatives to select a C/C++ toolset, version and associated tools. 

    usage: $PROGRAM toolset version-number

    e.g. "$PROGRAM clang 17"

    Options:
      -h = Show this help message and exit
      -r = Remove existing update-alternatives for cc, c++ and clang-tidy
EoH
    exit 0
}

PROGRAM=${0##*/}
REMOVE=0

while getopts 'hr' VAL; do
    case $VAL in
        h ) Display_Help;;
        r ) REMOVE=1;;
    esac
done
shift $((OPTIND -1))

(($# != 2)) && Display_Help

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
    update-alternatives --remove-all clang-tidy
fi

if [[ $toolset == "gcc" ]];
then
    update-alternatives \
        --install /usr/bin/c++ c++ /usr/bin/${toolset/gcc/g++}-$version $priority \
        --slave   /usr/bin/cc  cc  /usr/bin/$toolset-$version
elif [[ $toolset == "clang" ]]
then
    update-alternatives \
        --install /usr/bin/c++ c++ /usr/bin/${toolset}-$version $priority \
        --slave   /usr/bin/cc  cc  /usr/bin/$toolset-$version \
        --slave   /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-$version
fi

update-alternatives --set c++ /usr/bin/${toolset/gcc/g++}-$version

