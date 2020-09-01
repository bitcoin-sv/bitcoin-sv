#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Verify that consolidation transactions pass if they do not pay any fee and also
test if they are rejected if not fulfilling all criteria for a consolidation transaction

This test tweaks relative script sizes by manipulating the consolidation factor. This
allows applying this test to standard p2pkh transactions.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error, satoshi_round, assert_equal, hashToHex
from test_framework.mininode import FromHex, CTransaction, COIN
from decimal import Decimal

def getInputScriptPubKey(node, input, index):
    txid = hashToHex(input.prevout.hash)
    raw = node.getrawtransaction(txid)
    tx = FromHex(CTransaction(), raw)
    tx.rehash()
    return tx.vout[index].scriptPubKey

class ConsolidationP2PKHTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.utxo_test_sats = 10000
        self.utxo_test_bsvs = satoshi_round(self.utxo_test_sats / COIN)
        self.blockmintxfee_sats = 500
        self.minrelaytxfee_sats = 250
        self.extra_args = [[
            "-whitelist=127.0.0.1",
            "-minrelaytxfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
            "-blockmintxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
            "-minconsolidationfactor=2",
            "-acceptnonstdtxn=1",
            ],[
            "-whitelist=127.0.0.1",
            "-minrelaytxfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
            "-blockmintxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
            #"-minconsolidationfactor=10", # test default consolidation factor
            "-acceptnonstdtxn=1",
        ]]

    def create_utxos_value10000(self, node, utxo_count, min_confirmations):

        utxos = []
        addr = node.getnewaddress()
        for i in range (utxo_count):
            txid = node.sendtoaddress(addr, self.utxo_test_bsvs)
            tx = FromHex(CTransaction(), node.getrawtransaction(txid))
            tx.rehash()
            utxos.append(tx)

        if min_confirmations > 0:
            node.generate(min_confirmations)

        return utxos

    def create_and_sign_tx(self, node, in_count, min_confirmations):

        utxos = self.create_utxos_value10000 (node, in_count, min_confirmations)
        inputs = []
        sum_values_bsvs = 0
        for u in utxos:
            for i in range(len(u.vout)):
                if u.vout[i].nValue == self.utxo_test_sats:
                    sum_values_bsvs = sum_values_bsvs + self.utxo_test_bsvs
                    inputs.append({"txid": u.hash, "vout": i})

        assert (len(utxos) == in_count)
        assert (sum_values_bsvs == in_count * self.utxo_test_bsvs)

        addr = node.getnewaddress()
        outputs = {}
        outputs[addr] = sum_values_bsvs

        rawtx = node.createrawtransaction(inputs, outputs)
        return node.signrawtransaction(rawtx)['hex']

    # send 2 p2pkh transactions that must fail
    # and one that must succeed
    def run_test(self):
        for node in self.nodes:
            self.consolidation_factor = int(node.getnetworkinfo()['minconsolidationfactor'])
            self.minConfirmations = int(node.getnetworkinfo()['minconsolidationinputmaturity'])
            self.log.info ("consolidation factor: {}".format(self.consolidation_factor))
            self.log.info ("minimum input confirmations: {}".format(self.minConfirmations))

            node.generate(300)

            # test ratio between size of input script and size of output script
            tx_hex = self.create_and_sign_tx (node, 1, min_confirmations = 1)
            tx = FromHex(CTransaction(), tx_hex)
            tx.rehash()
            sin = len(getInputScriptPubKey(node, tx.vin[0], 0))
            sout = len(tx.vout[0].scriptPubKey)

            enough_inputs = sout * self.consolidation_factor // sin
            enough_inputs = max(enough_inputs, 2)
            enough_confirmations = self.minConfirmations

            # FAILING CONDITION: input_sizes <= consolidation_factor * output_size
            # We assume scriptSig ~ 4 * scriptPubKey
            tx_hex = self.create_and_sign_tx (node, in_count = enough_inputs - 1 , min_confirmations = enough_confirmations)
            assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
            self.log.info ("test 1: PASS")

            # FAILING CONDITION: not enough input confirmations
            tx_hex = self.create_and_sign_tx (node, in_count = enough_inputs , min_confirmations = enough_confirmations - 1)
            assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
            self.log.info ("test 2: PASS")

            # ALL CONDITIONS MET: must succeed
            tx_hex = self.create_and_sign_tx (node, in_count = enough_inputs, min_confirmations = enough_confirmations)
            txid = node.sendrawtransaction(tx_hex)
            node.generate(1)
            tx = node.getrawtransaction(txid, 1)
            confirmations = tx.get('confirmations', 0)
            assert_equal (confirmations, 1)
            self.log.info ("test 3: PASS")

if __name__ == '__main__':
    ConsolidationP2PKHTest().main()
