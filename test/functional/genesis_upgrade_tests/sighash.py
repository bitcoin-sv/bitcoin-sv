#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from genesis_upgrade_tests.test_base import GenesisHeightTestsCaseBase, GenesisHeightBasedSimpleTestsCase
from test_framework.height_based_test_framework import SimpleTestDefinition

from test_framework.key import CECKey
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.script import CScript, SignatureHashForkId, SignatureHash, SIGHASH_ALL, \
    SIGHASH_FORKID, OP_CHECKSIG, OP_CHECKMULTISIG, OP_0, OP_1, OP_2, OP_FALSE, OP_RETURN

SIMPLE_OUTPUT_SCRIPT = CScript([OP_FALSE,OP_RETURN]) # Output script used by spend transactions. Could be anything that is standard, but OP_FALSE OP_RETURN is the easiest to create.

def make_key(bytes=b"randombytes"):
    key = CECKey()
    key.set_secretbytes(bytes)
    return key

class SighashCaseTest(GenesisHeightTestsCaseBase):

    NAME = "Sighash algorithm check tx size"
    _UTXO_KEY = make_key()
    ARGS = GenesisHeightTestsCaseBase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1',]

    def new_transaction(self, utxo, use_new_sighash_algorithm, target_tx_size):
        ndx, tx_to_spend = utxo
        padding_size = target_tx_size - 200
        for attempt in range(15):
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, ndx), b''))
            tx.vout.append(CTxOut(tx_to_spend.vout[0].nValue - 2*target_tx_size, SIMPLE_OUTPUT_SCRIPT))
            tx.vout.append(CTxOut(1, CScript([OP_FALSE, OP_RETURN] + [bytes(attempt)[:1] * padding_size])))
            if use_new_sighash_algorithm:
                sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[0].nValue)
                sig = self._UTXO_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
                tx.vin[0].scriptSig = CScript([sig])
                tx.rehash()
            else:
                sighash, _ = SignatureHash(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL)
                sig = self._UTXO_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))
                tx.vin[0].scriptSig = CScript([sig])
                tx.rehash()
            diff = target_tx_size - len(tx.serialize())
            if diff == 0:
                return tx
            padding_size += diff
        return None


    def get_transactions_for_test(self, tx_collection, coinbases):
        if tx_collection.label == "PRE-GENESIS":
            utxos, data = self.utxos["PRE-GENESIS"]
            tx_new_sighash = self.new_transaction(utxos.pop(0), use_new_sighash_algorithm=True, target_tx_size=1000000) # MAX_TX_SIZE before genesis
            tx_collection.add_tx(tx_new_sighash)
            tx_old_sighash = self.new_transaction(utxos.pop(0), use_new_sighash_algorithm=False, target_tx_size=500)
            tx_collection.add_tx(tx_old_sighash,
                                 p2p_reject_reason = b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        if tx_collection.label == "MEMPOOL AT GENESIS":
            utxos, data = self.utxos["PRE-GENESIS"]
            tx_old_sighash_max_size = self.new_transaction(utxos.pop(0), use_new_sighash_algorithm=False, target_tx_size=1000000)
            tx_collection.add_tx(tx_old_sighash_max_size)

            # TODO Uncomment these tests after CORE-287 MAX_STANDARD_TX_SIZE is implemented
            #tx_old_sighash_too_big = self.new_transaction(utxos.pop(0), use_new_sighash_algorithm=False, target_tx_size=1000001)
            #tx_collection.add_tx(tx_old_sighash_too_big,
            #                     p2p_reject_reason=b'mandatory-script-verify-flag-failed (Transaction too big for this sighash type)',
            #                     block_reject_reason=b'blk-bad-inputs')
            #tx_new_sighash = self.new_transaction(utxos.pop(0), use_new_sighash_algorithm=True, target_tx_size=1500000)
            #tx_collection.add_tx(tx_new_sighash)

        if tx_collection.label == "GENESIS":
            post_genesis_utxos, data = self.utxos["GENESIS"]
            # TODO Uncomment this tests after CORE-287 MAX_STANDARD_TX_SIZE is implemented
            #tx_new_sighash = self.new_transaction(post_genesis_utxos.pop(0), use_new_sighash_algorithm=True, target_tx_size=1500000)
            #tx_collection.add_tx(tx_new_sighash)
            tx_old_sighash = self.new_transaction(post_genesis_utxos.pop(0), use_new_sighash_algorithm=False, target_tx_size=500)
            tx_collection.add_tx(tx_old_sighash,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')


def make_unlock_with_old(tx, tx_to_spend):
    sighash, _ = SignatureHash(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL)
    sig = SighashSimpleCaseTest.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))
    return CScript([sig])

