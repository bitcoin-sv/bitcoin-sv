#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Testing on two nodes
1. Creating bunch of blocks on first node.
2. Restarting first node wirh reindex flag set, no safe mode message should be received.
3. Starting second node wirh mocktime set 48 hours in the future, this triggers IBD. No safe mode message should be received.
"""
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from json import loads
from time import sleep

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import connect_nodes, wait_until


class WebhookHandler(BaseHTTPRequestHandler):

    def __init__(self, test, *a, **kw):
        self.test = test
        super(WebhookHandler, self).__init__(*a, **kw)

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        body = self.rfile.read(content_length).decode("utf-8")
        self.test.webhook_messages.append(loads(body))

        self.send_response(200)
        self.send_header('Content-type', 'text/html')
        self.end_headers()
        self.wfile.write("POST request for {}".format(self.path).encode('utf-8'))

    def log_message(self, format, *args):
        pass


class SafeModeIBDTest(BitcoinTestFramework):

    def start_server(self):
        self.serverThread = threading.Thread(target=self.server.serve_forever)
        self.serverThread.deamon = True
        self.serverThread.start()

    def kill_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.serverThread.join()

    def make_handler(self, *a, **kw):
        return WebhookHandler(self,  *a, **kw)

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):

        self.stop_node(1)

        #create bunch of blocks and stop the node
        self.nodes[0].generate(20)
        tip_hash = self.nodes[0].getbestblockhash()
        self.stop_node(0)

        # setting up webhook http server
        self.PORT = 8999
        self.webhook_messages = []
        self.server = HTTPServer(('', self.PORT), self.make_handler)
        self.start_server()

        # restart node and force reindexing
        self.start_node(0, extra_args=["-reindex=1", f"-safemodewebhookurl=http://127.0.0.1:{self.PORT}/safemode",])
        # wait until reindexing finishes
        wait_until(lambda: self.nodes[0].getbestblockhash() == tip_hash,
                   timeout=30,
                   check_interval=1,
                   label=f"waiting until {hash} become tip")

        # give time for messages to arrive
        sleep(5)
        # we should not receive receive messages
        assert len(self.webhook_messages) == 0

        # start second node with 48 hours in the furture. this will trigger initial block download mode
        self.start_node(1, extra_args=[f"-mocktime={int(time.time()) + 48*60*60}", f"-safemodewebhookurl=http://127.0.0.1:{self.PORT}/safemode",])
        connect_nodes(self.nodes, 0, 1)

        # wait until nodes sync themselves
        wait_until(lambda: self.nodes[1].getbestblockhash() == tip_hash,
                   timeout=30,
                   check_interval=1,
                   label=f"waiting until {hash} become tip")

        # exit the IBD by generating one block, making the tip less than 24 hours behind
        self.nodes[1].generate(1)

        # still we should not receive receive messages
        assert len(self.webhook_messages) == 0

        self.kill_server()


if __name__ == '__main__':
    SafeModeIBDTest().main()
