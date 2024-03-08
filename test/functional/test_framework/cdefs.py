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
_policy_h_fh = open(os.path.join(get_srcdir(), 'src', 'policy', 'policy.h'), 'rt')
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

# Slurp in script.h contents
_script_h_fh = open(os.path.join(get_srcdir(), 'src', 'script', 'script.h'),'rt')
_script_h_contents = _script_h_fh.read()
_script_h_fh.close()

# Slurp in limitedstack.h contents
_limitedstack_h_fh = open(os.path.join(get_srcdir(), 'src', 'script',
                                       'limitedstack.h'), 'rt')
_limitedstack_h_contents = _limitedstack_h_fh.read()
_limitedstack_h_fh.close()

# Slurp in validation.h contents
_validation_h_fh = open(os.path.join(get_srcdir(), 'src',
                                     'validation.h'), 'rt')
_validation_h_contents = _validation_h_fh.read()
_validation_h_fh.close()

# Slurp in text_writer.h contents
_textwriter_h_fh = open(os.path.join(get_srcdir(), 'src', 'rpc',
                                     'text_writer.h'), 'rt')
_textwriter_h_contents = _textwriter_h_fh.read()
_textwriter_h_fh.close()

# Slurp in txn_validation_config.h contents
_txnvalidationconfig_h_fh = open(os.path.join(get_srcdir(),
                                              'src',
                                              'txn_validation_config.h'), 'rt')
_txnvalidationconfig_h_contents = _txnvalidationconfig_h_fh.read()
_txnvalidationconfig_h_fh.close()

# Slurp in txn_validator.h contents
_txnvalidator_h_fh = open(os.path.join(get_srcdir(),
                                       'src',
                                       'txn_validator.h'), 'rt')
_txnvalidator_h_contents = _txnvalidator_h_fh.read()
_txnvalidator_h_fh.close()


def _extractConsensusValue(name):
    return int(eval(re.search(name + ' = (.+);', _consensus_h_contents).group(1)))


INT64_MAX = 2**63-1
UINT32_MAX = 2**32-1
# This constant is currently needed to evaluate some that are formulas
ONE_KILOBYTE = 1000
ONE_MEGABYTE = 1000000
ONE_GIGABYTE = 1000000000


def _extractPolicyValue(name):
    return int(eval(re.search(name + ' = (.+);', _policy_h_contents).group(1)))


def _extractChainParamsValue(name):
    return eval(re.search('#define ' + name + ' (.+)', _chainparams_cpp_contents).group(1))


def _extractScriptValue(name):
    return int(eval(re.search(name + ' = (.+);', _script_h_contents).group(1)))


def _extractLimitedStackValue(name):
    return int(eval(re.search(name + ' = (.+);', _limitedstack_h_contents).group(1)))


def _extractValidationValue(name):
    return int(eval(re.search(name + ' = (.+);', _validation_h_contents).group(1)))


def _extractTextWriterValue(name):
    return int(eval(re.search(name + ' = (.+);', _textwriter_h_contents).group(1)))


def _extractTxnValidationConfigValue(name):
    return int(eval(re.search(name + " =\n\t.*\{(\d+)\};", _txnvalidationconfig_h_contents).group(1)))


def _extractTxnValidatorValue(name):
    return int(eval(re.search(name + " \{\n.*\((\d+)\)", _txnvalidator_h_contents).group(1)))


# Extract relevant default values parameters

# The maximum allowed block size before the fork
LEGACY_MAX_BLOCK_SIZE = ONE_MEGABYTE

# Default settings for maximum allowed size for a block, in bytes - berfore and after activation of new rules
REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME = _extractPolicyValue('REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME')
REGTEST_DEFAULT_MAX_BLOCK_SIZE = _extractPolicyValue('REGTEST_DEFAULT_MAX_BLOCK_SIZE')
REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = _extractPolicyValue('REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE')
REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = _extractPolicyValue('REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER')

MAX_TX_SIZE_POLICY_BEFORE_GENESIS = _extractPolicyValue('MAX_TX_SIZE_POLICY_BEFORE_GENESIS')
DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS = _extractPolicyValue('DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS')

GENESIS_ACTIVATION_HEIGHT_REGTEST = _extractChainParamsValue('GENESIS_ACTIVATION_REGTEST')

# The following consensus parameters should not be automatically imported.
# They *should* cause test failures if application code is changed in ways
# that violate current consensus.

# The maximum allowed number of signature check operations per MB in a block
# (network rule)
MAX_BLOCK_SIGOPS_PER_MB = 20000

# The maximum allowed number of signature check operations per transaction
# (network rule)
MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS = _extractConsensusValue('MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS')

# The maximum number of sigops we're willing to relay/mine in a single tx
# (policy.h constant)
MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS = _extractPolicyValue('MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS')
MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS = _extractPolicyValue('MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS')
# The default maximum number of sigops we're willing to relay/mine in a single tx after genesis
# (policy.h constant)
DEFAULT_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS = _extractPolicyValue('DEFAULT_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS')


