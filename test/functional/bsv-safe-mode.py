#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing entering and exiting the safe-mode. Testing command line params and RPC methods for ignoring
and reconsidering blocks for the safe mode activation. Testing webhook callbacks for the safe mode.

For different set of parameters (safemodemaxforkdistance, safemodeminforklength, safemodeminblockdifference) we doing these steps
and testing safe mode status with rpc and webhook:
1. Creating three different forks, every fork is at the limit with one parameter for the safe mode activation.
2. Restarting the node with command line params off by one, should not be in safe mode
3. Modifying chainstate (extending tips) to make some of forks not trigger the safe mode. Making the same using ignoresafemodeforblock
   and reconsidersafemodeforblock.
"""
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from json import loads

from test_framework.blocktools import make_block, send_by_headers, wait_for_tip, wait_for_tip_status
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


class SafeMode(BitcoinTestFramework):

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
        self.num_nodes = 1

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

    def run_rest_case(self, min_fork_len, max_height_difference, max_fork_distance):

        args = [f"-safemodemaxforkdistance={max_fork_distance}",
                f"-safemodeminforklength={min_fork_len}",
                f"-safemodeminblockdifference={max_height_difference}",
                f"-safemodewebhookurl=http://127.0.0.1:{self.PORT}/safemode",
                ]

        with self.run_node_with_connections("Preparation", 0, args,
                                            2) as (conn1, conn2):
            conn1.rpc.generate(1)

            root_block, root_block_time = make_block(conn1, last_block_time=0)
            self.last_block_time = root_block_time
            send_by_headers(conn1, [root_block], do_send_blocks=True)
            wait_for_tip(conn1, root_block.hash)

            # We will create
            # ========================================================
            #  mc -> main chain mc[N] is active tip
            #  sf -> short fork
            #  df -> distant fork
            #  ld -> low height difference fork
            #
            #  |--------------max_fork_distance------------------------|
            # root - mc[0] - mc[1] - mc[2] - mc[3] - ... - mc[N-1] -  mc[N]
            #  |         \                                          \
            #  |          \                                           sf[0] - sf[1] - ... -sf[N]
            #  |           \                                          |-----min_fork_len------|
            #   \           \
            #    \           ld[0] - ... - ld[N]
            #     \                          |---max_height_difference---|  -> (if negative ld[N] is behind active tip, infront otherwise)
            #      \
            #       \
            #        \
            #         df[0] - df[1] - ... df[N]
            #

            # the main chain, make it long enough to be able to create distant fork
            main_chain = self.make_chain(conn1, root_block, max_fork_distance)

            # the distant fork, last common block is at limit of acceptance
            distant_fork_len = max(max_fork_distance + max_height_difference, min_fork_len) + 10 # make it longer than neccesary
            distant_fork = self.make_chain(conn1, root_block, distant_fork_len)
            expected_distant_fork_data = {"forkfirstblock": distant_fork[0].hash, "tips": {distant_fork[-1].hash}, "lastcommonblock": root_block.hash}

            # the short fork, fork with minimal acceptable length
            short_fork = self.make_chain(conn1, main_chain[-2], min_fork_len)
            expected_short_fork_data = {"forkfirstblock": short_fork[0].hash, "tips": {short_fork[-1].hash}, "lastcommonblock": main_chain[-2].hash}

            # the low height difference fork; a fork whose tip is at minimal acceptable height relative to the chain tip
            low_height_difference_fork_len = len(main_chain) + max_height_difference - 1 # minus 1 is beacause we are starting at first block of the main chain
            low_height_difference_fork = self.make_chain(conn1, main_chain[0], low_height_difference_fork_len)
            expected_low_height_difference_fork_data = {"forkfirstblock": low_height_difference_fork[0].hash, "tips": {low_height_difference_fork[-1].hash}, "lastcommonblock": main_chain[0].hash}

            # send main branch that should be active chain
            send_by_headers(conn1, main_chain, do_send_blocks=True)
            wait_for_tip(conn1, main_chain[-1].hash)
            # no forks yes, not in the safe mode
            self.wait_for_safe_mode_data(conn1.rpc, []) # not in safe mode

            send_by_headers(conn1, distant_fork, do_send_blocks=False)
            wait_for_tip_status(conn1, distant_fork[-1].hash, "headers-only")
            # distant fork triggers the safe mode
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data])

            send_by_headers(conn1, short_fork, do_send_blocks=False)
            wait_for_tip_status(conn1, short_fork[-1].hash, "headers-only")
            # two forks triggering the safe mode: distant fork and short fork
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_short_fork_data,

                                                     ])

            send_by_headers(conn1, low_height_difference_fork, do_send_blocks=False)
            wait_for_tip_status(conn1, low_height_difference_fork[-1].hash, "headers-only")
            # all three forks triggering the safe mode
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     expected_short_fork_data,
                                                     ])

        # stopping the node
        self.webhook_messages = []
        args_off_by_one = [f"-safemodemaxforkdistance={max_fork_distance-1}",
                           f"-safemodeminforklength={min_fork_len+1}",
                           f"-safemodeminblockdifference={max_height_difference+1}",
                           f"-safemodewebhookurl=http://127.0.0.1:{self.PORT}/safemode"]

        # Restaring the node with limits off by 1 so no fork satisfies safe mode activation criteria
        with self.run_node_with_connections("Preparation", 0, args_off_by_one,
                                            2) as (conn1, conn2):

            # The node is not in the safe mode, no forks
            self.wait_for_safe_mode_data(conn1.rpc, [], check_webhook_messages=False)
            assert len(self.webhook_messages) == 0 # we are starting without safe mode, the message is not sent

        # Restaring the node with original params, the node should be in the safe mode again
        with self.run_node_with_connections("Preparation", 0, args,
                                            2) as (conn1, conn2):

            # the safe mode is at the same state as before first restart
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     expected_short_fork_data,
                                                     ])

            # We will add three more extensions to the chain
            #=====================================================
            #  ... - mc[N-1] -  mc[N] - mc_extension            sf_extension_2
            #                 \                               /
            #                   sf[0] - sf[1] - ... - sf[N-1] - sf[N]
            #                                                 \
            #                                                   sf_extension

            short_fork_extension = self.make_chain(conn1, short_fork[-2], 1)
            send_by_headers(conn1, short_fork_extension, do_send_blocks=False)

            # when adding a new tip to the short branch we will just add a new tip to an existing fork
            expected_short_fork_data["tips"].add(short_fork_extension[-1].hash)
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     expected_short_fork_data,
                                                     ])

            # ignore tips of the short short branch making it not triggering safe mode any more
            conn1.rpc.ignoresafemodeforblock(short_fork_extension[-1].hash)
            conn1.rpc.ignoresafemodeforblock(short_fork[-1].hash)
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     ])

            # reconsidering previously ignored blocks
            conn1.rpc.reconsidersafemodeforblock(short_fork_extension[-1].hash)
            conn1.rpc.reconsidersafemodeforblock(short_fork[-1].hash)
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     expected_short_fork_data,
                                                     ])

            # ignoring root of the short fork, short fork will not trigger the safe mode.
            conn1.rpc.ignoresafemodeforblock(short_fork[0].hash)
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     ])

            # extend ignored short fork with one more tip, we should ignore this block also because its ancestor is ignored
            short_fork_extension_2 = self.make_chain(conn1, short_fork[-2], 1)
            send_by_headers(conn1, short_fork_extension_2, do_send_blocks=True)
            wait_for_tip_status(conn1, short_fork_extension_2[-1].hash, "headers-only")
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     ])

            # but when it will be reconsidered the new tip should be visible
            expected_short_fork_data["tips"].add(short_fork_extension_2[-1].hash)

            # reconsidering one of the tips of the short fork will revert ignoring of the root block
            conn1.rpc.reconsidersafemodeforblock(short_fork[-1].hash)
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     expected_short_fork_data,
                                                     ])

            main_chain_extension = self.make_chain(conn1, main_chain[-1], 1)
            send_by_headers(conn1, main_chain_extension, do_send_blocks=True)
            # we have extended the main chain so distant fork became too distant and low height for became to low
            # not in the safe mode anymore
            self.wait_for_safe_mode_data(conn1.rpc, [expected_short_fork_data,
                                                     ])

            # we are now invalidating main chain extension so distant and low fork are triggering the safe mode again
            conn1.rpc.invalidateblock(main_chain_extension[0].hash)
            self.wait_for_safe_mode_data(conn1.rpc, [expected_distant_fork_data,
                                                     expected_low_height_difference_fork_data,
                                                     expected_short_fork_data,
                                                     ])
            pass

    def run_test(self):

        self.PORT = 8765
        self.webhook_messages = []
        self.server = HTTPServer(('', self.PORT), self.make_handler)
        self.start_server()

        self.run_rest_case(min_fork_len=3, max_height_difference=-5, max_fork_distance=10)
        self.run_rest_case(min_fork_len=10, max_height_difference=5, max_fork_distance=20)

        self.kill_server()


if __name__ == '__main__':
    SafeMode().main()
