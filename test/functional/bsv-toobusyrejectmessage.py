#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import p2p_port, disconnect_nodes
from test_framework.blocktools import create_block, create_coinbase, assert_equal

import contextlib
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

    def run_test(self):
        @contextlib.contextmanager
        def run_connection(title):
            logger.debug("setup %s", title)

            self.start_node(0)       

            test_nodes = []
            for i in range(self.num_peers):
                test_nodes.append(NodeConnCB())

            connections = []
            for test_node in test_nodes:
                connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node)
                connections.append(connection)
                test_node.add_connection(connection)

            thr = NetworkThread()
            thr.start()
            for test_node in test_nodes:
                test_node.wait_for_verack()

            logger.debug("before %s", title)
            yield test_nodes
            logger.debug("after %s", title)

            for connection in connections:
                connection.close()
            del connections

            # once all connection.close() are complete, NetworkThread run loop completes and thr.join() returns success
            thr.join()
            disconnect_nodes(self.nodes[0],1)
            self.stop_node(0)
            logger.debug("finished %s", title)

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
                test_node.send_message(msg_reject(message=b"getdata", code=self.REJECT_TOOBUSY, reason=b"node too busy"))


        with run_connection("Scenario 1: sending TOOBUSY reject message with 2 nodes") as test_nodes:
            block = self.prepareBlock()

            for test_node in test_nodes:
                test_node.on_getdata = on_getdata
                headers_message = msg_headers()
                headers_message.headers = [CBlockHeader(block)]
                test_node.send_message(headers_message)
                test_node.wait_for_getdata(block.sha256)
                test_node.sync_with_ping()

            for key, value in askedFor.items():
                assert_equal(value, 1)
            assert_equal(len(askedFor), 2)

        self.num_peers = 1
        askedFor = {}
        rejectSent = False

        with run_connection("Scenario 2: sending TOOBUSY reject message with 1 node") as test_nodes:
            block = self.prepareBlock()

            test_node = test_nodes[0]
            test_node.on_getdata = on_getdata

            headers_message = msg_headers()
            headers_message.headers = [CBlockHeader(block)]

            begin_test = datetime.datetime.now()
            test_node.send_message(headers_message)

            test_node.wait_for_getdata(block.sha256)     
            test_node.last_message["getdata"] = []

            test_node.wait_for_getdata(block.sha256)
            end_test = datetime.datetime.now()
            assert(end_test - begin_test > datetime.timedelta(seconds = 5))

            assert_equal(next(iter(askedFor.values())), 2)
            assert_equal(len(askedFor), 1)

if __name__ == '__main__':
    TooBusyRejectMsgTest().main()