# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import OP_1, OP_ADD, OP_DROP, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE

"""
Test unlocking script PUSHONLY restriction removal at all heights.
"""


class SigPushOnlyTestCase(ChronicleHeightTestsCase):
    NAME = "Test unlocking script PUSHONLY restriction removal"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    NON_PUSH_UNLOCK = [OP_1, OP_1, OP_ADD, OP_DROP]

    def new_transactions(self, utxos, sighash, version):

        # Spend UTXO and create a txn with non-PUSHDATA opcodes in the unlocking script
        input_non_push = [self.Input(utxos.pop(), sighash, unlocking_script=self.NON_PUSH_UNLOCK)]
        tx_non_push = self.new_transaction(self._UTXO_KEY, input_non_push, version)

        # Spend UTXO and create a txn with PUSHDATA only opcodes in the unlocking script
        input_push_only = [self.Input(utxos.pop(), sighash)]
        tx_push_only = self.new_transaction(self._UTXO_KEY, input_push_only, version)

        return tx_non_push, tx_push_only

    def get_transactions_for_test(self, tx_collection, coinbases):

        SIGHASH_NTDA = SIGHASH_ALL | SIGHASH_FORKID
        SIGHASH_OTDA = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        VERSION_NON_MALLEABLE = 1
        VERSION_MALLEABLE = 2

        if tx_collection.label == "PRE_GENESIS":
            utxos, _ = self.utxos["PRE_GENESIS"]

            # Before Genesis (but after BCH fork) we can spend non-PUSHDATA and PUSHDATA only with NTDA & any version
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Signed with OTDA, both are rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Before Chronicle
        elif tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # 1 input, OTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # 1 input, OTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # 1 input, OTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable; push only accepted, non-push accepted for mining into next block
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable; push only accepted for mining into next block, non-push rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version malleable; both accepted for mining into next block
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable; accepted for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable; accepted for mining into next block
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Check old UTXOs can again use non-PUSHDATA when using malleable version number
            pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_OTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_OTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

            # Check old UTXOs can again use non-PUSHDATA when using malleable version number
            pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_OTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_OTDA, unlocking_script=self.NON_PUSH_UNLOCK)], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # 1 input, NTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, NTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_NTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version non-malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)
            # 1 input, OTDA, version malleable
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_OTDA, VERSION_MALLEABLE)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, all NTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version non-malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_NON_MALLEABLE)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            # multi-sig, mixed NTDA & OTDA, some inputs use non-PUSHDATA, version malleable
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_NTDA),
                self.Input(utxos.pop(), SIGHASH_OTDA),
                self.Input(utxos.pop(), SIGHASH_NTDA, unlocking_script=self.NON_PUSH_UNLOCK)
            ], VERSION_MALLEABLE)
            tx_collection.add_tx(tx)
