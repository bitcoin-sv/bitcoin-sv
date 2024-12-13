# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import OP_1, OP_ADD, OP_DROP, SIGHASH_ALL, SIGHASH_FORKID

"""
Test unlocking script PUSHONLY restriction removal at all heights.
"""


class SigPushOnlyTestCase(ChronicleHeightTestsCase):
    NAME = "Test unlocking script PUSHONLY restriction removal"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    NON_PUSH_UNLOCK = [OP_1, OP_1, OP_ADD, OP_DROP]

    def get_transactions_for_test(self, tx_collection, coinbases):
        if tx_collection.label == "PRE_GENESIS":
            utxos, _ = self.utxos["PRE_GENESIS"]

            # Before Genesis (but after BCH fork) we can spend non-PUSHDATA signed with forkid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

        # Before Chronicle
        elif tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Create tx with non-PUSHDATA opcodes in the unlocking script signed with forkid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Create tx with PUSHDATA only opcodes in the unlocking script signed with forkid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            # A multi-input tx, all inputs signed with forkid, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # A non-push-only tx signed with forkid is always a consensus failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A non-push-only tx signed without forkid might be valid after Chronicle
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A simple push-only tx signed with forkid is still valid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            # A multi-input tx, all inputs signed with forkid, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # A non-push-only tx signed with forkid is always a consensus failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A non-push-only tx signed without forkid is valid for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

            # A simple push-only tx signed with forkid is still valid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            # Check old UTXOs can again use non-PUSHDATA when signed without forkid
            pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)
            pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

            # A multi-input tx, all inputs signed without forkid one using non-PUSHDATA is valid for mining into the next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

            # A multi-input tx, inputs signed with & without forkid, some inputs use non-PUSHDATA; rejected
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed with forkid, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # A non-push-only tx signed with forkid is always a consensus failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A non-push-only tx signed without forkid is now valid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

            # A simple push-only tx signed with forkid is still valid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)])
            tx_collection.add_tx(tx)

            # Check old UTXOs can again use non-PUSHDATA when signed without forkid
            pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)
            pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

            # A multi-input tx, all inputs signed without forkid, some inputs use non-PUSHDATA; accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx)

            # A multi-input tx, inputs signed with & without forkid, some inputs use non-PUSHDATA; rejected
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed with forkid, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # A non-push-only tx signed with forkid is always a consensus failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A non-push-only tx signed without forkid is valid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

            # A multi-input tx, all inputs signed without forkid, some inputs use non-PUSHDATA; accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx)

            # A multi-input tx, inputs signed with & without forkid, some inputs use non-PUSHDATA; rejected
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed with forkid, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # A non-push-only tx signed with forkid is always a consensus failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A non-push-only tx signed without forkid is valid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

            # A multi-input tx, all inputs signed without forkid, some inputs use non-PUSHDATA; accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx)

            # A multi-input tx, inputs signed with & without forkid, some inputs use non-PUSHDATA; rejected
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed with forkid, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # A non-push-only tx signed with forkid is always a consensus failure
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A non-push-only tx signed without forkid is valid
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

            # A multi-input tx, all inputs signed without forkid, some inputs use non-PUSHDATA; accepted
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx)

            # A multi-input tx, inputs signed with & without forkid, some inputs use non-PUSHDATA; rejected
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed with forkid, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
