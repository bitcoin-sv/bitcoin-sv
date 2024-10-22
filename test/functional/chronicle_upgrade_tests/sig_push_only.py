# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import CScript, OP_CHECKSIG, OP_1, OP_ADD, OP_DROP, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.key import CECKey

"""
Test unlocking script PUSHONLY restriction removal at all heights.
"""


def make_key():
    key = CECKey()
    key.set_secretbytes(b"randombytes")
    return key


def new_transaction(key, utxo, unlocking_script=[]):
    n, tx_to_spend = utxo

    # Spend UTXO and create a txn with unlocking_script + the usual signature
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, n), b''))
    tx.vout.append(CTxOut(tx_to_spend.vout[0].nValue - 100, CScript([key.get_pubkey(), OP_CHECKSIG])))
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[0].nValue)
    sig = key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    tx.vin[0].scriptSig = CScript(unlocking_script + [sig])
    tx.rehash()

    return tx


class SigPushOnlyTestCase(ChronicleHeightTestsCase):
    NAME = "Test unlocking script PUSHONLY restriction removal"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = make_key()

    NON_PUSH_UNLOCK = [OP_1, OP_1, OP_ADD, OP_DROP]

    def get_transactions_for_test(self, tx_collection, coinbases):
        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Create tx with non-PUSHDATA opcodes in the unlocking script
            tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Create tx with PUSHDATA only opcodes in the unlocking script
            tx = new_transaction(self._UTXO_KEY, utxos.pop())
            tx_collection.add_tx(tx)

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            tx = new_transaction(self._UTXO_KEY, utxos.pop())
            tx_collection.add_tx(tx)

        # Start of Chronicle grace period
        #elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
        #    utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx,
        #                         p2p_reject_reason=b'chronicle-script-verify-flag-failed (Only non-push operators allowed in signatures)',
        #                         block_reject_reason=b'blk-bad-inputs')

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop())
        #    tx_collection.add_tx(tx)

        # Block before Chronicle activation
        #elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
        #    utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx,
        #                         p2p_reject_reason=b'chronicle-script-verify-flag-failed (Only non-push operators allowed in signatures)',
        #                         block_reject_reason=b'blk-bad-inputs')

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop())
        #    tx_collection.add_tx(tx)

        # Chronicle activation height
        #elif tx_collection.label == "CHRONICLE_ACTIVATION":
        #    utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Tx with non-PUSHDATA opcodes in the unlocking script is now accepted
        #    tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)

            # Tx with PUSHDATA only opcodes in the unlocking script accepted just as before
        #    tx = new_transaction(self._UTXO_KEY, utxos.pop())
        #    tx_collection.add_tx(tx)

            # Check a UTXO between Genesis and Chronicle can still only use PUSHDATA
        #    pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
        #    tx = new_transaction(self._UTXO_KEY, pre_chronicle_utxos[0], unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx,
        #                         p2p_reject_reason=b'chronicle-script-verify-flag-failed (Only non-push operators allowed in signatures)',
        #                         block_reject_reason=b'blk-bad-inputs')

            # Check a UTXO pre-Genesis can again use non-PUSHDATA
        #    pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
        #    tx = new_transaction(self._UTXO_KEY, pre_genesis_utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        #elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
        #    utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop())
        #    tx_collection.add_tx(tx)

        #    pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
        #    tx = new_transaction(self._UTXO_KEY, pre_chronicle_utxos[0], unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx,
        #                         p2p_reject_reason=b'chronicle-script-verify-flag-failed (Only non-push operators allowed in signatures)',
        #                         block_reject_reason=b'blk-bad-inputs')

        #    pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
        #    tx = new_transaction(self._UTXO_KEY, pre_genesis_utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)

        # End of chronicle grace period
        #elif tx_collection.label == "CHRONICLE_GRACE_END":
        #    utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop())
        #    tx_collection.add_tx(tx)

        #    pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
        #    tx = new_transaction(self._UTXO_KEY, pre_chronicle_utxos[0], unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx,
        #                         p2p_reject_reason=b'chronicle-script-verify-flag-failed (Only non-push operators allowed in signatures)',
        #                         block_reject_reason=b'blk-bad-inputs')

        #    pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
        #    tx = new_transaction(self._UTXO_KEY, pre_genesis_utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)

        # After Chronicle
        #elif tx_collection.label == "POST_CHRONICLE":
        #    utxos, _ = self.utxos["POST_CHRONICLE"]

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)

        #    tx = new_transaction(self._UTXO_KEY, utxos.pop())
        #    tx_collection.add_tx(tx)

        #    pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
        #    tx = new_transaction(self._UTXO_KEY, pre_chronicle_utxos[0], unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx,
        #                         p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
        #                         block_reject_reason=b'blk-bad-inputs')

        #    pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
        #    tx = new_transaction(self._UTXO_KEY, pre_genesis_utxos.pop(), unlocking_script=self.NON_PUSH_UNLOCK)
        #    tx_collection.add_tx(tx)
