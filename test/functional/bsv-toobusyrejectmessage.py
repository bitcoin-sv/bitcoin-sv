#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import p2p_port
from test_framework.blocktools import create_block, create_coinbase, assert_equal

import datetime

# This test checks TOOBUSY reject message and behaviour that it triggers.
# Scenario 1:
#   2 nodes (A and B) send HEADERS message to bitcoind. Bitcoind sends GetData to node A.
#   Node A then sends REJECT_TOOBUSY message. After that, node B should be asked for the same block (GetData).
# Scenario 2:
#   Node A sends HEADERS message to bitcoind. Bitcoind sends GetData to node A.
#   Node A sends REJECT_TOOBUSY message. Bitcoind waits and asks again after 5 seconds.


class TooBusyRejectMsgTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.num_peers = 2
        self.REJECT_TOOBUSY = int('0x44', 16)

    def prepareBlock(self):
        height = 1
        tip = int("0x" + self.nodes[0].getbestblockhash(), 0)
        block_time = int(time.time()) + 1
        block = create_block(tip, create_coinbase(height), block_time)
        block.solve()
        return block

    def getDataLambda(self, conn, block_hash):
        lm = conn.last_message.get("getdata")
        return lm and lm.inv[0].hash == block_hash

    def run_test(self):
        self.stop_node(0)

        askedFor = {}
        rejectSent = False

        def on_getdata(conn, message):
            if (conn in askedFor):
                askedFor[conn] += 1
            else:
                askedFor[conn] = 1
            nonlocal rejectSent
            # First node that receives GetData should send reject.
            if not rejectSent:
                rejectSent = True
                conn.send_message(msg_reject(message=b"getdata", code=self.REJECT_TOOBUSY, reason=b"node too busy"))

        with self.run_node_with_connections("Scenario 1: sending TOOBUSY reject message with 2 nodes", 0, [], self.num_peers) as connections:
            block = self.prepareBlock()

            for connection in connections:
                connection.cb.on_getdata = on_getdata
                headers_message = msg_headers()
                headers_message.headers = [CBlockHeader(block)]
                connection.cb.send_message(headers_message)
                wait_until(lambda: self.getDataLambda(connection.cb, block.sha256), lock=mininode_lock)

            for key, value in askedFor.items():
                assert_equal(value, 1)
            assert_equal(len(askedFor), 2)

        self.num_peers = 1
        askedFor = {}
        rejectSent = False

        with self.run_node_with_connections("Scenario 2: sending TOOBUSY reject message with 1 node", 0, [], self.num_peers) as connections:
            block = self.prepareBlock()

            connection = connections[0]
            connection.cb.on_getdata = on_getdata

            headers_message = msg_headers()
            headers_message.headers = [CBlockHeader(block)]

            begin_test = datetime.datetime.now()
            connection.cb.send_message(headers_message)

            wait_until(lambda: self.getDataLambda(connection.cb, block.sha256), lock=mininode_lock)

            connection.cb.last_message["getdata"] = []

            # Bitcoind asks again after 5 seconds.
            wait_until(lambda: self.getDataLambda(connection.cb, block.sha256), lock=mininode_lock)

            end_test = datetime.datetime.now()
            assert(end_test - begin_test > datetime.timedelta(seconds = 5))

            assert_equal(next(iter(askedFor.values())), 2)
            assert_equal(len(askedFor), 1)


if __name__ == '__main__':
    TooBusyRejectMsgTest().main()
