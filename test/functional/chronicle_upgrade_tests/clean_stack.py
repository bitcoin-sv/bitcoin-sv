# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import OP_TRUE, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE

"""
P2P txns that don't leave a clean stack are rejected until we hit one block below
Chronicle activation height and are then accepted only if they are signed with
malleability permitting sighash flags for mining into the next block.

Blocks containing txns that don't clean their stack are always accepted as before.
"""


class CleanStackTestCase(ChronicleHeightTestsCase):
    NAME = "Reject txns without a clean stack before Chronicle and accept after"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    def get_transactions_for_test(self, tx_collection, coinbases):
        SIGHASH_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        SIGHASH_NON_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Txn created without clean stack, properly signed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # Txn created without clean stack, signed with Chronicle malleability allowed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txns created without clean stack signed with mixed malleability flags
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Txn created without clean stack, properly signed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # Txn created without clean stack, signed with Chronicle malleability allowed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txns created without clean stack signed with mixed malleability flags
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # A txn without a clean stack signed as non-malleable is always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # A txn without a clean stack signed as malleability allowed might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn created without clean stack, all inputs indicating non-malleable, is always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # Txn created without clean stack, all inputs indicating malleability allowed, might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txns created without clean stack signed with mixed malleability flags, might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # A txn without a clean stack signed as non-malleable is always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # A txn without a clean stack signed as malleability allowed can be accepted for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # Txn created without clean stack, all inputs indicating non-malleable, is a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # Txn created without clean stack, all inputs indicating malleability allowed, can be accepted for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            # Txns created without clean stack signed with mixed malleability flags, is a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # A txn without a clean stack signed as non-malleable; policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # A txn without a clean stack signed as malleability allowed; accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # Txn created without clean stack, all inputs indicating non-malleable; policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # Txn created without clean stack, all inputs indicating malleability allowed; accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            # Txns created without clean stack signed with mixed malleability flags; policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_MALLEABLE),
                self.Input(utxos.pop(), SIGHASH_NON_MALLEABLE, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
