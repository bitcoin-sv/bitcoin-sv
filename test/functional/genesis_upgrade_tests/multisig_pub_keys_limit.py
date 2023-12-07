from genesis_upgrade_tests.test_base import GenesisHeightTestsCaseBase, GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.cdefs import MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS
from test_framework.script import CScript, OP_FALSE, OP_RETURN, SignatureHashForkId, SignatureHash, SIGHASH_ALL, \
    SIGHASH_FORKID, OP_CHECKSIG, OP_0, OP_1, OP_CHECKMULTISIG, OP_TRUE, OP_DROP


def make_key():
    key = CECKey()
    key.set_secretbytes(b"randombytes1")
    return key


def makePubKeys(numOfKeys):
    key = CECKey()
    key.set_secretbytes(b"randombytes2")
    return [key.get_pubkey()] * numOfKeys


def make_unlock_script(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = MaxMultiSigTest.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([OP_0, sig])


class MaxMultiSigTest(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1','-maxpubkeyspermultisigpolicy=100']
    NAME = "Max multi signature test"
    THE_KEY = make_key()
    PUBKEYS10 = makePubKeys(10)
    PUBKEYS19 = makePubKeys(MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS - 1)
    PUBKEYS20 = makePubKeys(MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS)
    PUBKEYS99 = makePubKeys(99)
    PUBKEYS100 = makePubKeys(100)
    TESTS = [


        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1] + PUBKEYS19 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS, OP_CHECKMULTISIG]),
                             "MEMPOOL AT GENESIS", make_unlock_script),

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1] + PUBKEYS10 + [THE_KEY.get_pubkey(), 11, OP_CHECKMULTISIG]),
                             "MEMPOOL AT GENESIS", make_unlock_script),

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1] + PUBKEYS20 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS + 1, OP_CHECKMULTISIG]),
                             "MEMPOOL AT GENESIS", make_unlock_script,
                             b"genesis-script-verify-flag-failed (Pubkey count negative or limit exceeded)",
                             b"blk-bad-inputs"),

        SimpleTestDefinition(None, CScript([OP_1] + PUBKEYS20 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS + 1, OP_CHECKMULTISIG]),
                             "MEMPOOL AT GENESIS", make_unlock_script),

        SimpleTestDefinition(None, CScript([OP_1] + PUBKEYS10 + [THE_KEY.get_pubkey(), 11, OP_CHECKMULTISIG]),
                             "MEMPOOL AT GENESIS", make_unlock_script),

        SimpleTestDefinition(None, CScript([OP_1] + PUBKEYS99 + [THE_KEY.get_pubkey(), 100, OP_CHECKMULTISIG]),
                             "MEMPOOL AT GENESIS", make_unlock_script),

        SimpleTestDefinition(None, CScript([OP_1] + PUBKEYS100 + [THE_KEY.get_pubkey(), 101, OP_CHECKMULTISIG]),
                             "MEMPOOL AT GENESIS", make_unlock_script,
                             b"non-mandatory-script-verify-flag (Pubkey count negative or limit exceeded)"),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_1] + PUBKEYS19 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_1] + PUBKEYS10 + [THE_KEY.get_pubkey(), 11, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script),

        SimpleTestDefinition("MEMPOOL AT GENESIS", CScript([OP_1] + PUBKEYS20 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS + 1, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script,
                             b"genesis-script-verify-flag-failed (Pubkey count negative or limit exceeded)",
                             b"blk-bad-inputs"),

        SimpleTestDefinition("GENESIS", CScript([OP_1] + PUBKEYS10 + [THE_KEY.get_pubkey(), 11, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script),

        SimpleTestDefinition("GENESIS", CScript([OP_1] + PUBKEYS20 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS + 1, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script),

        SimpleTestDefinition("GENESIS", CScript([OP_1] + PUBKEYS99 + [THE_KEY.get_pubkey(), 100, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script),

        SimpleTestDefinition("GENESIS", CScript([OP_1] + PUBKEYS100 + [THE_KEY.get_pubkey(), 101, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script,
                             b"non-mandatory-script-verify-flag (Pubkey count negative or limit exceeded)")

    ]


def make_unlock_script_for_default(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = MaxMultiSigTestPolicyNotSet.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([OP_0, sig])


class MaxMultiSigTestPolicyNotSet(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1']
    NAME = "Max multi signature test with no policy rule set"
    THE_KEY = make_key()
    PUBKEYS19 = makePubKeys(MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS - 1)
    PUBKEYS20 = makePubKeys(MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS)
    TESTS = [

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1] + PUBKEYS19 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS, OP_CHECKMULTISIG]),
                             "PRE-GENESIS", make_unlock_script_for_default),

        SimpleTestDefinition("PRE-GENESIS", CScript([OP_1] + PUBKEYS20 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS + 1, OP_CHECKMULTISIG]),
                             "PRE-GENESIS", make_unlock_script_for_default,
                             b"genesis-script-verify-flag-failed (Pubkey count negative or limit exceeded)",
                             b"blk-bad-inputs"),

        SimpleTestDefinition("GENESIS", CScript([OP_1] + PUBKEYS19 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script_for_default),

        SimpleTestDefinition("GENESIS", CScript([OP_1] + PUBKEYS20 + [THE_KEY.get_pubkey(), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS + 1, OP_CHECKMULTISIG]),
                             "GENESIS", make_unlock_script_for_default)

    ]
