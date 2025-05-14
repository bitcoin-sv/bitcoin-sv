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
    def new_spending_transaction(self, inputs):
        tx = CTransaction()
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
    def create_transactions(self, utxos, multisigdetails, nulldummy, otherhashtype=None):
        # Create a transaction with a multisig output
        spend_tx = self.new_multisig_txn(utxos.pop())

        # Spend multisig, and optionally another output
        inputs = [self.Input((0, spend_tx), multisigdetails, nulldummy)]
        if otherhashtype:
            inputs += [self.Input(utxos.pop(), otherhashtype=otherhashtype)]
        tx = self.new_spending_transaction(inputs)

        return spend_tx, tx

    def get_transactions_for_test(self, tx_collection, coinbases):
        SIGHASH_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        SIGHASH_NON_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID
        NULL_BYTE = b''
        NON_NULL_BYTE = b'1'

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],   # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Single multisig input, null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],  # noqa
                NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_NON_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Single multisig input, non-null dummy, signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Single multisig input, non-null dummy, signed mixed-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_NON_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')

            # Multi-input, multi-sig null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],  # noqa
                NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed non-malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_NON_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero)')
            # Multi-input, multi-sig non-null dummy signed malleable, other input signed malleable
            spend_tx, tx = self.create_transactions(utxos,
                [ (self.KEY1, SIGHASH_MALLEABLE), (self.KEY2, SIGHASH_MALLEABLE) ],     # noqa
                NON_NULL_BYTE,
                SIGHASH_MALLEABLE)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
