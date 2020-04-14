#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Verify that consolidation transactions are free uless they do not meet below conditions.

Definition of a consolidation transaction.
====================================================
      A consolidation transaction is one that reduces the number of UTXO's by a margin that
      is more valuable for the nework than the current fee for that transaction. Additionally
      free consolidation transactions shall not be suitable for Denial of Service attack.
      The profitability of DoS attacks is reduced by increasing the consolidation factor

Minimal requirements for a consolidation transactions
====================================================

    - Every utxo used in the transaction needs to have been confirmed.
    - The number of transaction inputs must exceed the number of transaction outputs.
    - The cumulated number of input script sizes must exceed the number of cumulated
      output script sizes by a configurable factor (default is 20)

Remarks
====================================================

    - A consolidation transaction is not guaranteed to be mined in the next potential 
      block of a miner because it may still be rejected by a full mempool.
    - If this test fails, it does not prove something went wrong. It could mean
      that new conditions of other projects overrule the consolidation transaction
      rules

Implementation Details
====================================================
      This test requires tweaking script sizes relative to the consolidation factor.
      Instead of moving the script sizes we move the consolidation factor - just
      because it is way simpler.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error, satoshi_round, assert_equal
from test_framework.mininode import FromHex, CTransaction

class ConsolidationP2PKHTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.COIN = 100000000
        self.utxo_test_sats = 10000
        self.utxo_test_bsvs = satoshi_round(self.utxo_test_sats / self.COIN)
        self.blockmintxfee_sats = 500
        self.blockmintxfee_bsvs = satoshi_round(self.blockmintxfee_sats / self.COIN)
        self.mintxrelayfee_sats = 250
        self.mintxrelayfee_bsvs = satoshi_round(self.mintxrelayfee_sats / self.COIN)
        self.extra_args = [[
            "-whitelist=127.0.0.1",
            "-mintxrelayfee={}".format(self.mintxrelayfee_sats),
            "-minblocktxfee={}".format(self.blockmintxfee_sats),
            "-mintxconsolidationfactor=2",
            "-acceptnonstdtxn=1",
            "-relaypriority=1"
            "-txindex=1"
            ],[
            "-whitelist=127.0.0.1",
            "-mintxrelayfee={}".format(self.mintxrelayfee_sats),
            "-minblocktxfee={}".format(self.blockmintxfee_sats),
            "-mintxconsolidationfactor=10",
            "-acceptnonstdtxn=1",
            "-relaypriority=1"
            "-txindex=1"
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
            self.consolidation_factor = int(node.getnetworkinfo()['mintxconsolidationfactor'])
            print ("consolidation factor", self.consolidation_factor)

            node.generate(300)

            # test ratio between size of input script and size of output script
            tx_hex = self.create_and_sign_tx (node, 1, min_confirmations = 1)
            tx = FromHex(CTransaction(), tx_hex)
            tx.rehash()
            sin = len(tx.vin[0].scriptSig)
            sout = len(tx.vout[0].scriptPubKey)

            enough_inputs = (sout * self.consolidation_factor) // sin + 1
            enough_inputs = max(enough_inputs, 2)
            one_input = 1
            enough_confirmations = 1

            # FAILING CONDITION: input_count == output_count
            # output is always 1
            tx_hex = self.create_and_sign_tx (node, in_count = one_input , min_confirmations = enough_confirmations)
            assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
            print ("test 1: PASS")

            # FAILING CONDITION: input_sizes <= consolidation_factor * output_size
            # We assume scriptSig ~ 4 * scriptPubKey
            tx_hex = self.create_and_sign_tx (node, in_count = enough_inputs - 1 , min_confirmations = enough_confirmations)
            assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
            print ("test 2: PASS")

            # FAILING CONDITION: no confirmed inputs
            tx_hex = self.create_and_sign_tx (node, in_count = enough_inputs , min_confirmations = enough_confirmations - 1)
            assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
            print ("test 3: PASS")

            # ALL CONDITIONS MET: must succeed
            tx_hex = self.create_and_sign_tx (node, in_count = enough_inputs, min_confirmations = enough_confirmations)
            txid = node.sendrawtransaction(tx_hex)
            node.generate(1)
            tx = node.getrawtransaction(txid, 1)
            confirmations = tx.get('confirmations', 0)
            assert_equal (confirmations, 1)
            print ("test 4: PASS")

if __name__ == '__main__':
    ConsolidationP2PKHTest().main()
