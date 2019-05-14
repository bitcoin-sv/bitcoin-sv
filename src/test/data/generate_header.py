#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

import os
import sys


def main(test_name, input_file):
    with open(input_file, "rb") as f:
        contents = f.read()

    print("namespace json_tests{")
    print("   static unsigned const char {}[] = {{".format(test_name))
    print(", ".join(map(lambda x: "0x{:02x}".format(x), contents)))
    print(" };")
    print("};")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("We need additional pylons!")
        os.exit(1)

    main(sys.argv[1], sys.argv[2])
