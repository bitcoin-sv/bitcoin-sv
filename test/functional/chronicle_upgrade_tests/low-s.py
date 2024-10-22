# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import CScript, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.key import CECKey
from test_framework.blocktools import create_tx
from test_framework.key import SECP256K1_ORDER_HALF

"""
Test that High-S signatures are invalid before Chronicle but accepted afterwards.
"""


def make_key():
    key = CECKey()
    key.set_secretbytes(b"randombytes")
    return key


def new_transactions(key, utxos):

    def high_s_sig(key, hash):
        while True:
            sig = key.sign(hash, low_s=False)
            r_size = sig[3]
            s_size = sig[5 + r_size]
            s_value = int.from_bytes(sig[6 + r_size:6 + r_size + s_size], byteorder='big')
            if s_value > SECP256K1_ORDER_HALF:
                return sig

    def sign_tx(tx, spendtx, n, key, high_s):
        sighash = SignatureHashForkId(spendtx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spendtx.vout[n].nValue)
        sig = high_s_sig(key, sighash) if high_s else key.sign(sighash)
        tx.vin[0].scriptSig = CScript([sig + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    # Spend UTXO and create a txn with a low-s signature
    n, spendtx = utxos[0]
    tx1 = create_tx(spendtx, n, value=spendtx.vout[0].nValue - 100, script=CScript([key.get_pubkey(), OP_CHECKSIG]))
    sign_tx(tx1, spendtx, 0, key, False)
    tx1.rehash()

    # Spend UTXO and create a txn with a high-s signature
    n, spendtx = utxos[1]
    tx2 = create_tx(spendtx, n, value=spendtx.vout[0].nValue - 100, script=CScript([key.get_pubkey(), OP_CHECKSIG]))
    sign_tx(tx2, spendtx, 0, key, True)
    tx2.rehash()

    return tx1, tx2


class LowSRemovalTestCase(ChronicleHeightTestsCase):
    NAME = "Test Low-S requirement removal"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = make_key()

    def get_transactions_for_test(self, tx_collection, coinbases):
        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Low-S signature transaction is accepted, High-S signature transaction is rejected
            tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
            tx_collection.add_tx(tx1)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Low-S signature transaction is accepted, High-S signature transaction is rejected
            tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
            tx_collection.add_tx(tx1)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        #elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
        #    utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Low-S signature transaction is accepted, High-S signature transaction is rejected but not banned
        #    tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
        #    tx_collection.add_tx(tx1)
        #    tx_collection.add_tx(tx2,
        #                         p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
        #                         block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        #elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
        #    utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Low-S signature transaction is accepted, High-S signature transaction is accepted (for mining into next block)
        #    tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
        #    tx_collection.add_tx(tx1)
        #    tx_collection.add_tx(tx2)

        # Chronicle activation height
        #elif tx_collection.label == "CHRONICLE_ACTIVATION":
        #    utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Low-S signature transaction is accepted, High-S signature transaction is accepted
        #    tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
        #    tx_collection.add_tx(tx1)
        #    tx_collection.add_tx(tx2)

        # Block after Chronicle activation height
        #elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
        #    utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Low-S signature transaction is accepted, High-S signature transaction is accepted
        #    tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
        #    tx_collection.add_tx(tx1)
        #    tx_collection.add_tx(tx2)

        # End of chronicle grace period
        #elif tx_collection.label == "CHRONICLE_GRACE_END":
        #    utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Low-S signature transaction is accepted, High-S signature transaction is accepted
        #    tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
        #    tx_collection.add_tx(tx1)
        #    tx_collection.add_tx(tx2)

        # After Chronicle
        #elif tx_collection.label == "POST_CHRONICLE":
        #    utxos, _ = self.utxos["POST_CHRONICLE"]

            # Low-S signature transaction is accepted, High-S signature transaction is accepted
        #    tx1, tx2 = new_transactions(self._UTXO_KEY, [utxos.pop(0), utxos.pop(0)])
        #    tx_collection.add_tx(tx1)
        #    tx_collection.add_tx(tx2)
