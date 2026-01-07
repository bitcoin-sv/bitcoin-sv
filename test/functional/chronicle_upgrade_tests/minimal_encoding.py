# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import CScriptOp, OP_DROP, OP_PUSHDATA1, OP_0, OP_1ADD, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE

"""
Test that before Chronicle we reject scripts without minimal encoding of
pushdata and script nums.

Test that during the Chronicle grace period we soft-reject such non-minimally
encoded data and script nums.

Test that after Chronicle we accept non-minimally encoded values provided tx
uses an appropriate version number.
"""


class MinimalEncodingTestCase(ChronicleHeightTestsCase):
    NAME = "Reject txns without minimal encoding of data & script nums before Chronicle and accept after"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()
    _NUMBER_OF_UTXOS_PER_HEIGHT = 72

    MINIMAL_DATA_PUSH = [OP_0, OP_DROP]
    LARGE_DATA_PUSH = [OP_PUSHDATA1, CScriptOp(1), CScriptOp(0), OP_DROP]

    MINIMALLY_ENCODED = [OP_0, OP_1ADD, OP_DROP]
    NON_MINIMALLY_ENCODED = [CScriptOp(2), CScriptOp(0), CScriptOp(0), OP_1ADD, OP_DROP]

    def create_txns(self, utxos, sighashes, version, locking_script):
        spend_tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)], output_locking_script=locking_script)

        # 1 input spends tx with special lock script using last sighash in list,
        # other (optional) inputs spend ordinary utxos with remaing sighashes
        inputs = [self.Input((0, spend_tx), sighashes.pop())] + [self.Input(utxos.pop(), sighash) for sighash in sighashes]
        tx = self.new_transaction(self._UTXO_KEY, inputs, version)

        return spend_tx, tx

    def get_transactions_for_test(self, tx_collection, coinbases):

        SIGHASH_NTDA = SIGHASH_ALL | SIGHASH_FORKID
        SIGHASH_OTDA = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        VERSION_NON_MALLEABLE = 1
        VERSION_MALLEABLE = 2

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimal data push, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Large data push, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimally encoded, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-minimally encoded, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimal data push, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Large data push, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimally encoded, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-minimally encoded, 1 input, OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimal data push, 1 input, OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, 1 input, NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Large data push, 1 input, OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, multi-sig, all NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; policy failure after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimally encoded, 1 input, OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; policy failure after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-minimally encoded, 1 input, OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; policy failure after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; may be accepted after Chronicle
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, 1 input, NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, 1 input, OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-sig, all NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, 1 input, NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, 1 input, OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-sig, all NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, 1 input, NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, 1 input, OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-sig, all NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, 1 input, NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, 1 input, OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-sig, all NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Minimal data push, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimal data push, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimal data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, 1 input, NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, 1 input, OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-sig, all NTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Large data push, multi-sig, mixed NTDA & OTDA, non-malleable version; policy failure
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')
            # Large data push, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, 1 input, NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Minimally encoded, multi-sig, all NTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, 1 input, NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, 1 input, OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, 1 input, OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_OTDA], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-sig, all NTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, all NTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_NTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, non-malleable version; policy rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_NON_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
            # Non-minimally encoded, multi-sig, mixed NTDA & OTDA, malleable version; accepted
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NTDA,
                SIGHASH_NTDA,
                SIGHASH_OTDA
            ], VERSION_MALLEABLE, self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
