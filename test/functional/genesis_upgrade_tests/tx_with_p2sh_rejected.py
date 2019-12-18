from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_TRUE, OP_HASH160, OP_EQUAL, hash160


class TxWithP2SHDefaultTestCase(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1']
    NAME = "Accept P2SH outputs before and reject after Genesis"

    TESTS_PRE_GENESIS_DEFAULT = [
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))
    ]

    TESTS_POST_GENESIS_DEFAULT = [
        SimpleTestDefinition("GENESIS", CScript([OP_TRUE]),
                             "GENESIS", b"", test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]),
                                        p2p_reject_reason=b'bad-txns-vout-p2sh')
    ]


    TESTS = TESTS_PRE_GENESIS_DEFAULT + TESTS_POST_GENESIS_DEFAULT


class TxWithP2SHAcceptTestCase(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-acceptp2sh_ashashpuzzle=1']
    NAME = "Accept P2SH outputs when acceptp2sh_ashashpuzzle is set"

    TESTS_PRE_GENESIS_ACCEPT = [
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))
    ]

    TESTS_POST_GENESIS_ACCEPT = [
        SimpleTestDefinition("GENESIS", CScript([OP_TRUE]),
                             "GENESIS", b"", test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))
    ]

    TESTS = TESTS_PRE_GENESIS_ACCEPT + TESTS_POST_GENESIS_ACCEPT