# The maximum allowed number of signatures per multisis op before genesis
# (network rule)
MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS = _extractConsensusValue('MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS')
# The default maximum number of sigs in multisig operation we're willing to relay/mine in a single multisig operation inside a script after genesis
# (policy.h constant)
DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS = _extractPolicyValue('DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS')

# Coinbase transaction outputs can only be spent after this number of new
# blocks (network rule)
COINBASE_MATURITY = 100

# The maximum allowed size for a transaction, in bytes
MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS = _extractConsensusValue('MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS')
MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS = _extractConsensusValue('MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS')

#Gracefull period for genesis activation where nodes will not be banned for certain ops
GENESIS_GRACEFULL_ACTIVATION_PERIOD = _extractPolicyValue('GENESIS_GRACEFULL_ACTIVATION_PERIOD')

# Maximum number of non-push operations per script before GENESIS
MAX_OPS_PER_SCRIPT_BEFORE_GENESIS = _extractConsensusValue('MAX_OPS_PER_SCRIPT_BEFORE_GENESIS')

MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS = _extractScriptValue('MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS')
MAX_STACK_ELEMENTS_BEFORE_GENESIS = _extractScriptValue('MAX_STACK_ELEMENTS_BEFORE_GENESIS')
ELEMENT_OVERHEAD = _extractLimitedStackValue('ELEMENT_OVERHEAD')
# Maximum script length in bytes before Genesis
MAX_SCRIPT_SIZE_BEFORE_GENESIS = _extractConsensusValue('MAX_SCRIPT_SIZE_BEFORE_GENESIS')

# Maximum length of numbers used in scripts before genesis
MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS = _extractConsensusValue('MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS')

# Maximum length of numbers used in scripts after genesis
MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS = _extractConsensusValue('MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS')
DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS = _extractPolicyValue('DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS')

MIN_TTOR_VALIDATION_DISTANCE = _extractValidationValue('MIN_TTOR_VALIDATION_DISTANCE')

SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE = _extractValidationValue('SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE')
SAFE_MODE_DEFAULT_MIN_FORK_LENGTH = _extractValidationValue('SAFE_MODE_DEFAULT_MIN_FORK_LENGTH')
SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE = _extractValidationValue('SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE')
DEFAULT_MIN_BLOCKS_TO_KEEP = _extractValidationValue('DEFAULT_MIN_BLOCKS_TO_KEEP')

BUFFER_SIZE_HttpTextWriter = _extractTextWriterValue('BUFFER_SIZE')

DEFAULT_MAX_STD_TXN_VALIDATION_DURATION = _extractTxnValidationConfigValue('DEFAULT_MAX_STD_TXN_VALIDATION_DURATION')
DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION = _extractTxnValidatorValue('DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION')

if __name__ == "__main__":
    # Output values if run standalone to verify
    print("REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME = %d" % REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME)
    print("REGTEST_DEFAULT_MAX_BLOCK_SIZE = %d (bytes)" % REGTEST_DEFAULT_MAX_BLOCK_SIZE)
    print("REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = %d (bytes)" % REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE)
    print("REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = %d (bytes)" % REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)
    print("MAX_BLOCK_SIGOPS_PER_MB = %d (sigops)" % MAX_BLOCK_SIGOPS_PER_MB)
    print("MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS = %d (sigops)" % MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS)
    print("COINBASE_MATURITY = %d (blocks)" % COINBASE_MATURITY)
    print("MAX_TX_SIZE_POLICY_BEFORE_GENESIS = %d" % MAX_TX_SIZE_POLICY_BEFORE_GENESIS)
    print("DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS = %d" % DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS)
    print("MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS = %d" % MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS)
    print("MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS = %d" % MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS)
    print("GENESIS_ACTIVATION_HEIGHT_REGTEST = %d" % GENESIS_ACTIVATION_HEIGHT_REGTEST)
    print("MAX_OPS_PER_SCRIPT_BEFORE_GENESIS = %d" % MAX_OPS_PER_SCRIPT_BEFORE_GENESIS)
    print("MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS = %d" % MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS)
    print("MAX_STACK_ELEMENTS_BEFORE_GENESIS = %d" % MAX_STACK_ELEMENTS_BEFORE_GENESIS)
    print("ELEMENT_OVERHEAD = %d" % ELEMENT_OVERHEAD)
    print("MAX_SCRIPT_SIZE_BEFORE_GENESIS = %d" % MAX_SCRIPT_SIZE_BEFORE_GENESIS)

    print("MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS = %d" % MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS)
    print("MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS = %d" % MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS)
    print("DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS = %d" % DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS)
    print("MIN_TTOR_VALIDATION_DISTANCE = %d" % MIN_TTOR_VALIDATION_DISTANCE)

    print("SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE = %d" % SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE)
    print("SAFE_MODE_DEFAULT_MIN_FORK_LENGTH = %d" % SAFE_MODE_DEFAULT_MIN_FORK_LENGTH)
    print("SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE = %d" % SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE)

    print("BUFFER_SIZE_HttpTextWriter = %d" % BUFFER_SIZE_HttpTextWriter)

    print("DEFAULT_MAX_STD_TXN_VALIDATION_DURATION = %d" % DEFAULT_MAX_STD_TXN_VALIDATION_DURATION)

    print("DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION = %d" % DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION)
