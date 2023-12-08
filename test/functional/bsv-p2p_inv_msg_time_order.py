#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from time import sleep

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG


class P2PInvMsgTimeOrder(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    # This function takes unspent transaction and returns transaction (pay to random address), second (optional)
    # parameter is fee that we want to pay for this transaction.
    def make_tx(self, unspent_transaction, fee=10000):
        unspent_amount = int(unspent_transaction['amount']) * 100000000  # BTC to Satoshis
        ftx = CTransaction()
        ftx.vout.append(CTxOut(unspent_amount - fee, CScript([OP_DUP, OP_HASH160,
                                                              hex_str_to_bytes(
                                                                  "ab812dc588ca9d5787dde7eb29569da63c3a238c"),
                                                              OP_EQUALVERIFY,
                                                              OP_CHECKSIG])))  # Pay to random address
        ftx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(unspent_transaction["txid"])[::-1]),
                                       unspent_transaction["vout"])))
        ftx.rehash()
        ftx_hex = self.nodes[0].signrawtransaction(ToHex(ftx))['hex']
        ftx = FromHex(CTransaction(), ftx_hex)
        ftx.rehash()

        return ftx

    def run_test_parametrized(self):
        self.stop_node(0)
        with self.run_node_with_connections("", 0, ['-broadcastdelay=500', '-txnpropagationfreq=500'], 2) as p2pc:
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

            # initialize
            self.nodes[0].generate(1)

            # List of transactions to be send (used for temporary storing created transactions)
            transactions_to_send = []
            # List of transaction hashes in order in which they are sent
            transaction_list_by_time = []

            # List of all unspent transactions available in self.node[0]
            unspent_txns = self.nodes[0].listunspent()

            # List of fees (in increasing order)
            fees = range(100000, 500000, 1000)

            # Make transactions with fees defined above
            for i in range(len(unspent_txns)):
                tx = self.make_tx(unspent_txns[i], fees[i])
                transactions_to_send.append(tx)

            # Send all transactions that have been previously assembled and signed
            for txts in transactions_to_send:
                transaction_list_by_time.append(txts.hash)
                connection.send_message(msg_tx(txts))

            # Due to asynchronous validation we can not expect that an order of receiving transactions is the same as order of sending.
            for txid in transaction_list_by_time:
                wait_until(lambda:  txid in txinvs, lock=mininode_lock, timeout=20)

            with mininode_lock:
                # Assert the number of received transactions is the same as the number of sent transactions.
                assert_equal(len(transaction_list_by_time), len(txinvs))

    def run_test(self):
        self.run_test_parametrized()


if __name__ == '__main__':
    P2PInvMsgTimeOrder().main()
