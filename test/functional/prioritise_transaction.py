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

    def run_legacy_test(self):
        self.start_node(0, ["-printpriority=1", "-blockmaxsize=32000000", "-blockassembler=legacy"], )

        self.txouts = gen_return_txouts()
        self.relayfee = self.nodes[0].getnetworkinfo()['relayfee']

        # Create enough UTXOs to fill the 3 blocks we need for this test
        utxo_count = 1440
        utxos = create_confirmed_utxos(
            self.relayfee, self.nodes[0], utxo_count)
        # our transactions are smaller than 100kb
        base_fee = self.relayfee * 100
        txids = []

        # Create 3 batches of transactions at 3 different fee rate levels
        range_size = utxo_count // 3
        for i in range(3):
            txids.append([])
            start_range = i * range_size
            end_range = start_range + range_size
            txids[i] = create_lots_of_big_transactions(self.nodes[0], self.txouts, utxos[
                                                       start_range:end_range], end_range - start_range, (i + 1) * base_fee)

        # Make sure that the size of each group of transactions exceeds
        # LEGACY_MAX_BLOCK_SIZE -- otherwise the test needs to be revised to create
        # more transactions.
        mempool = self.nodes[0].getrawmempool(True)
        sizes = [0, 0, 0]
        for i in range(3):
            for j in txids[i]:
                assert(j in mempool)
                sizes[i] += mempool[j]['size']
            # Fail => raise utxo_count
            assert(sizes[i] > LEGACY_MAX_BLOCK_SIZE)

        # add a fee delta to something in the cheapest bucket and make sure it gets mined
        # also check that a different entry in the cheapest bucket is NOT mined (lower
        # the priority to ensure its not mined due to priority)
        self.nodes[0].prioritisetransaction(
            txids[0][0], 0, int(3 * base_fee * COIN))
        self.nodes[0].prioritisetransaction(txids[0][1], -1e15, 0)

        self.nodes[0].generate(1)

        mempool = self.nodes[0].getrawmempool()
        self.log.info("Assert that prioritised transaction was mined")
        assert(txids[0][0] not in mempool)
        assert(txids[0][1] in mempool)

        high_fee_tx = None
        for x in txids[2]:
            if x not in mempool:
                high_fee_tx = x

        # Something high-fee should have been mined!
        assert(high_fee_tx != None)

        # Add a prioritisation before a tx is in the mempool (de-prioritising a
        # high-fee transaction so that it's now low fee).
        self.nodes[0].prioritisetransaction(
            high_fee_tx, -1e15, -int(2 * base_fee * COIN))

        # Add everything back to mempool
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())

        # Check to make sure our high fee rate tx is back in the mempool
        mempool = self.nodes[0].getrawmempool()
        assert(high_fee_tx in mempool)

        # Now verify the modified-high feerate transaction isn't mined before
        # the other high fee transactions. Keep mining until our mempool has
        # decreased by all the high fee size that we calculated above.
        while (self.nodes[0].getmempoolinfo()['bytes'] > sizes[0] + sizes[1]):
            self.nodes[0].generate(1)

        # High fee transaction should not have been mined, but other high fee rate
        # transactions should have been.
        mempool = self.nodes[0].getrawmempool()
        self.log.info(
            "Assert that de-prioritised transaction is still in mempool")
        assert(high_fee_tx in mempool)
        for x in txids[2]:
            if (x != high_fee_tx):
                assert(x not in mempool)

        # Create a free, low priority transaction.  Should be rejected.
        utxo_list = self.nodes[0].listunspent()
        assert(len(utxo_list) > 0)
        utxo = utxo_list[0]

        inputs = []
        outputs = {}
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        outputs[self.nodes[0].getnewaddress()] = utxo["amount"] - self.relayfee
        raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx_hex = self.nodes[0].signrawtransaction(raw_tx)["hex"]
        txid = self.nodes[0].sendrawtransaction(tx_hex)

        # A tx that spends an in-mempool tx has 0 priority, so we can use it to
        # test the effect of using prioritise transaction for mempool
        # acceptance
        inputs = []
        inputs.append({"txid": txid, "vout": 0})
        outputs = {}
        outputs[self.nodes[0].getnewaddress()] = utxo["amount"] - self.relayfee
        raw_tx2 = self.nodes[0].createrawtransaction(inputs, outputs)
        tx2_hex = self.nodes[0].signrawtransaction(raw_tx2)["hex"]
        tx2_id = self.nodes[0].decoderawtransaction(tx2_hex)["txid"]

        # This will raise an exception due to min relay fee not being met
        assert_raises_rpc_error(-26, "66: insufficient priority",
                                self.nodes[0].sendrawtransaction, tx2_hex)
        assert(tx2_id not in self.nodes[0].getrawmempool())

        # This is a less than 1000-byte transaction, so just set the fee
        # to be the minimum for a 1000 byte transaction and check that it is
        # accepted.
        self.nodes[0].prioritisetransaction(
            tx2_id, 0, int(self.relayfee * COIN))

        self.log.info(
            "Assert that prioritised free transaction is accepted to mempool")
        assert_equal(self.nodes[0].sendrawtransaction(tx2_hex), tx2_id)
        assert(tx2_id in self.nodes[0].getrawmempool())

        self.stop_node(0)

    def run_journaling_test(self):
        self.start_node(0, ["-blockmintxfee=0.00003"] )

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


    def run_test(self):
        self.run_legacy_test()
        self.run_journaling_test()

if __name__ == '__main__':
    PrioritiseTransactionTest().main()
