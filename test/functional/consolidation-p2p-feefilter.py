#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
#

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time

'''
Test if consolidation transactions pass the feefilter
'''

def getInputScriptPubKey(node, input, index):
    txid = hashToHex(input.prevout.hash)
    raw = node.getrawtransaction(txid)
    tx = FromHex(CTransaction(), raw)
    tx.rehash()
    return tx.vout[index].scriptPubKey

class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.txinvs = []

class FeeFilterTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.COIN = 100000000
        self.utxo_test_sats = 10000
        self.utxo_test_bsvs = satoshi_round(self.utxo_test_sats / self.COIN)
        self.blockmintxfee_sats = 500
        self.mintxrelayfee_sats = 250
        self.extra_args = [[
            "-mintxrelayfee={}".format(self.mintxrelayfee_sats),
            "-minblocktxfee={}".format(self.blockmintxfee_sats),
            "-minconsolidationfactor=10",
            ],[
            "-whitelistforcerelay=1"
            "-mintxrelayfee={}".format(self.mintxrelayfee_sats),
            "-minblocktxfee={}".format(self.blockmintxfee_sats),
            "-minconsolidationfactor=10",
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
            sync_blocks(self.nodes)

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



    def run_test(self):
        node1 = self.nodes[1]

        # Get out of IBD
        node1.generate(300)
        sync_blocks(self.nodes)

        self.consolidation_factor = int(node1.getnetworkinfo()['minconsolidationfactor'])
        self.minConfirmations = int(node1.getnetworkinfo()['minconsolidationinputmaturity'])
        # test ratio between size of input script and size of output script
        tx_hex = self.create_and_sign_tx(node1, 1, min_confirmations=1)
        tx = FromHex(CTransaction(), tx_hex)
        tx.rehash()
        sin = len(getInputScriptPubKey(node1, tx.vin[0], 0))
        sout = len(tx.vout[0].scriptPubKey)

        enough_inputs = sout * self.consolidation_factor // sin
        enough_inputs = max(enough_inputs, 2)
        enough_confirmations = self.minConfirmations
        # END setup consolidation transactions
        sync_blocks(self.nodes)

        tx_hex = self.create_and_sign_tx(node1, in_count=enough_inputs, min_confirmations=enough_confirmations)
        sync_blocks(self.nodes)
        sync_mempools(self.nodes)

        txid1 = node1.sendrawtransaction(tx_hex)
        txid2 = node1.sendtoaddress(node1.getnewaddress(), 1)

        wait_until(lambda: txid1 in self.nodes[0].getrawmempool(), timeout=5)
        wait_until(lambda: txid2 in self.nodes[0].getrawmempool(), timeout=5)

if __name__ == '__main__':
    FeeFilterTest().main()
