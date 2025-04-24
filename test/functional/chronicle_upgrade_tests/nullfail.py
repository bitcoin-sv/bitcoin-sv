# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from chronicle_upgrade_tests.test_base import ChronicleHeightTestsCase
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.script import CScript, SignatureHash, OP_CHECKSIG, OP_DUP, OP_NOTIF, OP_ENDIF, OP_VERIFY, SIGHASH_ALL, SIGHASH_FORKID, SIGHASH_CHRONICLE

"""
Test the operation of the Chronicle NULLFAIL changes at all block heights.
"""


class NullFailTestCase(ChronicleHeightTestsCase):
    NAME = "Test NULLFAIL"
    ARGS = ChronicleHeightTestsCase.ARGS + ['-whitelist=127.0.0.1']
    BAD_KEY = ChronicleHeightTestsCase.make_key(b"wibble")
    _UTXO_KEY = ChronicleHeightTestsCase.make_key()
    _NUMBER_OF_UTXOS_PER_HEIGHT = 50

    # Details about an input for spending
    class Input:
        utxo = None
        hash_type = None
        key = None
        unlocking_script = None

        def __init__(self, utxo, hash_type, key, unlocking_script=[]):
            self.utxo = utxo
            self.hash_type = hash_type
            self.key = key
            self.unlocking_script = unlocking_script

    # Spend UTXOs and create a txn with unlocking_script + signature
    def new_transaction(self, inputs, output_locking_script=None):

        tx = CTransaction()
        total_input = 0

        # Add inputs
        for inp in inputs:
            utxo = inp.utxo
            n, tx_to_spend = utxo

            tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, n), b''))
            total_input += tx_to_spend.vout[n].nValue

        # 1 output
        if output_locking_script is not None:
            tx.vout.append(CTxOut(total_input - 100, CScript(output_locking_script)))
        else:
            tx.vout.append(CTxOut(total_input - 100, CScript([self._UTXO_KEY.get_pubkey(), OP_CHECKSIG])))

        # Txn signing function
        def sign_txn_input(key, sighash, hash_type):
            if hash_type is None or key is None:
                # Null sig
                return b''
            return key.sign(sighash) + bytes(bytearray([hash_type]))

        # Sign
        for index, (inp, tx_input) in enumerate(zip(inputs, tx.vin)):
            utxo = inp.utxo
            hash_type = inp.hash_type
            unlocking_script = inp.unlocking_script
            key = inp.key
            n, tx_to_spend = utxo

            sighash = SignatureHash(tx_to_spend.vout[n].scriptPubKey, tx, index, hash_type, tx_to_spend.vout[n].nValue) if hash_type is not None else None
            sig = sign_txn_input(key, sighash, hash_type)
            tx_input.scriptSig = CScript(unlocking_script + [sig])

        tx.rehash()
        return tx

    # Create a single txn with the given inputs
    def create_transaction(self, utxos, sigdetails):
        inputs = [self.Input(utxos.pop(), sigdetail[0], sigdetail[1]) for sigdetail in sigdetails]
        return self.new_transaction(inputs)

    # Create a txn with the given locking script and another that spends it
    def create_transactions(self, utxos, sigdetails, locking_script=[]):
        spend_tx = self.new_transaction([self.Input(utxos.pop(), SIGHASH_ALL | SIGHASH_FORKID, self._UTXO_KEY)], output_locking_script=locking_script)

        # 1 input spends tx with special lock script using last sighash in list,
        # other (optional) inputs spend ordinary utxos with remaining sighashes
        first_input_sigdetail = sigdetails.pop()
        inputs = [self.Input(utxos.pop(), sigdetail[0], sigdetail[1]) for sigdetail in sigdetails] + [self.Input((0, spend_tx), first_input_sigdetail[0], first_input_sigdetail[1])]
        tx = self.new_transaction(inputs)

        return spend_tx, tx

    def get_transactions_for_test(self, tx_collection, coinbases):
        SIGHASH_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_CHRONICLE
        SIGHASH_NON_MALLEABLE = SIGHASH_ALL | SIGHASH_FORKID

        # A dummy smart contract style txn
        CONTRACT_SCRIPT = [OP_DUP, self.BAD_KEY.get_pubkey(), OP_CHECKSIG, OP_NOTIF, self._UTXO_KEY.get_pubkey(), OP_CHECKSIG, OP_ENDIF, OP_DUP, OP_VERIFY]

        # Before Chronicle
        if tx_collection.label == "PRE_CHRONICLE":
            utxos, _ = self.utxos["PRE_CHRONICLE"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle grace period
        elif tx_collection.label == "CHRONICLE_PRE_GRACE":
            utxos, _ = self.utxos["CHRONICLE_PRE_GRACE"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Start of Chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_BEGIN":
            utxos, _ = self.utxos["CHRONICLE_GRACE_BEGIN"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'flexible-mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Illegal use of SIGHASH_CHRONICLE)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block before Chronicle activation
        elif tx_collection.label == "CHRONICLE_PRE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_PRE_ACTIVATION"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Chronicle activation height
        elif tx_collection.label == "CHRONICLE_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_ACTIVATION"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')

        # Block after Chronicle activation height
        elif tx_collection.label == "CHRONICLE_POST_ACTIVATION":
            utxos, _ = self.utxos["CHRONICLE_POST_ACTIVATION"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')

        # End of chronicle grace period
        elif tx_collection.label == "CHRONICLE_GRACE_END":
            utxos, _ = self.utxos["CHRONICLE_GRACE_END"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')

        # After Chronicle
        elif tx_collection.label == "POST_CHRONICLE":
            utxos, _ = self.utxos["POST_CHRONICLE"]

            # Multiple signatures, good signatures, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)
            # Multiple signatures, good signatures, mixed malleability
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ])
            tx_collection.add_tx(tx)

            # Multiple signatures, 1 bad signature, malleability disallowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability allowed on all sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on bad sig, allowed on good sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 bad signature, malleability disallowed on good sigs, allowed on bad sig
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self.BAD_KEY)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Multiple signatures, 1 null signature, malleability disallowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, malleability allowed on all other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')
            # Multiple signatures, 1 null signature, mixed malleability on other sigs
            tx = self.create_transaction(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ])
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack e',
                                 block_reject_reason=b'blk-bad-inputs')

            # Smart contract style script, bad signature non-null, malleability allowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx)
            # Smart contract style script, bad signature non-null, malleability disallowed on all sigs
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature non-null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability disallowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_NON_MALLEABLE, self._UTXO_KEY),
                (None, None),
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
            # Smart contract style script, bad signature null, malleability allowed on other sig
            spend_tx, tx = self.create_transactions(utxos, [
                (SIGHASH_MALLEABLE, self._UTXO_KEY),
                (None, None)
            ], locking_script=CONTRACT_SCRIPT)
            tx_collection.add_mempool_tx(spend_tx)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason=b'mandatory-script-verify-flag-failed (Script failed an OP_VERIFY operation)',
                                 block_reject_reason=b'blk-bad-inputs')
