#!/usr/bin/env python3
# Copyright (C) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import *
from test_framework.util import *
from test_framework.script import *
import time
from time import sleep


def hashToHex(hash):
    return format(hash, '064x')


def invsOrderedbyTime(invListExpected, txinvs):
    for x in range(60):
        with mininode_lock:
            if (invListExpected == txinvs):
                return True
        time.sleep(1)

    for i in range(len(txinvs)):
        if invListExpected.index(txinvs[i]) < i:
            return False
    return True


class TxnInvOrder(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.num_txns = 3

    def make_txns(self, fee, num, ids, testnode):
        self.nodes[0].settxfee(fee)
        txids = []
        for i in range(num):
            txids.append(self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1))
        ids.extend(txids)
        return ids

        # For checking if the txn queues for all peers have drained yet
    def check_queue_drains(self):
        peerinfo = self.nodes[0].getpeerinfo()
        for peer in peerinfo:
            txninvsize = peer['txninvsize']
            if (txninvsize > 0):
                return False
        return True

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

    # For checking the txns make it into all peers mempools
    def check_final_mempool(self):
        for n in range(0, self.num_nodes):
            mempoolsize = self.nodes[n].getmempoolinfo()['size']
            if(mempoolsize != self.num_txns*4):
                return False
        return True

    def test_inventory_order(self):
        self.stop_node(0)
        with self.run_node_with_connections("", 0, ['-broadcastdelay=50000', '-txnpropagationfreq=50000'], 2) as p2pc:
            connection = p2pc[0]
            connection2 = p2pc[1]

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
                sleep(0.1)

            wait_until(lambda: invsOrderedbyTime(ids, txinvs), timeout=20)

            assert (invsOrderedbyTime(ids, txinvs))

    def run_test(self):
        # Make some coins to spend
        self.log.info("Mining blocks...")
        self.nodes[0].generate(100)
        sync_blocks(self.nodes)
        self.test_inventory_order()

if __name__ == '__main__':
    TxnInvOrder().main()
