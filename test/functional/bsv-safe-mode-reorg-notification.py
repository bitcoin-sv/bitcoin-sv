#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Tests webhook notifications for correct information about reorg.
"""
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from json import loads

from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
from test_framework.cdefs import SAFE_MODE_DEFAULT_MIN_FORK_LENGTH
from test_framework.mininode import msg_block
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until


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


class SafeModeReogNotification(BitcoinTestFramework):

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

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def make_chain(self, conn, root_block, n_blocks):
        result = []
        last_block = root_block
        while(len(result) < n_blocks):
            last_block, self.last_block_time = make_block(conn, last_block, last_block_time=self.last_block_time)
            result.append(last_block)
        return result

    def check_safe_mode_data(self, rpc, forks, check_webhook_messages=True):
        json_messages = [rpc.getsafemodeinfo()]
        if check_webhook_messages:
            json_messages.append(self.webhook_messages[-1])
        for safe_mode_json in json_messages:
            if not forks:
                assert safe_mode_json["safemodeenabled"] == False
            else:
                assert safe_mode_json["safemodeenabled"] == True
                assert len(forks) == len(safe_mode_json["forks"])
                for fork_expected, fork_json in zip(forks, safe_mode_json["forks"]):
                    assert fork_expected["forkfirstblock"] == fork_json["forkfirstblock"]["hash"]
                    assert fork_expected["lastcommonblock"] == fork_json["lastcommonblock"]["hash"], f'{fork_expected["lastcommonblock"]}, {fork_json["lastcommonblock"]["hash"]}'
                    assert fork_expected["tips"] == set(t["hash"] for t in fork_json["tips"])

    def wait_for_safe_mode_data(self, rpc, forks, check_webhook_messages=True):

        def is_safe_mode_data_ok():
            try:
                self.check_safe_mode_data(rpc, forks, check_webhook_messages)
            except AssertionError:
                return False
            return True

        wait_until(is_safe_mode_data_ok, check_interval=0.3)

    def check_last_webhook_msg_reorged_from(self, old_tip, numberofdisconnectedblocks=None):
        if old_tip is None:
            assert self.webhook_messages[-1]["reorg"]["happened"] == False
            assert self.webhook_messages[-1]["reorg"]["oldtip"] == None
            assert self.webhook_messages[-1]["reorg"]["numberofdisconnectedblocks"] == 0
        else:
            assert self.webhook_messages[-1]["reorg"]["happened"] == True
            assert self.webhook_messages[-1]["reorg"]["oldtip"]["hash"] == old_tip
            if numberofdisconnectedblocks is not None:
                assert self.webhook_messages[-1]["reorg"]["numberofdisconnectedblocks"] == numberofdisconnectedblocks

    def run_test(self):

        self.PORT = 8765
        self.webhook_messages = []
        self.server = HTTPServer(('', self.PORT), self.make_handler)
        self.start_server()

        args = [f"-safemodewebhookurl=http://127.0.0.1:{self.PORT}/safemode",
                ]

        with self.run_node_with_connections("Test Reorg", 0, args,
                                            2) as (conn, conn2):
            conn.rpc.generate(1)

            root_block, root_block_time = make_block(conn, last_block_time=0)
            self.last_block_time = root_block_time
            send_by_headers(conn, [root_block], do_send_blocks=True)
            wait_for_tip(conn, root_block.hash)

            # the main chain, just enough to be able to riger the safe mode after reorg
            main_chain = self.make_chain(conn, root_block, SAFE_MODE_DEFAULT_MIN_FORK_LENGTH)
            expected_main_chain_fork_data = {"forkfirstblock": main_chain[0].hash, "tips": {main_chain[-1].hash},
                                             "lastcommonblock": root_block.hash}

            # the new chain, just enough to be able to triger the reorg
            new_chain = self.make_chain(conn, root_block, len(main_chain) + 1)
            expected_new_chain_fork_data = {"forkfirstblock": new_chain[0].hash, "tips": {new_chain[-1].hash},
                                            "lastcommonblock": root_block.hash}

            # sending the main chain
            send_by_headers(conn, main_chain, do_send_blocks=True)
            wait_for_tip(conn, main_chain[-1].hash)

            # send headers of the new chain and verify that we are in the safe mode
            send_by_headers(conn, new_chain, do_send_blocks=False)
            wait_for_tip_status(conn, new_chain[-1].hash, "headers-only")
            self.wait_for_safe_mode_data(conn.rpc, [expected_new_chain_fork_data])
            self.check_last_webhook_msg_reorged_from(None)
            self.webhook_messages = []

            # now send blocks of the new chain
            for bl in new_chain:
                conn.send_message(msg_block(bl))

            # a reorg happened, tip should be at last block of the new chain
            wait_for_tip(conn, new_chain[-1].hash)
            # still in the safe mode, but fork is the main chain
            self.wait_for_safe_mode_data(conn.rpc, [expected_main_chain_fork_data])
            # last block caused an reorg, check if got correct notification
            self.check_last_webhook_msg_reorged_from(main_chain[-1].hash, len(main_chain))

            # extending the new chain, just enough to be able to triger the safe mode after sending headers
            new_chain_extension = self.make_chain(conn, new_chain[-1], SAFE_MODE_DEFAULT_MIN_FORK_LENGTH)
            expected_new_chain_ext_fork_data = {"forkfirstblock": new_chain_extension[0].hash, "tips": {new_chain_extension[-1].hash},
                                                "lastcommonblock": new_chain[-1].hash}

            # sending the new chain extension
            send_by_headers(conn, new_chain_extension, do_send_blocks=False)
            wait_for_tip_status(conn, new_chain_extension[-1].hash, "headers-only")
            # two forks main chain from before and new chain extension
            self.wait_for_safe_mode_data(conn.rpc, [expected_main_chain_fork_data,
                                                    expected_new_chain_ext_fork_data,
                                                    ])
            # no reorg
            self.check_last_webhook_msg_reorged_from(None)

            # now send blocks of the new chain extension
            for bl in new_chain_extension:
                conn.send_message(msg_block(bl))
            # the tip has advanced
            wait_for_tip(conn, new_chain_extension[-1].hash)
            self.wait_for_safe_mode_data(conn.rpc, [expected_main_chain_fork_data,
                                                    ])
            # still no reorg
            self.check_last_webhook_msg_reorged_from(None)

            # invalidating firs block of the new chain extension
            conn.rpc.invalidateblock(new_chain_extension[0].hash)
            # rolled back
            wait_for_tip(conn, new_chain[-1].hash)
            self.wait_for_safe_mode_data(conn.rpc, [expected_main_chain_fork_data,
                                                    expected_new_chain_ext_fork_data,
                                                    ])
            # rolling back is qualified as an reorg
            self.check_last_webhook_msg_reorged_from(new_chain_extension[-1].hash, len(new_chain_extension))

        self.kill_server()


if __name__ == '__main__':
    SafeModeReogNotification().main()
