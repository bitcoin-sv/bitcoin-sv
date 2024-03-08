#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import threading
import json
import http.client as httplib
from functools import partial
from http.server import HTTPServer
from ds_callback_service.CallbackService import CallbackService, RECEIVE, STATUS, RESPONSE_TIME, FLAG
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import check_for_log_msg, assert_equal
from test_framework.mininode import *
from test_framework.script import *

import time

'''
Test badly behaving double-spend endpoints:

1) Endpoints returning HTTP codes other than 200.
2) Endpoint that is slow just once.
3) Endpoint that is always slow.
4) Server is too slow to work with.
5) Server doesn't contain x-bsv-dsnt in header.
'''

# 127.0.0.1 as network-order bytes
LOCAL_HOST_IP = 0x7F000001
WRONG_IP = 0x7F000002


class DoubleSpendHandlerErrors(BitcoinTestFramework):

    def __del__(self):
        self.kill_server()

    def set_test_params(self):
        self.num_nodes = 1
        self.callback_service = "localhost:8080"
        self.extra_args = [['-dsendpointport=8080',
                            '-banscore=100000',
                            '-genesisactivationheight=1'
                            ]]

    def start_server(self):
        self.serverThread = threading.Thread(target=self.server.serve_forever)
        self.serverThread.deamon = True
        self.serverThread.start()

    def kill_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.serverThread.join()

    def create_and_send_transaction(self, inputs, outputs):
        tx = CTransaction()
        tx.vin = inputs
        tx.vout = outputs
        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx))["hex"]
        tx = FromHex(CTransaction(), tx_hex)

        self.node0.send_message(msg_tx(tx))

        return tx

    def check_tx_received(self, tx_hash):
        self.conn.request(
            'GET',
            "/dsnt/1/received"
        )

        queried = self.conn.getresponse()
        assert_equal(queried.status, 200)
        return tx_hash in json.loads(queried.read())

    def check_tx_not_received(self, tx_hash):
        time.sleep(3)
        self.conn.request(
            'GET',
            "/dsnt/1/received"
        )

        queried = self.conn.getresponse()
        assert_equal(queried.status, 200)
        return tx_hash not in json.loads(queried.read())

    def check_ds_enabled(self, utxo):
        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # tx2 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Script verification for double-spend passed", "/node0"))

        return tx1.hash

    def check_ds_enabled_error_msg(self, utxo, log_msg):
        assert(not check_for_log_msg(self, log_msg, "/node0"))
        tx_hash = self.check_ds_enabled(utxo)
        wait_until(lambda: check_for_log_msg(self, log_msg, "/node0"), timeout=70)
        return tx_hash

    def run_test(self):

        self.nodes[0].generate(110)
        utxo = self.nodes[0].listunspent()

        self.stop_node(0)
        with self.run_node_with_connections("Server returning 400", 0, ['-dsendpointport=8080'], 1) as p2p_connections:
            # Turn on CallbackService.
            handler = partial(CallbackService, RECEIVE.YES, STATUS.CLIENT_ERROR, RESPONSE_TIME.FAST, FLAG.YES)
            self.server = HTTPServer(('localhost', 8080), handler)
            self.start_server()
            self.conn = httplib.HTTPConnection(self.callback_service)

            self.node0 = p2p_connections[0]
            self.check_ds_enabled_error_msg(utxo[0], "Got 400 response from endpoint")

            self.kill_server()

        with self.run_node_with_connections("Server returning 500", 0, ['-dsendpointport=8080'], 1) as p2p_connections:
            # Turn on CallbackService.
            handler = partial(CallbackService, RECEIVE.YES, STATUS.SERVER_ERROR, RESPONSE_TIME.FAST, FLAG.YES)
            self.server = HTTPServer(('localhost', 8080), handler)
            self.start_server()
            self.conn = httplib.HTTPConnection(self.callback_service)

            self.node0 = p2p_connections[0]
            self.check_ds_enabled_error_msg(utxo[1], "Got 500 response from endpoint")
            self.check_ds_enabled_error_msg(utxo[2], "Skipping notification to blacklisted endpoint")

            self.kill_server()

        with self.run_node_with_connections("Server is slow, but functional", 0, ['-dsendpointport=8080'], 1) as p2p_connections:
            # Turn on CallbackService.
            handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.SLOW, FLAG.YES)
            self.server = HTTPServer(('localhost', 8080), handler)
            self.start_server()
            self.conn = httplib.HTTPConnection(self.callback_service)

            self.node0 = p2p_connections[0]
            tx_hash = self.check_ds_enabled_error_msg(utxo[3], "Timeout sending notification to endpoint 127.0.0.1, resubmitting to the slow queue")

            wait_until(lambda: self.check_tx_received(tx_hash))

            self.kill_server()

        with self.run_node_with_connections("Server is consistently slow, but functional", 0, ['-dsendpointport=8080','-dsendpointslowrateperhour=2'], 1) as p2p_connections:
            # Turn on CallbackService.
            handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.SLOW, FLAG.YES)
            self.server = HTTPServer(('localhost', 8080), handler)
            self.start_server()
            self.conn = httplib.HTTPConnection(self.callback_service)
            self.node0 = p2p_connections[0]

            tx_hash = self.check_ds_enabled(utxo[0])
            wait_until(lambda: check_for_log_msg(self, "Started tracking stats for a new potentially slow endpoint 127.0.0.1", "/node0"))
            wait_until(lambda: self.check_tx_received(tx_hash))
            tx_hash = self.check_ds_enabled(utxo[1])
            wait_until(lambda: check_for_log_msg(self, "Updated stats for potentially slow endpoint 127.0.0.1, is slow: 0", "/node0"))
            wait_until(lambda: self.check_tx_received(tx_hash))
            tx_hash = self.check_ds_enabled(utxo[2])
            wait_until(lambda: check_for_log_msg(self, "Updated stats for potentially slow endpoint 127.0.0.1, is slow: 1", "/node0"))
            wait_until(lambda: self.check_tx_received(tx_hash))
            tx_hash = self.check_ds_enabled(utxo[3])
            wait_until(lambda: check_for_log_msg(self, "Endpoint 127.0.0.1 is currently slow, submitting via the slow queue", "/node0"))
            wait_until(lambda: self.check_tx_received(tx_hash))

            self.kill_server()

        with self.run_node_with_connections("Server is too slow, bitcoind ignores it", 0, ['-dsendpointport=8080'], 1) as p2p_connections:
            # Turn on CallbackService.
            handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.SLOWEST, FLAG.YES)
            self.server = HTTPServer(('localhost', 8080), handler)
            self.start_server()
            self.conn = httplib.HTTPConnection(self.callback_service)

            self.node0 = p2p_connections[0]
            tx_hash = self.check_ds_enabled_error_msg(utxo[4], "Timeout sending slow-queue notification to endpoint 127.0.0.1")

            self.check_tx_not_received(tx_hash)

            self.kill_server()

        with self.run_node_with_connections("Server has no x-bsv-dsnt in header", 0, ['-dsendpointport=8080'], 1) as p2p_connections:
            # Turn on CallbackService.
            handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.NO)
            self.server = HTTPServer(('localhost', 8080), handler)
            self.start_server()
            self.conn = httplib.HTTPConnection(self.callback_service)

            self.node0 = p2p_connections[0]
            tx_hash = self.check_ds_enabled_error_msg(utxo[5], "Missing x-bsv-dsnt header in response from endpoint 127.0.0.1")

            self.check_tx_not_received(tx_hash)

            self.kill_server()


if __name__ == '__main__':
    DoubleSpendHandlerErrors().main()
