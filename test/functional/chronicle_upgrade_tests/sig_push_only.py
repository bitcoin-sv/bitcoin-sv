# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import OP_1, OP_ADD, OP_DROP, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_RELAX

"""
Test unlocking script PUSHONLY restriction removal at all heights.
"""


class SigPushOnlyTestCase(ChronicleHeightTestsCase):
    NAME = "Test unlocking script PUSHONLY restriction removal"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    NON_PUSH_UNLOCK = [OP_1, OP_1, OP_ADD, OP_DROP]

    def new_transactions(self, utxos, sighash):

        # Spend UTXO and create a txn with non-PUSHDATA opcodes in the unlocking script
        input_non_push = [self.Input(utxos.pop(), sighash, unlocking_script=self.NON_PUSH_UNLOCK)]
        tx_non_push = self.new_transaction(self._UTXO_KEY, input_non_push)

        # Spend UTXO and create a txn with PUSHDATA only opcodes in the unlocking script
        input_push_only = [self.Input(utxos.pop(), sighash)]
        tx_push_only = self.new_transaction(self._UTXO_KEY, input_push_only)

        return tx_non_push, tx_push_only

    def get_transactions_for_test(self, tx_collection, coinbases):
        if tx_collection.label == "PRE_GENESIS":
            utxos, _ = self.utxos["PRE_GENESIS"]

            # Before Genesis (but after BCH fork) we can spend non-PUSHDATA and PUSHDATA only signed with forkid
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

        # Before Chronicle
        elif tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Txs signed without ForkID, without Relax; both are rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txs signed without ForkID, with Relax; both are rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Txs signed without ForkID, without Relax; both are rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txs signed without ForkID, with Relax; both are rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Txs signed without ForkID, without Relax; both are soft rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txs signed without ForkID, with Relax; both are soft rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are soft rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_RELAX)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Signature must use SIGHASH_FORKID)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Txs signed without ForkID, without Relax; both are accepted for mining into the next block
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed without ForkID, with Relax; both are accepted for mining into the next block
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are accepted for mining into the next block
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

            # Check old UTXOs can again use non-PUSHDATA when signed with malleability allowed
            pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)
            pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Txs signed without ForkID, without Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed without ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

            # Check old UTXOs can again use non-PUSHDATA when signed with malleability allowed
            pre_genesis_utxos, _ = self.utxos["PRE_GENESIS"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_genesis_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)
            pre_chronicle_utxos, _ = self.utxos["PRE_CHRONICLE"]
            tx = self.new_transaction(self._UTXO_KEY, [self.Input(pre_chronicle_utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK)])
            tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Txs signed without ForkID, without Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed without ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Txs signed without ForkID, without Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed without ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Txs signed without ForkID, without Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed without ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, without Relax; PUSHDATA only accepted, non-PUSHDATA rejected
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID)
            tx_collection.add_tx(tx_non_push,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')
            tx_collection.add_tx(tx_push_only)

            # Txs signed with ForkID, with Relax; both are accepted
            tx_non_push, tx_push_only = self.new_transactions(utxos, SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX)
            tx_collection.add_tx(tx_non_push)
            tx_collection.add_tx(tx_push_only)

            # A multi-input tx, all inputs signed non-malleable, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, mixed malleability signing, some inputs use non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID),
                self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_RELAX, unlocking_script=self.NON_PUSH_UNLOCK)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)',
                                 block_reject_reason=b'blk-bad-inputs')

            # A multi-input tx, all inputs signed malleable, one using non-PUSHDATA
            tx = self.new_transaction(self._UTXO_KEY, [
                self.Input(utxos.pop(), SIGHASH_ALL),
                self.Input(utxos.pop(), SIGHASH_ALL, unlocking_script=self.NON_PUSH_UNLOCK),
                self.Input(utxos.pop(), SIGHASH_ALL)
            ])
            tx_collection.add_tx(tx)
