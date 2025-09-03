# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import OP_TRUE, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE

"""
P2P txns that don't leave a clean stack are rejected until we hit one block below
Chronicle activation height and are then accepted only if they contain a
malleability permitting version number for mining into the next block.

Blocks containing txns that don't clean their stack are always accepted as before.
"""


class CleanStackTestCase(ChronicleHeightTestsCase):
    NAME = "Reject txns without a clean stack before Chronicle and accept after"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()
    _NUMBER_OF_UTXOS_PER_HEIGHT = 48

    def get_transactions_for_test(self, tx_collection, coinbases):

        SIGHASH_NTDA = SIGHASH_ALL | SIGHASH_FORKID
        SIGHASH_OTDA = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        VERSION_NON_MALLEABLE = 1
        VERSION_MALLEABLE = 2

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn without a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn without a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn without a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn without a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn without a clean stack, 1 input, NTDA, non-malleable version - always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version - will be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, non-malleable version - might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn without a clean stack, 1 input, OTDA, malleable version - might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version - always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version - will be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version - might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version - might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, 1 input, NTDA, non-malleable version - always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version - accepted for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, 1 input, OTDA, non-malleable version - now a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, malleable version - accepted for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version - always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version - accepted for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version - now a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version - accepted for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, 1 input, NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, 1 input, OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, 1 input, NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, 1 input, OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, 1 input, NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, 1 input, OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Txn with a clean stack, 1 input, NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, 1 input, OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn with a clean stack, multi-sig, all NTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, all NTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn with a clean stack, multi-sig, mixed NTDA & OTDA, malleable version
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, 1 input, NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, 1 input, OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, 1 input, OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE])], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Txn without a clean stack, multi-sig, all NTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, all NTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, non-malleable version - policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
            # Txn without a clean stack, multi-sig, mixed NTDA & OTDA, malleable version - accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA, unlocking_script=[OP_TRUE]),
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
