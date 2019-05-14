#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Imports some application default values from source files outside the test
framework, and defines equivalents of consensus parameters for the test
framework.
"""

import os
import re

from test_framework.util import get_srcdir

# Slurp in policy.h contents
_policy_h_fh = open(os.path.join(get_srcdir(), 'src', 'policy',
                                    'policy.h'), 'rt')
_policy_h_contents = _policy_h_fh.read()
_policy_h_fh.close()

# This constant is currently needed to evaluate some that are formulas
ONE_MEGABYTE = 1000000
ONE_GIGABYTE = 1000000000

def _extractPolicyValue(name):
    return eval(re.search(name + ' = (.+);', _policy_h_contents).group(1))


# Extract relevant default values parameters

# The maximum allowed block size before the fork
LEGACY_MAX_BLOCK_SIZE = ONE_MEGABYTE

# Default settings for maximum allowed size for a block, in bytes - berfore and after activation of new rules
REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME = _extractPolicyValue('REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME')
REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE = _extractPolicyValue('REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE')
REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER =  _extractPolicyValue('REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER')
REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = _extractPolicyValue('REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE')
REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = _extractPolicyValue('REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER')


# The following consensus parameters should not be automatically imported.
# They *should* cause test failures if application code is changed in ways
# that violate current consensus.

# The maximum allowed number of signature check operations per MB in a block
# (network rule)
MAX_BLOCK_SIGOPS_PER_MB = 20000

# The maximum allowed number of signature check operations per transaction
# (network rule)
MAX_TX_SIGOPS_COUNT = 20000

# The maximum number of sigops we're willing to relay/mine in a single tx
# (policy.h constant)
MAX_STANDARD_TX_SIGOPS = MAX_TX_SIGOPS_COUNT // 5

# Coinbase transaction outputs can only be spent after this number of new
# blocks (network rule)
COINBASE_MATURITY = 100

# Anti replay OP_RETURN commitment.
ANTI_REPLAY_COMMITMENT = b"Bitcoin: A Peer-to-Peer Electronic Cash System"

# The maximum allowed size for a transaction, in bytes
MAX_TX_SIZE = ONE_MEGABYTE


if __name__ == "__main__":
    # Output values if run standalone to verify
    print("REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME = %d" % REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME)
    print("REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE = %d (bytes)" % REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE)
    print("REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER = %d (bytes)" % REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER)
    print("REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = %d (bytes)" % REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE)
    print("REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = %d (bytes)" % REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)
    print("MAX_BLOCK_SIGOPS_PER_MB = %d (sigops)" % MAX_BLOCK_SIGOPS_PER_MB)
    print("MAX_TX_SIGOPS_COUNT = %d (sigops)" % MAX_TX_SIGOPS_COUNT)
    print("COINBASE_MATURITY = %d (blocks)" % COINBASE_MATURITY)
