# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE
from test_framework.key import SECP256K1_ORDER_HALF

"""
Test that High-S signatures are invalid before Chronicle but accepted afterwards
provided they contain an appropriate version number.
"""


class LowSRemovalTestCase(ChronicleHeightTestsCase):
    NAME = "Test Low-S requirement removal"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()
    _NUMBER_OF_UTXOS_PER_HEIGHT = 48

    def new_transactions(self, utxos, sighashes, version):

        def high_s_sig(hash):
            while True:
                sig = self._UTXO_KEY.sign(hash, low_s=False)
                r_size = sig[3]
                s_size = sig[5 + r_size]
                s_value = int.from_bytes(sig[6 + r_size:6 + r_size + s_size], byteorder='big')
                if s_value > SECP256K1_ORDER_HALF:
                    return sig

        def sign_tx_high_s(sighash, hash_type):
            return high_s_sig(sighash) + bytes(bytearray([hash_type]))

        # Spend UTXO and create a txn with a low-s signature
        inputs_low = [self.Input(utxos.pop(), sighash) for sighash in sighashes]
        tx_low = self.new_transaction(self._UTXO_KEY, inputs_low, version)

        # Spend UTXO and create a txn with a high-s signature
        inputs_high = [self.Input(utxos.pop(), sighash) for sighash in sighashes]
        tx_high = self.new_transaction(self._UTXO_KEY, inputs_high, version, sign_tx_high_s)

        return tx_low, tx_high

    def get_transactions_for_test(self, tx_collection, coinbases):

        SIGHASH_NTDA = SIGHASH_ALL | SIGHASH_FORKID
        SIGHASH_OTDA = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        VERSION_NON_MALLEABLE = 1
        VERSION_MALLEABLE = 2

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # High and low S, 1 input, NTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # High and low S, multi-sig, all NTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # High and low S, 1 input, NTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # High and low S, multi-sig, all NTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # High and low S, 1 input, NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version - low S accepted, high S will be accepted after Chronicle
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, non-malleable version - low S will be accepted after Chronicle, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version - both will be accepted after Chronicle
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # High and low S, multi-sig, all NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version - low S accepted, high S will be accepted after Chronicle
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version - low S will be accepted after Chronicle, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version - both will be accepted after Chronicle
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # High and low S, 1 input, NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version - low S accepted, high S accepted for mining into next block
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, 1 input, OTDA, non-malleable version - low S accepted for mining into next block, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version - both accepted for mining into next block
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # High and low S, multi-sig, all NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version - low S accepted, high S accepted for mining into next block
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version - low S accepted for mining into next block, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version - both accepted for mining into next block
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # High and low S, 1 input, NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, 1 input, OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # High and low S, multi-sig, all NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # High and low S, 1 input, NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, 1 input, OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # High and low S, multi-sig, all NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # High and low S, 1 input, NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, 1 input, OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # High and low S, multi-sig, all NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # High and low S, 1 input, NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, 1 input, OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, 1 input, OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # High and low S, multi-sig, all NTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, all NTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
            # High and low S, multi-sig, mixed NTDA & OTDA, non-malleable version - low S accepted, high S rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')
            # High and low S, multi-sig, mixed NTDA & OTDA, malleable version - both accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_NTDA,
                SIGHASH_OTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
