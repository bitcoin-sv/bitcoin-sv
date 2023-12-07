#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.mininode import msg_getdata, CInv, wait_until
from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import assert_equal

# This tests checks GETDATA P2P message.
# 1. Check that sending GETDATA of unknown block does no action.
# 2. Check that sending GETDATA of known block returns BLOCK message.
# 3. Check that sending GETDATA of unknown transaction returns NOTFOUND message.
# 4. Check that sending GETDATA of known transaction returns TX message.


class GetDataTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):

        self.stop_node(0)

        with self.run_node_with_connections("send GETDATA messages and check responses", 0, [], 1) as p2p_connections:

            receivedBlocks = set()

            def on_block(conn, message):
                nonlocal receivedBlocks
                receivedBlocks.add(message.block.hash)

            receivedTxs = set()

            def on_tx(conn, message):
                nonlocal receivedTxs
                receivedTxs.add(message.tx.hash)

            receivedTxsNotFound = set()

            def on_notfound(conn, message):
                nonlocal receivedTxsNotFound
                for inv in message.inv:
                    receivedTxsNotFound.add(inv.hash)

            self.nodes[0].generate(5)

            connection = p2p_connections[0]
            connection.cb.on_block = on_block
            connection.cb.on_tx = on_tx
            connection.cb.on_notfound = on_notfound

            # 1. Check that sending GETDATA of unknown block does no action.
            unknown_hash = 0xdecaf
            connection.cb.send_message(msg_getdata([CInv(CInv.BLOCK, unknown_hash)]))

            # 2. Check that sending GETDATA of known block returns BLOCK message.
            known_hash = self.nodes[0].getbestblockhash()
            connection.cb.send_message(msg_getdata([CInv(CInv.BLOCK, int(known_hash, 16))]))
            wait_until(lambda: known_hash in receivedBlocks)
            # previously requested unknown block is not in the received list
            assert_equal(unknown_hash not in receivedBlocks, True)
            # python automatically sends GETDATA for INV that it receives
            # this means we can receive more blocks than just the one previously requested
            assert_equal(len(receivedBlocks) >= 1, True)

            # 3. Check that sending GETDATA of unknown transaction returns NOTFOUND message.
            connection.cb.send_message(msg_getdata([CInv(CInv.TX, unknown_hash)]))
            wait_until(lambda: unknown_hash in receivedTxsNotFound)

            # 4. Check that sending GETDATA of known transaction returns TX message.
            known_hash = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1.0)
            connection.cb.send_message(msg_getdata([CInv(CInv.TX, int(known_hash, 16))]))
            wait_until(lambda: known_hash in receivedTxs)
            assert_equal(len(receivedTxs), 1)


if __name__ == '__main__':
    GetDataTest().main()
