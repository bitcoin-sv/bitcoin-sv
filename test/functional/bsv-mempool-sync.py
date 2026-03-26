#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Test primary mempool sync'ing with the extended mempool P2P message.

Start 3 nodes.
Configure 2 of the nodes to periodically synchronise their mempools.
Add txns with different ages to each node's mempool (some in primary, some only in secondary).
Check the behaviour of both the syncing and non-syncing nodes.
'''

from test_framework.mininode import COIN, COutPoint, CTxIn, CTxOut, CTransaction, ToHex, P2PEventHandler, msg_mempool
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, wait_until, disconnect_nodes_bi, connect_nodes, check_for_log_msg, open_log_file

import time


# Our message callback handler
class TestNode(P2PEventHandler):
    last_inv_count = None
    last_mempool_request = None
    mempool_request_count = 0

    def on_mempool(self, conn, message):
        self.last_mempool_request = message
        self.mempool_request_count += 1

    def on_inv(self, conn, message):
        self.last_inv_count = len(message.inv)


# An old style mempool msg
class msg_mempool_old():
    command = b"mempool"

    def serialize(self):
        return b""


class MempoolSync(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ["-mempoolsyncpeer=127.0.0.1", "-whitelist=127.0.0.1", "-mempoolsyncage=5", "-mempoolsyncperiod=1", "-importsync=1"],
            ["-mempoolsyncpeer=127.0.0.1", "-mempoolsyncage=10", "-mempoolsyncperiod=2"],
            []
        ]

    def run_test(self):
        # Generate some coins
        self.nodes[0].generate(111)
        self.sync_all()

        # Get UTXOs
        utxos = self.nodes[0].listunspent()
        assert (len(utxos) > 10)

        # Disconnect nodes
        disconnect_nodes_bi(self.nodes, 0, 1)
        disconnect_nodes_bi(self.nodes, 0, 2)
        disconnect_nodes_bi(self.nodes, 1, 2)

        # Add a txn to each node's mempool
        def send_txn(utxo, value, fee, node, mock_time):
            node.setmocktime(mock_time)
            tx = CTransaction()
            tx.vin = [
                CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript(), 0xffffffff)
            ]
            tx.vout = [
                CTxOut(value - fee, CScript([OP_TRUE]))
            ]
            tx_signed = self.nodes[0].signrawtransaction(ToHex(tx))["hex"]
            tx_id = node.sendrawtransaction(tx_signed)
            wait_until(lambda: tx_id in node.getrawmempool())

        cur_time = int(time.time())

        # Add 3 paying txns and 1 non-paying txn to node0 with different ages
        with open_log_file(self.nodes[0]) as log_file:
            mock_time = cur_time
            for i in range(4):
                utxo = utxos.pop()
                value = int(utxo["amount"]) * COIN
                fee = 100 if i > 0 else 0
                send_txn(utxo, value, fee, self.nodes[0], mock_time)
                mock_time -= 1
                wait_until(lambda: check_for_log_msg(self, "[txnpropagator] Got 1 new transactions", log_file=log_file))

        # Add 2 paying txns and 1 non-paying txn to node1 with different ages
        with open_log_file(self.nodes[1]) as log_file:
            mock_time = cur_time
            for i in range(3):
                utxo = utxos.pop()
                value = int(utxo["amount"]) * COIN
                fee = 100 if i > 0 else 0
                send_txn(utxo, value, fee, self.nodes[1], mock_time)
                mock_time -= 1
                wait_until(lambda: check_for_log_msg(self, "[txnpropagator] Got 1 new transactions", log_file=log_file))

        # Add 1 paying txn and 1 non-paying txn to node2
        with open_log_file(self.nodes[0]) as log_file:
            mock_time = cur_time
            for i in range(2):
                utxo = utxos.pop()
                value = int(utxo["amount"]) * COIN
                fee = 100 if i > 0 else 0
                send_txn(utxo, value, fee, self.nodes[2], mock_time)
                wait_until(lambda: check_for_log_msg(self, "[txnpropagator] Got 1 new transactions", log_file=log_file))

        # Get us a test framework connection to node2
        self.stop_node(2)
        with self.run_node_with_connections("Mempool sync 2", 2, self.extra_args[2], cb_class=TestNode, number_of_connections=1) as (conn2,):

            # Re-apply mocktime
            self.nodes[2].setmocktime(cur_time)

            # Check new-style mempool request to peer not configured to synchronise with us is ignored
            conn2.send_message(msg_mempool(age=1))
            wait_until(lambda: check_for_log_msg(self, "Ignoring mempool sync request from unknown peer", "/node2"))
            assert (conn2.transport.cb.last_inv_count is None)

        # Restart node2
        self.start_node(2)
        self.nodes[2].setmocktime(cur_time)

        # Get us a test framework connection to node1
        self.stop_node(1)
        with self.run_node_with_connections("Mempool sync 1", 1, self.extra_args[1], cb_class=TestNode, number_of_connections=1) as (conn1,):

            # Re-apply mocktime
            self.nodes[1].setmocktime(cur_time)

            # Should get @5 mempool requests in 10 seconds
            time.sleep(10)
            assert (conn1.transport.cb.mempool_request_count >= 4 and conn1.transport.cb.mempool_request_count <= 6)

            # Check old-style mempool msg from a non-whitelisted is disabled
            conn1.send_message(msg_mempool_old())
            wait_until(lambda: check_for_log_msg(self, "mempool request from nonwhitelisted peer disabled", "/node1"))
            assert (conn1.transport.cb.last_inv_count is None)

        # Restart node1
        self.start_node(1)
        self.nodes[1].setmocktime(cur_time)

        # Get us a test framework connection to node0
        self.stop_node(0)
        with self.run_node_with_connections("Mempool sync 0", 0, self.extra_args[0], cb_class=TestNode, number_of_connections=1) as (conn0,):

            # Re-apply mocktime
            self.nodes[0].setmocktime(cur_time)

            # Reconnect nodes
            connect_nodes(self.nodes, 0, 1)
            connect_nodes(self.nodes, 0, 2)
            connect_nodes(self.nodes, 1, 2)
            assert_equal(len(self.nodes[0].getrawmempool()), 4)
            assert_equal(len(self.nodes[1].getrawmempool()), 3)
            assert_equal(len(self.nodes[2].getrawmempool()), 2)

            # Old style mempool request from whitelisted node should still get us everything (4 txns)
            conn0.send_message(msg_mempool_old())
            wait_until(lambda: conn0.transport.cb.last_inv_count == 4)

            # Mempool request with age 4 second will not get any response because txn ages are 0,1,2,3
            conn0.transport.cb.last_inv_count = None
            conn0.send_message(msg_mempool(age=4))
            time.sleep(1)
            assert (conn0.transport.cb.last_inv_count is None)

            # Mempool request with age 3 will get 1 response
            conn0.transport.cb.last_inv_count = None
            conn0.send_message(msg_mempool(age=3))
            wait_until(lambda: conn0.transport.cb.last_inv_count == 1)

            # Mempool request with age 2 will get 2 responses
            conn0.transport.cb.last_inv_count = None
            conn0.send_message(msg_mempool(age=2))
            wait_until(lambda: conn0.transport.cb.last_inv_count == 2)

            # Mempool request with age 1 will get 3 responses
            conn0.transport.cb.last_inv_count = None
            conn0.send_message(msg_mempool(age=1))
            wait_until(lambda: conn0.transport.cb.last_inv_count == 3)

            # Mempool request with age 0 will still only get 3 responses because 4th txn is only in secondary pool
            conn0.transport.cb.last_inv_count = None
            conn0.send_message(msg_mempool(age=0))
            wait_until(lambda: conn0.transport.cb.last_inv_count == 3)

            # Move time on so every txn ages 1 second
            self.nodes[0].setmocktime(cur_time + 1)

            # Now a mempool request with age 4 will get 1 response
            conn0.transport.cb.last_inv_count = None
            conn0.send_message(msg_mempool(age=4))
            wait_until(lambda: conn0.transport.cb.last_inv_count == 1)

            # Should get @5 mempool requests in 5 seconds
            conn0.transport.cb.mempool_request_count = 0
            time.sleep(5)
            assert (conn0.transport.cb.mempool_request_count >= 4 and conn0.transport.cb.mempool_request_count <= 6)

            # Check nodes 0 and 1 sync their primary mempools once proper time is restored
            assert_equal(len(self.nodes[0].getrawmempool()), 4)
            assert_equal(len(self.nodes[1].getrawmempool()), 3)
            assert_equal(len(self.nodes[2].getrawmempool()), 2)
            self.nodes[0].setmocktime(0)
            self.nodes[1].setmocktime(0)
            self.nodes[2].setmocktime(0)
            wait_until(lambda: len(self.nodes[0].getrawmempool()) == 6)
            wait_until(lambda: len(self.nodes[1].getrawmempool()) == 6)

            # Node2 will also get the txns via standard inv announcement from one of the other nodes
            wait_until(lambda: len(self.nodes[2].getrawmempool()) == 7)


if __name__ == '__main__':
    MempoolSync().main()
