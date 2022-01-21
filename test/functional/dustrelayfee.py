#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Verify that dust and non-dust tx outputs are rejected and accepted as expected,
when the dustrelayfee and dustlimitfactor setting changes between releases or is configured manually.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error, sync_blocks
from test_framework.mininode import COIN
from decimal import Decimal

class DustRelayFeeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [[],[],[]]

    # Parameterized test for node launched with different fee settings, which checks:
    # - wallet does not allow sending dust amount
    # - wallet accepts sending amount which meets dust threshold
    # - node does not accept tx output with dust
    # - node accepts tx output which meets dust threshold
    def test_node_with_fees(self, nodeid, dustlimitfactor, dustrelayfee_sats, minrelayfee_sats):
        node = self.nodes[nodeid]
        dustrelayfee = Decimal(dustrelayfee_sats)/COIN
        minrelayfee = Decimal(minrelayfee_sats)/COIN
        # Check that nodes agree before shutting one off
        sync_blocks(self.nodes[nodeid:], timeout=20)

        self.restart_node(nodeid, extra_args=[
            "-dustrelayfee="+str(dustrelayfee),
            "-dustlimitfactor="+str(dustlimitfactor),
            "-minrelaytxfee="+str(minrelayfee),
            "-acceptnonstdtxn=0"])

        # Calculate dust threshold as defined in transaction.h, GetDustThreshold()
        # For e.g. if dustrelayfee is 300% then we have following mapping
        # dustrelayfee 1000 --> threshold 546
        # dustrelayfee  250 --> threshold 135
        dust_threshold_sats = (dustlimitfactor * int(182 * dustrelayfee_sats / 1000)) / 100
        amount_is_not_dust = Decimal(dust_threshold_sats)/COIN
        amount_is_dust = Decimal(dust_threshold_sats - 1)/COIN
        if dustlimitfactor == 0:
            amount_is_not_dust = 1

        # Test: Wallet will allow sending amount which meets dust threshold
        addr = node.getnewaddress()
        addr2 = node.getnewaddress()
        txid = node.sendtoaddress(addr, amount_is_not_dust)
        assert(txid in node.getrawmempool())

        # Test: Wallet will not allow sending dust amount
        if dustlimitfactor > 0:
            assert_raises_rpc_error(-4, "Transaction amount too small", node.sendtoaddress, addr, amount_is_dust)

        # Get confirmed utxo to spend
        utxo_list = node.listunspent(1)
        utxo = utxo_list[0]
        fee_amount = Decimal('0.00010000')

        # Test: create tx with dust output that will be rejected
        inputs = []
        outputs = {}
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        if dustlimitfactor > 0:
            outputs[addr] = amount_is_dust
            outputs[addr2] = utxo["amount"] - amount_is_dust - fee_amount
            raw_tx = node.createrawtransaction(inputs, outputs)
            tx_hex = node.signrawtransaction(raw_tx)["hex"]
            txid = node.decoderawtransaction(tx_hex)["txid"]
            assert_raises_rpc_error(-26, "64: dust", node.sendrawtransaction, tx_hex)
            assert(txid not in node.getrawmempool())

        # Test: update output value so it meets dust threshold and tx is accepted
        outputs[addr] = amount_is_not_dust
        outputs[addr2] = utxo["amount"] - amount_is_not_dust - fee_amount
        raw_tx = node.createrawtransaction(inputs, outputs)
        tx_hex = node.signrawtransaction(raw_tx)["hex"]
        txid = node.sendrawtransaction(tx_hex)
        assert(txid in node.getrawmempool())

    def run_test(self):
        self.nodes[0].generate(120)

        # 1. Default settings of BSV node before Genesis release
        self.test_node_with_fees(0, 300, dustrelayfee_sats=1000, minrelayfee_sats=1000)

        # 2. Default settings of BSV node running Genesis release
        self.test_node_with_fees(0, 300, dustrelayfee_sats=1000, minrelayfee_sats=250)
        
        # 3. BSV node where dustrelayfee is lowered to match minrelayfee
        self.test_node_with_fees(0, 300, dustrelayfee_sats=250, minrelayfee_sats=250)

        self.nodes[1].generate(120)

        # 4. Default settings of BSV node before Genesis release (but dust limit 200%)
        self.test_node_with_fees(1, 200, dustrelayfee_sats=1000, minrelayfee_sats=1000)

        # 5. Default settings of BSV node running Genesis release (but dust limit 200%)
        self.test_node_with_fees(1, 200, dustrelayfee_sats=1000, minrelayfee_sats=250)
        
        # 6. BSV node where dustrelayfee is lowered to match minrelayfee (but dust limit 200%)
        self.test_node_with_fees(1, 200, dustrelayfee_sats=250, minrelayfee_sats=250)

        self.nodes[2].generate(120)

        # 7. Default settings of BSV node before Genesis release (but dust limit 0%)
        self.test_node_with_fees(2, 0, dustrelayfee_sats=1000, minrelayfee_sats=1000)

        # 8. Default settings of BSV node running Genesis release (but dust limit 0%)
        self.test_node_with_fees(2, 0, dustrelayfee_sats=1000, minrelayfee_sats=250)
        
        # 9. BSV node where dustrelayfee is lowered to match minrelayfee (but dust limit 0%)
        self.test_node_with_fees(2, 0, dustrelayfee_sats=250, minrelayfee_sats=250)

if __name__ == '__main__':
    DustRelayFeeTest().main()
