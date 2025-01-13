# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_RELAX
from test_framework.key import SECP256K1_ORDER_HALF

"""
Test that High-S signatures are invalid before Chronicle but accepted afterwards
provided they are signed without ForkID.
"""


class LowSRemovalTestCase(ChronicleHeightTestsCase):
    NAME = "Test Low-S requirement removal"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    def new_transactions(self, utxos, sighashes):

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
        tx_low = self.new_transaction(self._UTXO_KEY, inputs_low)

        # Spend UTXO and create a txn with a high-s signature
        inputs_high = [self.Input(utxos.pop(), sighash) for sighash in sighashes]
        tx_high = self.new_transaction(self._UTXO_KEY, inputs_high, sign_tx_high_s)

        return tx_low, tx_high

    def get_transactions_for_test(self, tx_collection, coinbases):
        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Signed without ForkID, without Relax; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed without ForkID, with Relax; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with mixed malleability flags; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Signed without ForkID, without Relax; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed without ForkID, with Relax; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with mixed malleability flags; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Signed without ForkID, without Relax; Both are soft rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed without ForkID, with Relax; Both are soft rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are soft rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with mixed malleability flags; Low-S signature is soft rejected, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are soft rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Signed without ForkID, without Relax; Both are accepted for mining into the next block
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed without ForkID, with Relax; Both are accepted for mining into the next block
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are accepted for mining into the next block
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with mixed malleability flags; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are accepted for mining into the next block
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Signed without ForkID, without Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed without ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with mixed malleability flags; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Signed without ForkID, without Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed without ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with mixed malleability flags; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Signed without ForkID, without Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed without ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with mixed malleability flags; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Signed without ForkID, without Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed without ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with ForkID, without Relax; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Signed with ForkID, with Relax; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)

            # Signed with mixed malleability flags; Low-S signature is accepted, High-S signature is rejected
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_FORKID,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-canonical signature: S value is unnecessarily high)',
                                 block_reject_reason=b'blk-bad-inputs')

            # All signed with malleability allowed flags; Both are accepted
            tx_low, tx_high = self.new_transactions(utxos, [
                SIGHASH_ALL,
                SIGHASH_ALL | SIGHASH_RELAX,
                SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX
            ])
            tx_collection.add_tx(tx_low)
            tx_collection.add_tx(tx_high)
