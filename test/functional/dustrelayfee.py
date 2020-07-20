#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Verify that dust and non-dust tx outputs are rejected and accepted as expected,
when the dustrelayfee setting changes between releases or is configured manually.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error
from test_framework.mininode import COIN
from decimal import Decimal

class DustRelayFeeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[]]

    # Parameterized test for node launched with different fee settings, which checks:
    # - wallet does not allow sending dust amount
    # - wallet accepts sending amount which meets dust threshold
    # - node does not accept tx output with dust
    # - node accepts tx output which meets dust threshold
    def test_node_with_fees(self, dustrelayfee_sats, minrelayfee_sats):
        dustrelayfee = Decimal(dustrelayfee_sats)/COIN
        minrelayfee = Decimal(minrelayfee_sats)/COIN
        self.restart_node(0, extra_args=["-dustrelayfee="+str(dustrelayfee), "-minrelaytxfee="+str(minrelayfee), "-acceptnonstdtxn=0"])

        # Calculate dust threshold as defined in transaction.h, GetDustThreshold()
        # dustrelayfee 1000 --> threshold 546
        # dustrelayfee  250 --> threshold 135
        dust_threshold_sats = 3 * int(182 * dustrelayfee_sats / 1000)
        amount_is_not_dust = Decimal(dust_threshold_sats)/COIN
        amount_is_dust = Decimal(dust_threshold_sats - 1)/COIN
        if dustrelayfee_sats==1000:
            assert(dust_threshold_sats==546)
        elif dustrelayfee_sats==250:
            assert(dust_threshold_sats==135)

        # Test: Wallet will not allow sending dust amount
        addr = self.nodes[0].getnewaddress()
        assert_raises_rpc_error(-4, "Transaction amount too small", self.nodes[0].sendtoaddress, addr, amount_is_dust)

        # Test: Wallet will allow sending amount which meets dust threshold
        txid = self.nodes[0].sendtoaddress(addr, amount_is_not_dust)
        assert(txid in self.nodes[0].getrawmempool())

        # Get confirmed utxo to spend
        utxo_list = self.nodes[0].listunspent(1)
        utxo = utxo_list[0]
        fee_amount = Decimal('0.00010000')

        # Test: create tx with dust output that will be rejected
        inputs = []
        outputs = {}
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        outputs[addr] = amount_is_dust
        outputs[self.nodes[0].getnewaddress()] = utxo["amount"] - amount_is_dust - fee_amount
        raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx_hex = self.nodes[0].signrawtransaction(raw_tx)["hex"]
        txid = self.nodes[0].decoderawtransaction(tx_hex)["txid"]
        assert_raises_rpc_error(-26, "64: dust", self.nodes[0].sendrawtransaction, tx_hex)
        assert(txid not in self.nodes[0].getrawmempool())

        # Test: update output value so it meets dust threshold and tx is accepted
        outputs[addr] = amount_is_not_dust
        raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx_hex = self.nodes[0].signrawtransaction(raw_tx)["hex"]
        txid = self.nodes[0].sendrawtransaction(tx_hex)
        assert(txid in self.nodes[0].getrawmempool())

    def run_test(self):
        self.nodes[0].generate(120)

        # 1. Default settings of BSV node before Genesis release
        self.test_node_with_fees(dustrelayfee_sats=1000, minrelayfee_sats=1000)

        # 2. Default settings of BSV node running Genesis release
        self.test_node_with_fees(dustrelayfee_sats=1000, minrelayfee_sats=250)
        
        # 3. BSV node where dustrelayfee is lowered to match minrelayfee
        self.test_node_with_fees(dustrelayfee_sats=250, minrelayfee_sats=250)

if __name__ == '__main__':
    DustRelayFeeTest().main()
