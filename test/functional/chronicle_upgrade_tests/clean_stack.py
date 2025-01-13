# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import OP_TRUE, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_RELAX

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
        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Txn created without clean stack, properly signed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # Txn created without clean stack, signed with Chronicle malleability allowed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn created without clean stack signed with mixed malleability flags
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Txn created without clean stack, properly signed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # Txn created without clean stack, signed with Chronicle malleability allowed
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn created without clean stack signed with mixed malleability flags
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # A txn without a clean stack signed without ForkID and without Relax might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A txn without a clean stack signed without ForkID and with Relax might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A txn without a clean stack signed with ForkID and without Relax is always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # A txn without a clean stack signed with ForkID and with Relax might be accepted after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn created without clean stack, all inputs indicating malleability allowed
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txn created without clean stack signed with mixed malleability flags
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # A txn without a clean stack signed without ForkID and without Relax can be accepted for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # A txn without a clean stack signed without ForkID and with Relax can be accepted for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # A txn without a clean stack signed with ForkID and without Relax is always a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # A txn without a clean stack signed with ForkID and with Relax can be accepted for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # Txn created without clean stack, all inputs indicating malleability allowed can be accepted for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            # Txn created without clean stack signed with mixed malleability flags is a policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # A txn without a clean stack signed without ForkID and without Relax; accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # A txn without a clean stack signed without ForkID and with Relax; accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # A txn without a clean stack signed with ForkID and without Relax; policy failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            # A txn without a clean stack signed with ForkID and with Relax; accepted
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            # Txn created without clean stack, all inputs indicating malleability allowed; accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            # Txn created without clean stack signed with mixed malleability flags; policy failure
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE])])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_RELAX),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=[OP_TRUE]),
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Script did not clean its stack)')
