# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.script import CScript, SignatureHash, OP_2, OP_3, OP_CHECKMULTISIG, OP_CHECKSIG, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE

"""
Test relaxation of NULLDUMMY script validation rule.
"""


class NullDummyTestCase(ChronicleHeightTestsCase):
    NAME = "Reject multisig txns without a null dummy value before Chronicle and accept after"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()
    _NUMBER_OF_UTXOS_PER_HEIGHT = 72

    KEY1 = ChronicleHeightTestsCase.make_key(b'secret1')
    KEY2 = ChronicleHeightTestsCase.make_key(b'secret2')
    KEY3 = ChronicleHeightTestsCase.make_key(b'secret3')

    # Details about an input for spending
    class Input:
        utxo = None
        multisigdetails = None
        otherhashtype = None
        nulldummy = None

        def __init__(self, utxo, multisigdetails=None, nulldummy=None, otherhashtype=None):
            self.utxo = utxo
            self.multisigdetails = multisigdetails
            self.nulldummy = nulldummy
            self.otherhashtype = otherhashtype

    # Create a transaction that spends a multisig output, and optionally other outputs
    def new_spending_transaction(self, inputs, version):
        tx = CTransaction()
        tx.nVersion = version
        total_input = 0

        # Add inputs
        for inp in inputs:
            utxo = inp.utxo
            n, tx_to_spend = utxo

            tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, n), b''))
            total_input += tx_to_spend.vout[n].nValue

        # Add output
        tx.vout.append(CTxOut(total_input - 100, CScript([self._UTXO_KEY.get_pubkey(), OP_CHECKSIG])))

        # Sign
        for index, (inp, tx_input) in enumerate(zip(inputs, tx.vin)):
            utxo = inp.utxo
            n, tx_to_spend = utxo

            if index == 0:
                # Index 0 is the multisig input
                nulldummy = inp.nulldummy
                sigs = []
                for sigdetail in inp.multisigdetails:
                    key, hash_type = sigdetail
                    sighash = SignatureHash(tx_to_spend.vout[n].scriptPubKey, tx, index, hash_type, tx_to_spend.vout[n].nValue)
                    sig = key.sign(sighash) + bytes(bytearray([hash_type]))
                    sigs.append(sig)
                tx_input.scriptSig = CScript([nulldummy] + sigs)
            else:
                hash_type = inp.otherhashtype
                sighash = SignatureHash(tx_to_spend.vout[n].scriptPubKey, tx, index, hash_type, tx_to_spend.vout[n].nValue)
                sig = self._UTXO_KEY.sign(sighash) + bytes(bytearray([hash_type]))
                tx_input.scriptSig = CScript([sig])

        tx.rehash()
        return tx

    # Create a transaction with a multisig output
    def new_multisig_txn(self, utxo):
        n, tx_to_spend = utxo

        # Add input
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, n), b''))
        total_input = tx_to_spend.vout[n].nValue

        # Add multisig output
        tx.vout.append(CTxOut(total_input - 100, CScript([OP_2, self.KEY1.get_pubkey(), self.KEY2.get_pubkey(), self.KEY3.get_pubkey(), OP_3, OP_CHECKMULTISIG])))

        # Sign
        sighash = SignatureHash(tx_to_spend.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[n].nValue)
        sig = self._UTXO_KEY.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
        tx.vin[0].scriptSig = CScript([sig])

        tx.rehash()
        return tx

    # Create a MULTISIG txn and another that spends it
    def create_transactions(self, utxos, multisigdetails, version, nulldummy, otherhashtype=None):
        # Create a transaction with a multisig output
        spend_tx = self.new_multisig_txn(utxos.pop())

        # Spend multisig, and optionally another output
        inputs = [self.Input((0, spend_tx), multisigdetails, nulldummy)]
        if otherhashtype:
            inputs += [self.Input(utxos.pop(), otherhashtype=otherhashtype)]
        tx = self.new_spending_transaction(inputs, version)

        return spend_tx, tx

    def get_transactions_for_test(self, tx_collection, coinbases):

        SIGHASH_NTDA = SIGHASH_ALL | SIGHASH_FORKID
        SIGHASH_OTDA = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        VERSION_NON_MALLEABLE = 1
        VERSION_MALLEABLE = 2
        NULL_BYTE = b''
        NON_NULL_BYTE = b'1'

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')

            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, single multisig input, OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, single multisig input, OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all NTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NTDA), (self.KEY2, SIGHASH_NTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other NTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_NTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, non-malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_NON_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Non-null dummy, multi-input, multisig all OTDA, other OTDA, malleable version
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_OTDA), (self.KEY2, SIGHASH_OTDA) ],   # noqa
                VERSION_MALLEABLE,
                NON_NULL_BYTE,
                SIGHASH_OTDA)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
