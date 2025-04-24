#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.height_based_test_framework import HeightBasedTestsCase, HeightBasedSimpleTestsCase
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.script import CScript, OP_CHECKSIG, SignatureHash


class ChronicleHeightTestsCase(HeightBasedTestsCase):

    NAME = None

    GENESIS_ACTIVATION_HEIGHT = 150
    GRACE_PERIOD = 5
    GENESIS_ACTIVATED_HEIGHT = GENESIS_ACTIVATION_HEIGHT + GRACE_PERIOD
    CHRONICLE_ACTIVATION_HEIGHT = 175
    CHRONICLE_GRACE_START_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT - GRACE_PERIOD
    CHRONICLE_GRACE_END_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT + GRACE_PERIOD

    ARGS = [f"-genesisactivationheight={GENESIS_ACTIVATION_HEIGHT}",
            f"-chronicleactivationheight={CHRONICLE_ACTIVATION_HEIGHT}",
            f"-maxgenesisgracefulperiod={GRACE_PERIOD}",
            f"-maxchroniclegracefulperiod={GRACE_PERIOD}"]

    TESTING_HEIGHTS = [
        (GENESIS_ACTIVATION_HEIGHT - 3,    None,                        "PRE_GENESIS"),                # noqa Just build UTXOs
        (GENESIS_ACTIVATION_HEIGHT - 2,    "PRE_GENESIS",               None),                         # noqa Pre-Genesis
        (GENESIS_ACTIVATED_HEIGHT,         None,                        "PRE_CHRONICLE"),              # noqa No-test, just build UTXOs
        (GENESIS_ACTIVATED_HEIGHT + 1,     "PRE_CHRONICLE",             "CHRONICLE_PRE_GRACE"),        # noqa Post-Genesis but pre anything Chronicle
        (CHRONICLE_GRACE_START_HEIGHT - 1, "CHRONICLE_PRE_GRACE",       "CHRONICLE_GRACE_BEGIN"),      # noqa Block before Chronicle grace period starts
        (CHRONICLE_GRACE_START_HEIGHT,     "CHRONICLE_GRACE_BEGIN",     "CHRONICLE_PRE_ACTIVATION"),   # noqa First block in Chronicle grace period
        (CHRONICLE_ACTIVATION_HEIGHT - 1,  "CHRONICLE_PRE_ACTIVATION",  "CHRONICLE_ACTIVATION"),       # noqa Block before Chronicle activation
        (CHRONICLE_ACTIVATION_HEIGHT,      "CHRONICLE_ACTIVATION",      "CHRONICLE_POST_ACTIVATION"),  # noqa Chronicle activates
        (CHRONICLE_ACTIVATION_HEIGHT + 1,  "CHRONICLE_POST_ACTIVATION", "CHRONICLE_GRACE_END"),        # noqa Block after Chronicle activation
        (CHRONICLE_GRACE_END_HEIGHT - 2,   "CHRONICLE_GRACE_END",       "POST_CHRONICLE"),             # noqa Last block in Chronicle grace period
        (CHRONICLE_GRACE_END_HEIGHT,       "POST_CHRONICLE",            None),                         # noqa Chronicle is activated and we've left the grace period
    ]

    # Make a key for signing
    @staticmethod
    def make_key(secret=b"randombytes"):
        key = CECKey()
        key.set_secretbytes(secret)
        return key

    # Details about an input for spending
    class Input:
        utxo = None
        hash_type = None
        unlocking_script = None

        def __init__(self, utxo, hash_type, unlocking_script=[]):
            self.utxo = utxo
            self.hash_type = hash_type
            self.unlocking_script = unlocking_script

    # Spend UTXOs and create a txn with unlocking_script + the usual signature
    def new_transaction(self, key, inputs, sign_fn=None, output_locking_script=[]):
        tx = CTransaction()
        total_input = 0

        # Add inputs
        for inp in inputs:
            utxo = inp.utxo
            n, tx_to_spend = utxo

            tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, n), b''))
            total_input += tx_to_spend.vout[n].nValue

        # 1 output
        tx.vout.append(CTxOut(total_input - 100, CScript(output_locking_script + [key.get_pubkey(), OP_CHECKSIG])))

        # Standard txn signing function
        def sign_txn_input(sighash, hash_type):
            return key.sign(sighash) + bytes(bytearray([hash_type]))

        # Sign
        for index, (inp, tx_input) in enumerate(zip(inputs, tx.vin)):
            utxo = inp.utxo
            hash_type = inp.hash_type
            unlocking_script = inp.unlocking_script
            n, tx_to_spend = utxo

            sighash = SignatureHash(tx_to_spend.vout[n].scriptPubKey, tx, index, hash_type, tx_to_spend.vout[n].nValue)
            sig = sign_txn_input(sighash, hash_type) if sign_fn is None else sign_fn(sighash, hash_type)
            tx_input.scriptSig = CScript(unlocking_script + [sig])

        tx.rehash()
        return tx


class ChronicleHeightBasedSimpleTestsCase(HeightBasedSimpleTestsCase):

    GENESIS_ACTIVATION_HEIGHT = 150
    GRACE_PERIOD = 5
    GENESIS_ACTIVATED_HEIGHT = GENESIS_ACTIVATION_HEIGHT + GRACE_PERIOD
    CHRONICLE_ACTIVATION_HEIGHT = 175
    CHRONICLE_GRACE_START_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT - GRACE_PERIOD
    CHRONICLE_GRACE_END_HEIGHT = CHRONICLE_ACTIVATION_HEIGHT + GRACE_PERIOD

    ARGS = [f"-genesisactivationheight={GENESIS_ACTIVATION_HEIGHT}",
            f"-chronicleactivationheight={CHRONICLE_ACTIVATION_HEIGHT}",
            f"-maxgenesisgracefulperiod={GRACE_PERIOD}",
            f"-maxchroniclegracefulperiod={GRACE_PERIOD}"]

    TESTING_HEIGHTS = [
        (GENESIS_ACTIVATED_HEIGHT + 1,     "PRE_CHRONICLE",             None),  # noqa Post-Genesis but pre anything Chronicle
        (CHRONICLE_GRACE_START_HEIGHT - 1, "CHRONICLE_PRE_GRACE",       None),  # noqa Block before Chronicle grace period starts
        (CHRONICLE_GRACE_START_HEIGHT,     "CHRONICLE_GRACE_BEGIN",     None),  # noqa First block in Chronicle grace period
        (CHRONICLE_ACTIVATION_HEIGHT - 1,  "CHRONICLE_PRE_ACTIVATION",  None),  # noqa Block before Chronicle activation
        (CHRONICLE_ACTIVATION_HEIGHT,      "CHRONICLE_ACTIVATION",      None),  # noqa Chronicle activates
        (CHRONICLE_ACTIVATION_HEIGHT + 1,  "CHRONICLE_POST_ACTIVATION", None),  # noqa Block after Chronicle activation
        (CHRONICLE_GRACE_END_HEIGHT - 2,   "CHRONICLE_GRACE_END",       None),  # noqa Last block in Chronicle grace period
        (CHRONICLE_GRACE_END_HEIGHT,       "POST_CHRONICLE",            None),  # noqa Chronicle is activated and we've left the grace period
    ]
