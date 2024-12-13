# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import SIGHASH_ALL, SIGHASH_FORKID

"""
Test the operation of the forkid within signatures at all heights.
"""


class ForkIDTestCase(ChronicleHeightTestsCase):
    NAME = "Test ForkID becomes optional after Chronicle"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    def get_transactions_for_test(self, tx_collection, coinbases):
        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Create tx signed with forkid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            # Create tx signed without forkid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed with forkid
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            # A multi-input tx, inputs signed with & without forkid
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            # When Chronicle activates we could accept txn signed without ForkID
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            # When Chronicle activates we could accept txn signed without ForkID
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            # Can now accept txn signed without ForkID for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            # Can now accept txn signed without ForkID for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)
