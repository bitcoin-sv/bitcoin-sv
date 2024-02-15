#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.key import CECKey
from genesis_upgrade_tests.test_base import GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.script import CScript, OP_TRUE, OP_HASH160, OP_EQUAL, hash160, OP_FALSE, OP_RETURN, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG


def make_key(bytes=b"randombytes"):
    key = CECKey()
    key.set_secretbytes(bytes)
    return key


def make_unlock_default(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = HandleTxsDefaultNode.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([sig])


def make_unlock_modified11(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = HandleTxsModified11Node.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([sig])


def make_unlock_modified10(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = HandleTxsModified10Node.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([sig])


def make_unlock_modified00(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = HandleTxsModified00Node.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([sig])

    class HandleTxsDefaultNode(GenesisHeightBasedSimpleTestsCase):
        ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-acceptnonstdtxn=0', '-acceptnonstdoutputs=1']
        NAME = "Reject nonstandard transactions and accept p2sh transactions before Genesis. Accept nonstandard and reject p2sh transactions after Genesis"

        THE_KEY = make_key()
        P2PK_LOCKING_SCRIPT = CScript([THE_KEY.get_pubkey(), OP_CHECKSIG])

        TEST_PRE_GENESIS_STANDARD_TX = [
            SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                                 "PRE-GENESIS", make_unlock_default)
        ]

        TEST_PRE_GENESIS_NONSTANDARD_TX = [
            SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                                 "PRE-GENESIS", make_unlock_default, test_tx_locking_script=CScript([OP_TRUE]),
                                 p2p_reject_reason=b'scriptpubkey')
        ]

        TEST_PRE_GENESIS_P2SH_TX = [
            SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                                 "PRE-GENESIS", make_unlock_default, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))
        ]

        TEST_GENESIS_STANDARD_TX = [
            SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                                 "GENESIS", make_unlock_default)
        ]

        TEST_GENESIS_NONSTANDARD_TX = [
            SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                                 "GENESIS", make_unlock_default, test_tx_locking_script=CScript([OP_TRUE]))
        ]

        # P2SH transaction will be rejected from p2p, but not rejected as part of the block
        TEST_GENESIS_P2SH_TX = [
            SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                                 "GENESIS", make_unlock_default, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]),
                                 p2p_reject_reason=b'bad-txns-vout-p2sh')
        ]

        TESTS = TEST_PRE_GENESIS_STANDARD_TX + TEST_PRE_GENESIS_NONSTANDARD_TX + TEST_PRE_GENESIS_P2SH_TX + TEST_GENESIS_STANDARD_TX + TEST_GENESIS_NONSTANDARD_TX + TEST_GENESIS_P2SH_TX


class HandleTxsModified11Node(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-acceptnonstdtxn=1', '-acceptnonstdoutputs=1']
    NAME = "Accept nonstandard transactions and p2sh transactions before Genesis. Accept nonstandard and reject p2sh transactions after Genesis"

    THE_KEY = make_key()
    P2PK_LOCKING_SCRIPT = CScript([THE_KEY.get_pubkey(), OP_CHECKSIG])

    TEST_PRE_GENESIS_STANDARD_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified11)
    ]

    TEST_PRE_GENESIS_NONSTANDARD_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified11, test_tx_locking_script=CScript([OP_TRUE]))
    ]

    TEST_PRE_GENESIS_P2SH_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified11, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))
    ]

    TEST_GENESIS_STANDARD_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified11)
    ]

    TEST_GENESIS_NONSTANDARD_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified11, test_tx_locking_script=CScript([OP_TRUE]))
    ]

    TEST_GENESIS_P2SH_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified11, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]),
                             p2p_reject_reason=b'flexible-bad-txns-vout-p2sh',
                             block_reject_reason=b'bad-txns-vout-p2sh')
    ]

    TESTS = TEST_PRE_GENESIS_STANDARD_TX + TEST_PRE_GENESIS_NONSTANDARD_TX + TEST_PRE_GENESIS_P2SH_TX + TEST_GENESIS_STANDARD_TX + TEST_GENESIS_NONSTANDARD_TX + TEST_GENESIS_P2SH_TX


class HandleTxsModified10Node(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-acceptnonstdtxn=1', '-acceptnonstdoutputs=0']
    NAME = "Accept nonstandard transactions and p2sh transactions before Genesis. Reject nonstandard and p2sh transactions after Genesis"

    THE_KEY = make_key()
    P2PK_LOCKING_SCRIPT = CScript([THE_KEY.get_pubkey(), OP_CHECKSIG])

    TEST_PRE_GENESIS_STANDARD_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified10)
    ]

    TEST_PRE_GENESIS_NONSTANDARD_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified10, test_tx_locking_script=CScript([OP_TRUE]))
    ]

    TEST_PRE_GENESIS_P2SH_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified10, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))
    ]

    TEST_GENESIS_STANDARD_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified10)
    ]

    TEST_GENESIS_NONSTANDARD_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified10, test_tx_locking_script=CScript([OP_TRUE]),
                             p2p_reject_reason=b'scriptpubkey')
    ]

    TEST_GENESIS_P2SH_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified10, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]),
                             p2p_reject_reason=b'flexible-bad-txns-vout-p2sh',
                             block_reject_reason=b'bad-txns-vout-p2sh')
    ]

    TESTS = TEST_PRE_GENESIS_STANDARD_TX + TEST_PRE_GENESIS_NONSTANDARD_TX + TEST_PRE_GENESIS_P2SH_TX + TEST_GENESIS_STANDARD_TX + TEST_GENESIS_NONSTANDARD_TX + TEST_GENESIS_P2SH_TX


class HandleTxsModified00Node(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-acceptnonstdtxn=0', '-acceptnonstdoutputs=0']
    NAME = "Reject nonstandard transactions and accept p2sh transactions before Genesis. Reject nonstandard and p2sh transactions after Genesis"

    THE_KEY = make_key()
    P2PK_LOCKING_SCRIPT = CScript([THE_KEY.get_pubkey(), OP_CHECKSIG])

    TEST_PRE_GENESIS_STANDARD_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified00)
    ]

    TEST_PRE_GENESIS_NONSTANDARD_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified00, test_tx_locking_script=CScript([OP_TRUE]),
                             p2p_reject_reason=b'scriptpubkey')
    ]

    TEST_PRE_GENESIS_P2SH_TX = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_modified00, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]))
    ]

    TEST_GENESIS_STANDARD_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified00)
    ]

    TEST_GENESIS_NONSTANDARD_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified00, test_tx_locking_script=CScript([OP_TRUE]),
                             p2p_reject_reason=b'scriptpubkey')
    ]

    TEST_GENESIS_P2SH_TX = [
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_modified00, test_tx_locking_script=CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL]),
                             p2p_reject_reason=b'flexible-bad-txns-vout-p2sh',
                             block_reject_reason=b'bad-txns-vout-p2sh')
    ]

    TESTS = TEST_PRE_GENESIS_STANDARD_TX + TEST_PRE_GENESIS_NONSTANDARD_TX + TEST_PRE_GENESIS_P2SH_TX + TEST_GENESIS_STANDARD_TX + TEST_GENESIS_NONSTANDARD_TX + TEST_GENESIS_P2SH_TX
