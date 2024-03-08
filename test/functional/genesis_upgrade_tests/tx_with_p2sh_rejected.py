from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_TRUE, OP_HASH160, OP_EQUAL, hash160


class TxWithP2SHOutputTestCase(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1']
    NAME = "Accept P2SH outputs before and reject after Genesis"

    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", CScript([OP_TRUE]),
                             "PRE-GENESIS", b"", test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL])),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_TRUE]),
                             "MEMPOOL AT GENESIS", b"",
                             test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]),
                             p2p_reject_reason=b'flexible-bad-txns-vout-p2sh',
                             block_reject_reason=b'bad-txns-vout-p2sh'),

        SimpleTestDefinition("GENESIS", CScript([OP_TRUE]),
                             "GENESIS", b"",
                             test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]),
                             p2p_reject_reason=b'flexible-bad-txns-vout-p2sh',
                             block_reject_reason=b'bad-txns-vout-p2sh')
    ]
