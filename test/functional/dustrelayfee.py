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


def create_zero_fee_tx_funded_from(node, tx_id, small_value):
    addr = node.getnewaddress()
    addr2 = node.getnewaddress()
    tx = node.getrawtransaction(tx_id, 1)
    n = -1

    for vout in tx['vout']:
        val = vout['value']
        if val > 100 * small_value:
            n = vout['n']
            break

    assert(n > -1)

    inputs = [{'txid': tx_id, 'vout': n}]
    small_value = float(small_value)
    val = float(val)
    outputs = {addr: val - small_value, addr2: small_value}
    rawtx = node.createrawtransaction(inputs, outputs)
    signed = node.signrawtransaction(rawtx)
    return signed["hex"]


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
    def test_node_with_fees(self, nodeid):
        node = self.nodes[nodeid]

        # Check that nodes agree before shutting one off
        sync_blocks(self.nodes[nodeid:], timeout=20)

        self.restart_node(nodeid, extra_args=[
            "-acceptnonstdtxn=0"])

        dust_threshold_sats = 1
        amount_is_not_dust = Decimal(dust_threshold_sats)/COIN
        amount_is_dust = Decimal(0.0)

        # Test: Wallet will allow sending amount which meets dust threshold
        addr = node.getnewaddress()
        addr2 = node.getnewaddress()
        txid = node.sendtoaddress(addr, amount_is_not_dust)
        assert(txid in node.getrawmempool())

        tx_id = node.sendtoaddress(addr, 1.0)

        # Test: Wallet will not allow sending dust amount

        tx_hex = create_zero_fee_tx_funded_from(node, tx_id, 0)
        assert_raises_rpc_error(-26, "64: dust", node.sendrawtransaction, tx_hex)

        # Get confirmed utxo to spend
        utxo_list = node.listunspent(1)
        utxo = utxo_list[0]
        fee_amount = Decimal('0.00010000')

        # Test: create tx with dust output that will be rejected
        inputs = []
        outputs = {}
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})

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
        self.test_node_with_fees(0)


if __name__ == '__main__':
    DustRelayFeeTest().main()
