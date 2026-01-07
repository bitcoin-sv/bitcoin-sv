# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.script import CScript, SignatureHash, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE
from test_framework.script import OP_0, OP_2, OP_3, OP_CHECKMULTISIGVERIFY, OP_CHECKSIGVERIFY, OP_CHECKSIG, OP_CODESEPARATOR

"""
Test behaviour of CHECKSIG in scriptSig at all heights.

The new Chronicle script signing rules for CHECKSIG operations
within a scriptSig are rejected until a block before the Chronicle
activation height, after which they are accepted.
"""


class CheckSigScriptSigTestCase(ChronicleHeightTestsCase):
    NAME = "Check behaviour of CHECKSIG in scriptSig before and after Chronicle activation"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    KEY0 = ChronicleHeightTestsCase.make_key(b'secret0')
    KEY1 = ChronicleHeightTestsCase.make_key(b'secret1')
    KEY2 = ChronicleHeightTestsCase.make_key(b'secret2')

    # Create txns with CHECKSIG and CHECKMULTISIG in scriptSig
    def create_transactions(self, utxos, sighashflags, version):

        # Create a transaction spending the given UTXO, with a simple scriptPubKey output.
        # This transaction and signature will be used as the first part of the scriptSig
        # in the final transactions.
        def txn(tx_to_spend, n):
            tx = CTransaction()
            tx.nVersion = version
            tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, n), b''))
            total_input = tx_to_spend.vout[n].nValue
            tx.vout.append(CTxOut(total_input - 100, CScript([self._UTXO_KEY.get_pubkey(), OP_CHECKSIG])))
            sighash = SignatureHash(tx_to_spend.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[n].nValue)
            sig = self._UTXO_KEY.sign(sighash) + bytes([SIGHASH_ALL | SIGHASH_FORKID])
            return tx, sig

        # Join a number of raw scripts into one script
        def script_concat(*args):
            result = bytearray()
            for arg in args:
                result.extend(arg)
            return CScript(result)

        # Create txn with CHECKSIG in scriptSig
        # UTXOSig, Key0Sig, OCS, PubKey0, CHECKSIGVERIFY | UTXOKey, CHECKSIG
        n, tx_to_spend = utxos.pop()
        tx1, sig = txn(tx_to_spend, n)
        scriptSigChecksig = CScript([self.KEY0.get_pubkey(), OP_CHECKSIGVERIFY])
        scriptSigSubScript = script_concat(scriptSigChecksig, tx_to_spend.vout[n].scriptPubKey)
        sighash = SignatureHash(scriptSigSubScript, tx1, 0, sighashflags, tx_to_spend.vout[n].nValue)
        othersig = self.KEY0.sign(sighash) + bytes([sighashflags])
        tx1.vin[0].scriptSig = script_concat(CScript([sig] + [othersig] + [OP_CODESEPARATOR]), scriptSigChecksig)

        # Create txn with CHECKMULTISIG in scriptSig
        # UTXOSig, OP_0, Key0Sig, Key1Sig, OCS, OP_2, PubKey0, PubKey1, PubKey2, OP_3, CHECKMULTISIGVERIFY | UTXOKey, CHECKSIG
        n, tx_to_spend = utxos.pop()
        tx2, sig = txn(tx_to_spend, n)
        scriptSigCheckMultisig = CScript([OP_2, self.KEY0.get_pubkey(), self.KEY1.get_pubkey(), self.KEY2.get_pubkey(), OP_3, OP_CHECKMULTISIGVERIFY])
        scriptSigSubScript = script_concat(scriptSigCheckMultisig, tx_to_spend.vout[n].scriptPubKey)
        sighash = SignatureHash(scriptSigSubScript, tx2, 0, sighashflags, tx_to_spend.vout[n].nValue)
        othersig0 = self.KEY0.sign(sighash) + bytes([sighashflags])
        othersig1 = self.KEY1.sign(sighash) + bytes([sighashflags])
        tx2.vin[0].scriptSig = script_concat(CScript([sig] + [OP_0] + [othersig0] + [othersig1] + [OP_CODESEPARATOR]), scriptSigCheckMultisig)

        tx1.rehash()
        tx2.rehash()
        return tx1, tx2

    def get_transactions_for_test(self, tx_collection, coinbases):

        SIGHASH_NTDA = SIGHASH_ALL | SIGHASH_FORKID
        SIGHASH_OTDA = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        VERSION_NON_MALLEABLE = 1
        VERSION_MALLEABLE = 2

        # Helper to create and add transactions to collection, either with an expected failure
        # reason or with success.
        def create_and_add_txns(utxos, sighashflags, version, p2p_reject_reason=None):
            tx1, tx2 = self.create_transactions(utxos, sighashflags, version)
            if p2p_reject_reason:
                tx_collection.add_tx(tx1, p2p_reject_reason=p2p_reject_reason, block_reject_reason=b'blk-bad-inputs')
                tx_collection.add_tx(tx2, p2p_reject_reason=p2p_reject_reason, block_reject_reason=b'blk-bad-inputs')
            else:
                tx_collection.add_tx(tx1)
                tx_collection.add_tx(tx2)

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE, b'flexible-mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE, b'flexible-mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # Now we can start accepting appropriately signed transactions for mining into the next block

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + NTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + non-malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            # CHECKSIGVERIFY/CHECKMULTISIGVERIFY + OTDA + malleable version
            create_and_add_txns(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
