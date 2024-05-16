#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests import tests
from test_framework.height_based_test_framework import SimplifiedTestFramework

import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    args, unknown_args = parser.parse_known_args()

    t = SimplifiedTestFramework([t() for t in tests(unknown_args if len(unknown_args) > 0 else None)])
    t.main()
