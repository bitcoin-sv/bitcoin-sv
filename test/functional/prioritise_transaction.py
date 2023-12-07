#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

#
# Test PrioritiseTransaction code
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
# FIXME: review how this test needs to be adapted w.r.t _LEGACY_MAX_BLOCK_SIZE
from test_framework.mininode import COIN
from test_framework.cdefs import LEGACY_MAX_BLOCK_SIZE, ONE_KILOBYTE
import decimal


class PrioritiseTransactionTest(BitcoinTestFramework):

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        self.start_node(0, ["-minminingtxfee=0.00003"])

        node = self.nodes[0]
        self.txouts = gen_return_txouts()

        utxo = create_confirmed_utxos(Decimal(1000)/Decimal(COIN), node, 1, age=101)[0]

        relayfeerate = self.nodes[0].getnetworkinfo()['relayfee']

        ESTIMATED_TX_SIZE_IN_KB = Decimal(385)/Decimal(ONE_KILOBYTE)

        relayfee = ESTIMATED_TX_SIZE_IN_KB * relayfeerate

        low_paying_txs = []

        # let's create a chain of three low paying transactions and sent them to the node, they end up in the mempool
        for _ in range(3):
            inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
            outputs = {node.getnewaddress(): satoshi_round(utxo['amount'] - relayfee)}
            rawtx = node.createrawtransaction(inputs, outputs)
            signed_tx = node.signrawtransaction(rawtx)["hex"]
            x = len(signed_tx)
            txid = node.sendrawtransaction(signed_tx)
            low_paying_txs.append(txid)
            assert txid in node.getrawmempool()
            tx_info = node.getrawtransaction(txid, True)
            utxo = {"txid":txid, "vout":0, "amount":tx_info["vout"][0]["value"]}

        node.generate(1)

        # after mining a new block they are still in the mempool
        for txid in low_paying_txs:
            assert txid in node.getrawmempool()

        # let's raise modified fee for the middle transaction, this will ensure that this transaction will be mined
        node.prioritisetransaction(low_paying_txs[1], 0, COIN)

        # mine a new block
        node.generate(1)

        rawmempool = node.getrawmempool()

        # prioritized tx is mined together with its parent, but it's child is not
        assert low_paying_txs[0] not in rawmempool
        assert low_paying_txs[1] not in rawmempool
        assert low_paying_txs[2] in rawmempool

        self.stop_node(0)


if __name__ == '__main__':
    PrioritiseTransactionTest().main()
