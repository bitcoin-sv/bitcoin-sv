from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_TRUE, OP_CHECKSIG, OP_RETURN, OP_DROP
from test_framework.cdefs import MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS, MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS


class SigOpLimitCountDefaultTestCase(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1']
    NAME = "SigOps Count Limit Simple test"

    TESTS_PRE_GENESIS_DEFAULT = [

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS)),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * (MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS + 1)),
                                            p2p_reject_reason=b'bad-txns-too-many-sigops'),
        # Framework puts all the transactions that are considered valid into one block - added 2 transactions
        # with 1 sigop to drop the density
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG])),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG] + [b"a" * 500, OP_DROP]*1000)),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG] + [b"a" * 500, OP_DROP]*1000)),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG] * MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS),
                             p2p_reject_reason=b'bad-txns-too-many-sigops'),

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * (MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS + 1)),
                                            p2p_reject_reason=b'flexible-bad-txn-sigops',
                                            block_reject_reason=b'bad-txn-sigops'),
    ]

    TESTS_POST_GENESIS_DEFAULT = [
        SimpleTestDefinition("GENESIS", CScript([OP_TRUE]),
                             "GENESIS", b"", test_tx_locking_script=CScript(
            [OP_CHECKSIG] * (MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS + 1)))
    ]

    TESTS = TESTS_PRE_GENESIS_DEFAULT + TESTS_POST_GENESIS_DEFAULT


class SigOpLimitCountPolicyTestCase(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-maxtxsigopscountspolicy=9000']
    NAME = "SigOps Count Limit Simple test with policy parameter"

    TESTS_PRE_GENESIS_POLICY = [

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS)),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * (MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS + 1)),
                                            p2p_reject_reason=b'bad-txns-too-many-sigops'),
        # Framework puts all the transactions that are considered valid into one block - added 2 transactions
        # with 1 sigop to drop the density
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG])),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG] + [b"a" * 500, OP_DROP] * 1000)),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG] + [b"a" * 500, OP_DROP] * 1000)),

        # Sum of all sigops (4000+4001+20000) is compared to MAX_BLOCK_SIGOPS_PER_MB (20000), thus failing with bad-blk-sigops
        # MAX_BLOCK_SIGOPS_PER_MB should be increased to at least 22002
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"",
                             test_tx_locking_script=CScript([OP_CHECKSIG] * MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS),
                             p2p_reject_reason=b'bad-txns-too-many-sigops'),
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * (MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS + 1)),
                                            p2p_reject_reason=b'flexible-bad-txn-sigops',
                                            block_reject_reason=b'bad-txn-sigops'),
    ]

    TESTS_POST_GENESIS_POLICY = [

        SimpleTestDefinition("GENESIS", CScript([OP_TRUE]),
                             "GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * 9000)),
        SimpleTestDefinition("GENESIS", CScript([OP_TRUE]),
                             "GENESIS", b"", test_tx_locking_script=CScript([OP_CHECKSIG] * (9000 + 1)),
                             p2p_reject_reason=b'bad-txns-too-many-sigops'),
    ]

    TESTS = TESTS_PRE_GENESIS_POLICY + TESTS_POST_GENESIS_POLICY
