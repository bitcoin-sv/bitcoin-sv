# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.script import CScript, OP_1ADD, OP_DROP, OP_TRUE, OP_DUP, OP_MUL, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.cdefs import MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS, ONE_KILOBYTE
from test_framework.key import CECKey

"""
Test maximum script number size limit changes.
"""


def make_key():
    key = CECKey()
    key.set_secretbytes(b"randombytes")
    return key


def spend_script_num_transaction(utxo):
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(utxo.sha256, 0), CScript()))
    tx.vout.append(CTxOut(utxo.vout[0].nValue - 100, CScript([OP_TRUE])))
    tx.rehash()
    return tx


def new_transactions(key, utxo, script_num_size, locking_script=[OP_1ADD, OP_DROP, OP_TRUE]):
    n, tx_to_spend = utxo

    # Spend UTXO and create a txn with a large script num in its locking script
    tx1 = CTransaction()
    tx1.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, n), b''))
    tx1.vout.append(CTxOut(tx_to_spend.vout[0].nValue - 100_000, CScript([bytearray([42] * script_num_size)] + locking_script)))
    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx1, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[0].nValue)
    sig = key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
    tx1.vin[0].scriptSig = CScript([sig])
    tx1.rehash()

    # Create txn that will cause execution of previous txn locking script
    tx2 = spend_script_num_transaction(tx1)

    return tx1, tx2


"""
1) Pre-Chronicle check the old maximum script num size consensus rule still applies.

2) In the grace period before Chronicle we soft reject oversized script nums.

3) Once Chronicle activates we accept what were previously oversized script nums.

4) At all heights we continue to reject a txn that spends a UTXO mined between
   Genesis and Chronicle that contains a script num larger than consensus was then.
"""


class ScriptNumSizeDefaultPolicyTestCase(ChronicleHeightTestsCase):
    NAME = "Test maximum script number size limits (Default policy)"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1',
                                            '-maxscriptsizepolicy=0',
                                            '-maxnonstdtxvalidationduration=1000000',
                                            '-maxtxnvalidatorasynctasksrunduration=1000001']
    _UTXO_KEY = make_key()

    post_genesis_utxo = None

    def get_transactions_for_test(self, tx_collection, coinbases):
        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Create tx with script number equal to pre-Chronicle consensus maximum
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            # Create tx with script number over pre-Chronicle consensus maximum
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Save one of these pre-Chronicle post-genesis UTXOs for later
            self.post_genesis_utxo = tx1

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # No change from pre-Chronicle behaviour
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            # Now we soft reject the oversized script num
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'chronicle-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            # Will be accepted for mining into the next block
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            # But still reject an attempt to spend a post-Genesis UTXO with
            # script num larger than consensus at the time it was mined.
            tx2 = spend_script_num_transaction(self.post_genesis_utxo)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'chronicle-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # From now on we accept what used to be oversized script nums
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx2 = spend_script_num_transaction(self.post_genesis_utxo)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'chronicle-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx2 = spend_script_num_transaction(self.post_genesis_utxo)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'chronicle-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

        # End of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx2 = spend_script_num_transaction(self.post_genesis_utxo)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'chronicle-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx2 = spend_script_num_transaction(self.post_genesis_utxo)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script number overflow)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Check a ridiculously large script num in a calculation can't bring down the node
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), script_num_size=1000, locking_script=[OP_DUP, OP_MUL] * 50 + [OP_DROP, OP_TRUE])
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2,
                                 p2p_reject_reason=b'max-script-num-overflow (Big integer OpenSSL error)',
                                 block_reject_reason=b'blk-bad-inputs',
                                 p2p_timeout=300)


"""
At all heights, check that we observe any configured maximum script number size policy limit.
"""


class ScriptNumSizeSetPolicyTestCase(ChronicleHeightTestsCase):
    NAME = "Test maximum script number size limits with 10K set policy"
    MAX_SCRIPT_SIZE_POLICY = ONE_KILOBYTE * 10
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1', '-maxscriptsizepolicy=0', '-maxscriptnumlengthpolicy={}'.format(MAX_SCRIPT_SIZE_POLICY)]
    _UTXO_KEY = make_key()

    def get_transactions_for_test(self, tx_collection, coinbases):
        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Create tx with script number equal to policy maximum
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            # Create tx with script number over policy maximum
            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'max-script-num-length-policy-limit-violated (Script number overflow)')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'max-script-num-length-policy-limit-violated (Script number overflow)')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'non-mandatory-script-verify-flag (Script number overflow)')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'non-mandatory-script-verify-flag (Script number overflow)')

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'non-mandatory-script-verify-flag (Script number overflow)')

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'non-mandatory-script-verify-flag (Script number overflow)')

        # End of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'non-mandatory-script-verify-flag (Script number overflow)')

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2)

            tx1, tx2 = new_transactions(self._UTXO_KEY, utxos.pop(0), self.MAX_SCRIPT_SIZE_POLICY + 1)
            tx_collection.add_mempool_tx(tx1)
            tx_collection.add_tx(tx2, p2p_reject_reason=b'max-script-num-length-policy-limit-violated (Script number overflow)')
