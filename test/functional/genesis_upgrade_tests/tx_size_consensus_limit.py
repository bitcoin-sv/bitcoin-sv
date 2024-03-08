#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.cdefs import MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS
from genesis_upgrade_tests.test_base import GenesisHeightTestsCaseBase, GenesisHeightBasedSimpleTestsCase
from genesis_upgrade_tests.tx_size_policy_limit import new_transaction, make_key
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut
from test_framework.script import CScript, OP_FALSE, OP_RETURN, SignatureHashForkId, SignatureHash, SIGHASH_ALL, \
    SIGHASH_FORKID, OP_CHECKSIG

SIMPLE_OUTPUT_SCRIPT = CScript([OP_FALSE,OP_RETURN]) # Output script used by spend transactions. Could be anything that is standard, but OP_FALSE OP_RETURN is the easiest to create.


class TxSizeConsensusCaseTest(GenesisHeightTestsCaseBase):

    NAME = "Max consensus tx size"
    _UTXO_KEY = make_key()
    ARGS = GenesisHeightTestsCaseBase.ARGS + ['-banscore=1000000', '-whitelist=127.0.0.1', '-maxstdtxvalidationduration=5000', '-maxnonstdtxvalidationduration=5001']

    def get_transactions_for_test(self, tx_collection, coinbases):
        if tx_collection.label == "PRE-GENESIS":
            utxos, data = self.utxos["PRE-GENESIS"]
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS)
            tx_collection.add_tx(tx)
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS + 1)
            tx_collection.add_tx(tx,
                                 p2p_reject_reason = b'flexible-bad-txns-oversize',
                                 block_reject_reason=b'bad-txns-oversize')

        if tx_collection.label == "MEMPOOL AT GENESIS":
            utxos, data = self.utxos["MEMPOOL AT GENESIS"]
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS)
            tx_collection.add_tx(tx)
            tx = new_transaction(self._UTXO_KEY, utxos.pop(0), MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS + 1)
            tx_collection.add_tx(tx)
