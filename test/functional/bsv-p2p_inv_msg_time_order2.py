#!/usr/bin/env python3
# Copyright (C) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
'''
Test checks if transactions included in inv message are in the same order
in which they were sent to node (send_message()) and then validated. -broadcastdelay=10000 is used to prevent
the node from broadcasting of each transaction separately it processed. Only one inv is sent for all transactions
in mempool after 10s instead.
'''

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import *
from test_framework.util import *
from test_framework.script import *
from time import sleep


def invsOrderedbyTime(invListExpected, txinvs):
    # invListExpected contains transactions that were sent, txinvs contains transactions that were received in INV.
    # It's not guaranteed that all transactions were accepted to mempool and therefore sent back in INV message.
    # Function checks if transactions in txinvs are in correct order.
    for i in range(len(txinvs)):
        logger.debug("Received tx inv %s at %d, sent at %d", txinvs[i], i, invListExpected.index(txinvs[i]))
        logger.debug("Sent tx at %d is %s", i, invListExpected[i])
        if invListExpected.index(txinvs[i]) < i:
            return False
    return True


class TxnInvOrder(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.num_txns = 3

    def create_tx_simple(self, fee, txs):
        self.nodes[0].settxfee(fee)
        ftx = CTransaction()
        ftx.vout.append(CTxOut(1000))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx), {'changePosition': len(ftx.vout)})['hex']
        ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()
        txs.append(ftx)
        return txs

    def test_inventory_order(self):
        self.stop_node(0)
        with self.run_node_with_connections("", 0, ['-broadcastdelay=10000'], 2) as p2pc:
            connection = p2pc[0]
            connection2 = p2pc[1]

            # protected by mininode_lock
            txinvs = []

            # Append txinv
            def on_inv(conn, message):
                for im in message.inv:
                    if im.type == 1:
                        txinvs.append(hashToHex(im.hash))

            connection2.cb.on_inv = on_inv

            fee0 = Decimal("0.00200000")
            fees = [fee0, fee0 * 2, fee0 * 3, fee0 * 2]
            ids = []
            txs = []
            # create all the transactions
            for fee in fees:
                for i in range(self.num_txns):
                    txs = self.create_tx_simple(fee, txs)
                    ids.append(txs[-1].hash)

            for tx in txs:
                connection.send_message(msg_tx(tx))
                # wait for transaction to be processed
                connection.cb.sync_with_ping()

            # have to wait to receive one inv message with all transactions
            wait_until(lambda: len(txinvs) > 0, timeout=60, lock=mininode_lock)

            with mininode_lock:
                assert (invsOrderedbyTime(ids, txinvs))

    def run_test(self):
        # Make some coins to spend
        self.log.info("Mining blocks...")
        self.nodes[0].generate(100)
        sync_blocks(self.nodes)
        self.test_inventory_order()


if __name__ == '__main__':
    TxnInvOrder().main()
