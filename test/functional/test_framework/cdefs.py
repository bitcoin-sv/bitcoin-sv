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

# Slurp in chainparams.cpp contents
_chainparams_cpp_fh = open(os.path.join(get_srcdir(), 'src', 
                                    'chainparams.cpp'), 'rt')
_chainparams_cpp_contents = _chainparams_cpp_fh.read()
_chainparams_cpp_fh.close()


# Slurp in consensus.h contents
_consensus_h_fh = open(os.path.join(get_srcdir(), 'src', 'consensus', 'consensus.h'), 'rt')
_consensus_h_contents = _consensus_h_fh.read()
_consensus_h_fh.close()

def _extractConsensusValue(name):
    return int(eval(re.search(name + ' = (.+);', _consensus_h_contents).group(1)))

UINT32_MAX = 2**32-1
# This constant is currently needed to evaluate some that are formulas
ONE_MEGABYTE = 1000000
ONE_GIGABYTE = 1000000000

def _extractPolicyValue(name):
    return int(eval(re.search(name + ' = (.+);', _policy_h_contents).group(1)))

def _extractChainParamsValue(name):
    return eval(re.search('#define ' + name + ' (.+)', _chainparams_cpp_contents).group(1))

# Extract relevant default values parameters

# The maximum allowed block size before the fork
LEGACY_MAX_BLOCK_SIZE = ONE_MEGABYTE

# Default settings for maximum allowed size for a block, in bytes - berfore and after activation of new rules
REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME = _extractPolicyValue('REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME')
REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS = _extractPolicyValue('REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS')
REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER_GENESIS =  _extractPolicyValue('REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER_GENESIS')
REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = _extractPolicyValue('REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE')
REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = _extractPolicyValue('REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER')

MAX_STANDARD_TX_SIZE = _extractPolicyValue('MAX_STANDARD_TX_SIZE')

GENESIS_ACTIVATION_HEIGHT_REGTEST = _extractChainParamsValue('GENESIS_ACTIVATION_REGTEST')

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

# The maximum allowed size for a transaction, in bytes
MAX_TX_SIZE = ONE_MEGABYTE

# Maximum number of non-push operations per script before GENESIS 
MAX_OPS_PER_SCRIPT_BEFORE_GENESIS = _extractConsensusValue('MAX_OPS_PER_SCRIPT_BEFORE_GENESIS')

if __name__ == "__main__":
    # Output values if run standalone to verify
    print("REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME = %d" % REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME)
    print("REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS = %d (bytes)" % REGTEST_DEFAULT_MAX_BLOCK_SIZE_BEFORE_GENESIS)
    print("REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER_GENESIS = %d (bytes)" % REGTEST_DEFAULT_MAX_BLOCK_SIZE_AFTER_GENESIS)
    print("REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = %d (bytes)" % REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE)
    print("REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = %d (bytes)" % REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)
    print("MAX_BLOCK_SIGOPS_PER_MB = %d (sigops)" % MAX_BLOCK_SIGOPS_PER_MB)
    print("MAX_TX_SIGOPS_COUNT = %d (sigops)" % MAX_TX_SIGOPS_COUNT)
    print("COINBASE_MATURITY = %d (blocks)" % COINBASE_MATURITY)
    print("MAX_STANDARD_TX_SIZE = %d" % MAX_STANDARD_TX_SIZE)
    print("GENESIS_ACTIVATION_HEIGHT_REGTEST = %d" % GENESIS_ACTIVATION_HEIGHT_REGTEST)
    print("MAX_OPS_PER_SCRIPT_BEFORE_GENESIS = %d" % MAX_OPS_PER_SCRIPT_BEFORE_GENESIS)