def make_unlock_with_new(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = SighashSimpleCaseTest.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([sig])


class SighashSimpleCaseTest(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', ]
    NAME = "Sighash Simple test"
    THE_KEY = make_key()
    P2PK_LOCKING_SCRIPT = CScript([THE_KEY.get_pubkey(), OP_CHECKSIG])
    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_with_new),
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_unlock_with_old,
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_unlock_with_new),
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_unlock_with_old),

        SimpleTestDefinition(None, P2PK_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_unlock_with_new),
        SimpleTestDefinition(None, P2PK_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_unlock_with_old,
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Signature must use SIGHASH_FORKID)',
                             block_reject_reason=b'blk-bad-inputs'),

        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_with_new),
        SimpleTestDefinition("PRE-GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_with_old),

        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_with_new),
        SimpleTestDefinition("GENESIS", P2PK_LOCKING_SCRIPT,
                             "GENESIS", make_unlock_with_old,
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Signature must use SIGHASH_FORKID)',
                             block_reject_reason=b'blk-bad-inputs'),
    ]

def make_multsig_unlock_with_old(tx, tx_to_spend):
    sighash, _ = SignatureHash(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL)
    sig = SighashMultisigSimpleCaseTest.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))
    return CScript([OP_0, sig])

def make_multsig_unlock_with_new(tx, tx_to_spend):
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                  tx_to_spend.vout[0].nValue)
    sig = SighashMultisigSimpleCaseTest.THE_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    return CScript([OP_0, sig])


class SighashMultisigSimpleCaseTest(GenesisHeightBasedSimpleTestsCase):
    ARGS = GenesisHeightBasedSimpleTestsCase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', ]
    NAME = "Sighash Simple test Multisig"
    THE_KEY = make_key()
    WRONG_KEY = make_key(b'otherbytes')
    MULTISIG_LOCKING_SCRIPT = CScript([OP_1, WRONG_KEY.get_pubkey(), THE_KEY.get_pubkey(), OP_2, OP_CHECKMULTISIG])
    TESTS = [
        SimpleTestDefinition("PRE-GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_multsig_unlock_with_new),
        SimpleTestDefinition("PRE-GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "PRE-GENESIS", make_multsig_unlock_with_old,
                             p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                             block_reject_reason=b'blk-bad-inputs'),
        
        SimpleTestDefinition("PRE-GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_multsig_unlock_with_new),
        SimpleTestDefinition("PRE-GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_multsig_unlock_with_old),
        
        SimpleTestDefinition(None, MULTISIG_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_multsig_unlock_with_new),
        SimpleTestDefinition(None, MULTISIG_LOCKING_SCRIPT,
                             "MEMPOOL AT GENESIS", make_multsig_unlock_with_old,
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Signature must use SIGHASH_FORKID)',
                             block_reject_reason=b'blk-bad-inputs'),
        
        SimpleTestDefinition("PRE-GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "GENESIS", make_multsig_unlock_with_new),
        SimpleTestDefinition("PRE-GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "GENESIS", make_multsig_unlock_with_old),
        
        SimpleTestDefinition("GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "GENESIS", make_multsig_unlock_with_new),
        SimpleTestDefinition("GENESIS", MULTISIG_LOCKING_SCRIPT,
                             "GENESIS", make_multsig_unlock_with_old,
                             p2p_reject_reason=b'non-mandatory-script-verify-flag (Signature must use SIGHASH_FORKID)',
                             block_reject_reason=b'blk-bad-inputs'),
    ]



if __name__ == '__main__':
    from test_framework.height_based_test_framework import SimplifiedTestFramework
    t = SimplifiedTestFramework([SighashSimpleCaseTest()])
    t.main()
