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
is appropriately signed.
"""


class MinimalEncodingTestCase(ChronicleHeightTestsCase):
    NAME = "Reject txns without minimal encoding of data & script nums before Chronicle and accept after"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()

    MINIMAL_DATA_PUSH = [OP_0, OP_DROP]
    LARGE_DATA_PUSH = [OP_PUSHDATA1, CScriptOp(1), CScriptOp(0), OP_DROP]

    MINIMALLY_ENCODED = [OP_0, OP_1ADD, OP_DROP]
    NON_MINIMALLY_ENCODED = [CScriptOp(2), CScriptOp(0), CScriptOp(0), OP_1ADD, OP_DROP]

    def create_txns(self, utxos, sighashes, locking_script):
        spend_tx = self.new_transaction(self._UTXO_KEY, [self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID)], output_locking_script=locking_script)

        # 1 input spends tx with special lock script using last sighash in list,
        # other (optional) inputs spend ordinary utxos with remaing sighashes
        inputs = [self.Input((0, spend_tx), sighashes.pop())] + [self.Input(utxos.pop(), sighash) for sighash in sighashes]
        tx = self.new_transaction(self._UTXO_KEY, inputs)

        return spend_tx, tx

    def get_transactions_for_test(self, tx_collection, coinbases):
        SIGHASH_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        SIGHASH_NON_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, multi-input, mixed malleability; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, multi-input, mixed malleability; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, multi-input, mixed malleability; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, multi-input, mixed malleability; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Large data push, multi-input, mixed malleability; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Data push larger than necessary)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Non-minimally encoded, multi-input, mixed malleability; rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Non-minimally encoded script number)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; accepted for mining into the next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; accepted for mining into next block
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Minimal data push, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMAL_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Large data push, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Large data push, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.LARGE_DATA_PUSH)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Data push larger than necessary)')

            # Minimally encoded, signed non-malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, signed non-malleable; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_NON_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')

            # Non-minimally encoded, signed malleable; accepted
            spend_tx, tx = self.create_txns(utxos, [SIGHASH_MALLEABLE], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)

            # Non-minimally encoded, multi-input, mixed malleability; P2P rejected
            spend_tx, tx = self.create_txns(utxos, [
                SIGHASH_NON_MALLEABLE,
                SIGHASH_MALLEABLE,
                SIGHASH_MALLEABLE
            ], self.NON_MINIMALLY_ENCODED)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'non-mandatory-script-verify-flag (Non-minimally encoded script number)')
