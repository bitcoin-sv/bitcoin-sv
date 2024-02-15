#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test for calling (get/decode)rawtransaction and check if the response is returned properly as JSON
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import COIN


class DecodeRawTransactionsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self, split=False):
        self.setup_nodes()

    def run_test(self):
        min_relay_tx_fee = self.nodes[0].getnetworkinfo()['relayfee']
        # This test is not meant to test fee estimation and we'd like
        # to be sure all txs are sent at a consistent desired feerate
        for node in self.nodes:
            node.settxfee(min_relay_tx_fee)

        self.nodes[0].generate(110)
        self.sync_all()

        inputs1 = []
        outputs1 = {self.nodes[0].getnewaddress(): 1.0}
        rawtx1 = self.nodes[0].createrawtransaction(inputs1, outputs1)

        inputs2 = []
        outputs2 = {self.nodes[0].getnewaddress(): 2.2}
        rawtx2 = self.nodes[0].createrawtransaction(inputs2, outputs2)

        decodedrawtx1 = self.nodes[0].decoderawtransaction(rawtx1)
        assert_equal(decodedrawtx1["hex"], rawtx1)
        decodedrawtx2 = self.nodes[0].decoderawtransaction(rawtx2)
        assert_equal(decodedrawtx2["hex"], rawtx2)

        batch = self.nodes[0].batch([self.nodes[0].decoderawtransaction.get_request(rawtx1), self.nodes[0].decoderawtransaction.get_request(rawtx2)])
        assert_equal(batch[0]["result"]["hex"], rawtx1)
        assert_equal(batch[1]["result"]["hex"], rawtx2)

        rawtx1_funded = self.nodes[0].fundrawtransaction(rawtx1)
        rawtx1_signed = self.nodes[0].signrawtransaction(rawtx1_funded["hex"])
        rawtx1_sent = self.nodes[0].sendrawtransaction(rawtx1_signed["hex"])

        rawtx2_funded = self.nodes[0].fundrawtransaction(rawtx2)
        rawtx2_signed = self.nodes[0].signrawtransaction(rawtx2_funded["hex"])
        rawtx2_sent = self.nodes[0].sendrawtransaction(rawtx2_signed["hex"])

        batch = self.nodes[0].batch([self.nodes[0].getrawtransaction.get_request(rawtx1_sent, 1), self.nodes[0].getrawtransaction.get_request(rawtx2_sent, 1)])
        assert_equal(batch[0]["result"]["hex"], rawtx1_signed["hex"])
        assert_equal(batch[1]["result"]["hex"], rawtx2_signed["hex"])

        batch = self.nodes[0].batch([self.nodes[0].getrawtransaction.get_request(rawtx1_sent), self.nodes[0].getrawtransaction.get_request(rawtx2_sent)])
        assert_equal(batch[0]["result"], rawtx1_signed["hex"])
        assert_equal(batch[1]["result"], rawtx2_signed["hex"])

        getrawtx1 = self.nodes[0].getrawtransaction(rawtx1_sent, 1)
        assert_equal(getrawtx1["hex"], rawtx1_signed["hex"])
        getrawtx2 = self.nodes[0].getrawtransaction(rawtx2_sent, 1)
        assert_equal(getrawtx2["hex"], rawtx2_signed["hex"])

        getrawtx1 = self.nodes[0].getrawtransaction(rawtx1_sent)
        assert_equal(getrawtx1, rawtx1_signed["hex"])
        getrawtx2 = self.nodes[0].getrawtransaction(rawtx2_sent)
        assert_equal(getrawtx2, rawtx2_signed["hex"])


if __name__ == '__main__':
    DecodeRawTransactionsTest().main()
