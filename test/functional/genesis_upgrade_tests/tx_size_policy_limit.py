#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.cdefs import DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS, MAX_TX_SIZE_POLICY_BEFORE_GENESIS
from genesis_upgrade_tests.test_base import GenesisHeightTestsCaseBase, GenesisHeightBasedSimpleTestsCase
#from test_framework.hight_based_test_framework import SimpleTestDefinition
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.script import CScript, OP_FALSE, OP_RETURN, SignatureHashForkId, SignatureHash, SIGHASH_ALL, \
    SIGHASH_FORKID, OP_CHECKSIG

SIMPLE_OUTPUT_SCRIPT = CScript([OP_FALSE,OP_RETURN]) # Output script used by spend transactions. Could be anything that is standard, but OP_FALSE OP_RETURN is the easiest to create.
NEW_MAX_TX_SIZE_POLICY = DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS * 2


def make_key():
    key = CECKey()
    key.set_secretbytes(b"randombytes")
    return key


def new_transaction(utxokey, utxo, target_tx_size):
    ndx, tx_to_spend = utxo
    padding_size = target_tx_size
    while True:
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, ndx), b''))
        tx.vin[0].scriptSig = b''
        tx.vout.append(CTxOut(tx_to_spend.vout[0].nValue - 2 * target_tx_size, SIMPLE_OUTPUT_SCRIPT))
        tx.vout.append(CTxOut(1, CScript([OP_FALSE,OP_RETURN] + [bytes(1) * padding_size])))
        sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[0].nValue)
        sig = utxokey.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
        tx.vin[0].scriptSig = CScript([sig])
        tx.rehash()

        diff = target_tx_size - len(tx.serialize())
        if diff == 0:
            return tx
        padding_size += diff


class DefaultTxSizePolicyCaseTest(GenesisHeightTestsCaseBase):

    NAME = "Default max policy tx size"
    _UTXO_KEY = make_key()
    ARGS = GenesisHeightTestsCaseBase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-acceptnonstdtxn=0']

    def get_transactions_for_test(self, tx_collection, coinbases):
        if tx_collection.label == "PRE-GENESIS":
            utxos, data = self.utxos["PRE-GENESIS"]
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_POLICY_BEFORE_GENESIS)
            tx_collection.add_tx(tx)
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_POLICY_BEFORE_GENESIS + 1)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason = b'tx-size')

        if tx_collection.label == "MEMPOOL AT GENESIS":
            utxos, data = self.utxos["MEMPOOL AT GENESIS"]
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS)
            tx_collection.add_tx(tx)
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS + 1)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason = b'tx-size')


class TxSizePolicyCaseTest(GenesisHeightTestsCaseBase):

    NAME = "Increased max policy tx size"
    _UTXO_KEY = make_key()
    ARGS = GenesisHeightTestsCaseBase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-acceptnonstdtxn=0', '-maxtxsizepolicy=%d' % NEW_MAX_TX_SIZE_POLICY, '-datacarriersize=%d' % NEW_MAX_TX_SIZE_POLICY] +\
                                             ['-maxstdtxvalidationduration=5000', '-maxnonstdtxvalidationduration=5001']

    def get_transactions_for_test(self, tx_collection, coinbases):
        if tx_collection.label == "PRE-GENESIS":
            utxos, data = self.utxos["PRE-GENESIS"]
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_POLICY_BEFORE_GENESIS)
            tx_collection.add_tx(tx)
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_POLICY_BEFORE_GENESIS + 1)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason = b'tx-size')

        if tx_collection.label == "MEMPOOL AT GENESIS":
            utxos, data = self.utxos["PRE-GENESIS"]
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), NEW_MAX_TX_SIZE_POLICY)
            tx_collection.add_tx(tx)
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), NEW_MAX_TX_SIZE_POLICY + 1)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason = b'tx-size')
