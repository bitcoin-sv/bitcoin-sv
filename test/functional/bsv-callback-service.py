#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import threading
import json
import http.client as httplib
from functools import partial
from ds_callback_service.CallbackService import CallbackService, RECEIVE, STATUS, RESPONSE_TIME, FLAG
from http.server import HTTPServer
from test_framework.util import assert_equal

'''
Test the mock double-spend notification endpoint server.
'''


class CallBackServiceTest():
    def start_server(self):
        self.serverThread = threading.Thread(target=self.server.serve_forever)
        self.serverThread.deamon = True
        self.serverThread.start()

    def kill_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.serverThread.join()

    def send_query(self, txid):
        self.conn.request(
            'GET',
            "/dsnt/1/query/{}".format(txid)
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, 200)
        resp.read()

    def send_submit(self, txid, n, ctxid, cn, status=200, reason="OK"):
        self.conn.request(
            'POST',
            "/dsnt/1/submit?txid={}&n={}&ctxid={}&cn={}".format(txid, n, ctxid, cn)
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, status)
        assert_equal(resp.reason, reason)
        resp.read()

    def send_queried(self):
        self.conn.request(
            'GET',
            "/dsnt/1/queried"
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, 200)
        return resp.read()

    def send_received(self):
        self.conn.request(
            'GET',
            "/dsnt/1/received"
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, 200)
        return resp.read()

    def main(self):

        self.callback_service = "localhost:8080"

        #turn on server
        handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServer(('localhost', 8080), handler)
        self.start_server()
        self.conn = httplib.HTTPConnection(self.callback_service)

        # in the beginning, queried list is empty
        queried_list = self.send_queried()
        assert_equal(json.loads(queried_list), [])

        tx_ids = ["123468889936b87b8d64211e1d2a36afebff6c73eaa8fa4a849cfe099076e723",
                  "132468889936b87b8d64211e1d2a36afebff6c73eaa8fa4a849cfe099076e723",
                  "312468889936b87b8d64211e1d2a36afebff6c73eaa8fa4a849cfe099076e723"]

        # send queries for some transactions
        for txid in tx_ids:
            self.send_query(txid)

        # check that these transactions were saved
        queried_list = self.send_queried()
        assert_equal(json.loads(queried_list), tx_ids)

        # no proofs were received so far
        received_list = self.send_received()
        assert_equal(json.loads(received_list), [])

        # submit proof for first transaction
        self.send_submit(tx_ids[0], 0, "randomhash", 0)

        # check that first tx was removed from queried list
        queried_list = self.send_queried()
        assert_equal(json.loads(queried_list), tx_ids[1:])

        # check that first tx's proof was received
        received_list = self.send_received()
        assert_equal(json.loads(received_list), [tx_ids[0]])

        # submit proof for these transactions
        self.send_submit(tx_ids[1], 0, "randomhash", 0)
        self.send_submit(tx_ids[2], 0, "randomhash", 0)

        # check that queried list is empty
        queried_list = self.send_queried()
        assert_equal(json.loads(queried_list), [])

        # check that received list contains all three proofs now
        received_list = self.send_received()
        assert_equal(json.loads(received_list), tx_ids)

        # bad URL
        self.conn.request(
            'GET',
            "/dsnt/1/queried/a/b/c"
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, 400)
        assert_equal(resp.reason, "Malformed URL.")

        # incorrect parameters in URL
        self.conn.request(
            'POST',
            "/dsnt/1/submit?txid=abc&n=0"
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, 400)
        assert_equal(resp.reason, "Malformed URL: URL should contain 'txid', 'n', 'ctxid', 'cn' parameters.")

        self.send_submit(tx_ids[0], -5, "randomhash", 0, 400, "Malformed URL: output number should be positive int or 0.")
        self.send_submit(tx_ids[0], 0, "randomhash", "abc", 400, "Malformed URL: output number should be positive int or 0.")
        self.send_submit(tx_ids[0], 0, "randomhash", 0, 400, "This txid was not asked for.")
        self.send_submit(tx_ids[0], 0, tx_ids[0], 0, 400, "Malformed URL: txid must not be the same as ctxid.")

        self.kill_server()

        handler = partial(CallbackService, RECEIVE.YES, STATUS.SERVER_ERROR, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServer(('localhost', 8080), handler)
        self.start_server()

        self.conn.request(
            'GET',
            "/dsnt/1/queried"
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, 500)
        resp.read()

        self.kill_server()

        handler = partial(CallbackService, RECEIVE.YES, STATUS.CLIENT_ERROR, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServer(('localhost', 8080), handler)
        self.start_server()

        self.conn.request(
            'GET',
            "/dsnt/1/queried"
        )

        resp = self.conn.getresponse()
        assert_equal(resp.status, 400)
        resp.read()

        self.kill_server()


if __name__ == '__main__':
    CallBackServiceTest().main()
