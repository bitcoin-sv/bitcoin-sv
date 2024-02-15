#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Check detection of too many pending responses for getheaders/gethdrsen requests

from test_framework.mininode import NodeConnCB, mininode_lock
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, count_log_msg, wait_until
import types


class NodeConnCBWithHeadersCount(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.num_headers_msgs_received = 0

    def on_headers(self, conn, message):
        self.num_headers_msgs_received = self.num_headers_msgs_received + 1

    def on_hdrsen(self, conn, message):
        self.num_headers_msgs_received = self.num_headers_msgs_received + 1


class P2PPendingResponses(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def send_requests_without_reading_responses(self, node_args, request_command, num_requests, expect_disconnect):
        # Helper to create GETHEADERS/GETHDRSEN P2P message with locator which specifies a block that is not on node's active chain.
        # After sending this message to the node, node will respond with HEADERS/HDRSEN P2P message that contains up to 2000 headers from the start of the active chain.
        class msg_raw():
            def __init__(self, request_command):
                self.command = request_command.encode("ascii")

            def serialize(self):
                return b'\xCF\x07\x81\x11\x01' + b'\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11' +b'\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11\x11'

        with self.run_node_with_connections("P2PPendingResponses", node_index=0, cb_class=NodeConnCBWithHeadersCount, args=node_args, number_of_connections=1) as conns:
            conn = conns[0]
            log_msg_request_received = f"received: {request_command}"

            if not expect_disconnect:
                # Remember how many messages were already in bitcoind's log file so that we can later
                # check how many were added during the test.
                initial_request_cnt = count_log_msg(conn.rpc, log_msg_request_received)

            self.log.info("Disable reading from socket in mininode")
            conn.orig_conn_readable = conn.readable
            def conn_readable_false(self): return False
            conn.readable = types.MethodType(conn_readable_false, conn)

            num_sent_before_disconnect = 0
            self.log.info(f"Sending {num_requests} {request_command} requests, expect_disconnect: {expect_disconnect}")
            for i in range(num_requests):
                try:
                    conn.send_message(msg_raw(request_command))
                    num_sent_before_disconnect = num_sent_before_disconnect + 1
                except:
                    # If send_message() throws, connection was already closed.
                    # NOTE: This is unlikely to happen because requests can be sent much faster than the node can process them
                    #       and will be stored in received buffer until then.
                    break

            if expect_disconnect:
                # We should be able to send some requests.
                # NOTE: If node processes requests slow enough, we were likely able to send all of them and
                #       requests were waiting in the node's receive queue until disconnect.
                assert num_sent_before_disconnect > 0
                # Wait until node closes the connection
                # NOTE: We cannot wait for disconnect in mininode because that would require enabling reading from socket and if we do that, disconnect may not happen.
                wait_until(lambda: len(conn.rpc.getpeerinfo())==0, check_interval=1)
            else:
                # If disconnect is not expected, we should be able to send all requests even without reading the responses
                assert num_sent_before_disconnect == num_requests
                self.log.info("Waiting until node receives all requests")
                wait_until(lambda: count_log_msg(conn.rpc, log_msg_request_received) == initial_request_cnt+num_requests, check_interval=1)
                # Check that connection was not closed
                assert_equal(len(conn.rpc.getpeerinfo()), 1)

            self.log.info("Enable reading from socket in mininode")
            conn.readable = conn.orig_conn_readable

            if expect_disconnect:
                # NOTE: mininode will only report disconnect if reading is enabled
                conn.cb.wait_for_disconnect()
            else:
                # If no disconnect is expected, we should receive all responses
                self.log.info(f"Waiting until {num_requests} responses are received")
                wait_until(lambda: conn.cb.num_headers_msgs_received == num_requests, check_interval=1, lock=mininode_lock)

    def run_test(self):
        # Mine some blocks so that HEADERS response returned by GETHEADERS will be as large as possible.
        # NOTE: This is needed because number of pending responses will only start increasing after TCP SNDBUF
        #       on the node's socket is full. This means the test needs to send more requests than specified in
        #       node's config to trigger the disconnect. The larger the response, the sooner TCP SNDBUF will be
        #       filled which will minimize the number of additional requests that need to be sent.
        self.log.info("Generating 2000 blocks")
        self.nodes[0].rpc.generate(2000)
        self.stop_node(0)

        self.log.info("---Part 1: Check that maximum pending responses for getheaders/gethdrsen is detected when enforcing is enabled")
        # NOTE: We need to send more than maximum allowed requests to actually trigger the disconnect because
        #       some responses will be removed from node's sending queue and will wait in TCP SNDBUF.
        #       If the size of TCP SNDBUF used by node is increased, number of requests used here should be increased as well.
        self.send_requests_without_reading_responses(["-maxpendingresponses_getheaders=50"], "getheaders", 100, True)
        self.send_requests_without_reading_responses(["-maxpendingresponses_gethdrsen=10"], "gethdrsen", 25, True)

        self.log.info("--- Part 2: Check that maximum number pending responses getheaders/gethdrsen can be sent even when enforcing is enabled")
        self.send_requests_without_reading_responses(["-maxpendingresponses_getheaders=50"], "getheaders", 50, False)
        self.send_requests_without_reading_responses(["-maxpendingresponses_gethdrsen=10"], "gethdrsen", 10, False)

        self.log.info("--- Part 3: Check that maximum pending responses for getheaders/gethdrsen is not enforced by default")
        self.send_requests_without_reading_responses([], "getheaders", 100, False)
        self.send_requests_without_reading_responses([], "gethdrsen", 25, False)

        self.log.info("--- Part 4: Check that maximum pending responses for getheaders/gethdrsen is not enforced for whitelisted peers")
        self.send_requests_without_reading_responses(["-whitelist=127.0.0.1","-maxpendingresponses_getheaders=50"], "getheaders", 100, False)
        self.send_requests_without_reading_responses(["-whitelist=127.0.0.1","-maxpendingresponses_gethdrsen=10"], "gethdrsen", 25, False)


if __name__ == '__main__':
    P2PPendingResponses().main()
