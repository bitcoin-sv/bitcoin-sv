#!/usr/bin/env python3
# Copyright (c) 2024 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.


'''
Test node handling for a peer that floods it with small msgs while not
reading our responses.
'''

from test_framework.mininode import NodeConn, NodeConnCB, NetworkThread, mininode_lock, READ_BUFFER_SIZE, ser_uint256
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import p2p_port, wait_until, check_for_log_msg

import threading


# A badly formatted msg that will cause a parsing failure and a reject reply
class msg_badblocktxn():
    command = b"blocktxn"
    block_hash = ser_uint256(0)

    def serialize(self):
        r = b""
        # Good block hash
        r += self.block_hash
        # Txn list length 0x3a, then just 4 0x00 bytes
        r += bytes([0x3a, 0x00, 0x00, 0x00, 0x00])
        return r


class TestConn(NodeConn):

    def __init__(self, dstaddr, dstport, rpc, callback):
        super().__init__(dstaddr, dstport, rpc, callback)
        self.do_reading = True

    def drain_buffer(self):
        with mininode_lock:
            try:
                while True:
                    _ = self.recv(READ_BUFFER_SIZE)
            except Exception:
                pass

            self.do_reading = True

    # Override read method so we can ignore incoming responses
    def handle_read(self):
        if self.do_reading:
            super().handle_read()


class PeerFlood(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-maxreceivebuffer=10KB", "-maxsendbuffer=10KB"]]

    def run_test(self):

        # Get out of IBD
        self.nodes[0].generate(1)

        # Create P2P connections to node
        test_node = NodeConnCB()
        connection = TestConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node)
        test_node.add_connection(connection)

        # Start up network handling in another thread
        NetworkThread().start()
        test_node.wait_for_verack()

        # Tell connection to start ignoring incoming data
        connection.do_reading = False

        # Send message flood to node in another thread
        bad_msg = msg_badblocktxn()
        stop_sending = threading.Event()

        def send_msg():
            while not stop_sending.is_set():
                test_node.send_message(bad_msg)

        send_thread = threading.Thread(target=send_msg)
        send_thread.start()

        # Node will eventually fill both send and recv buffers
        wait_until(lambda: check_for_log_msg(self, "Dropping reject message because we're paused for send and receive", "/node0"))
        stop_sending.set()
        send_thread.join()

        # Once we stop flooding and if we re-start reading, node will clear its backlog
        connection.drain_buffer()

        def send_recv_done():
            pi0 = self.nodes[0].getpeerinfo()[0]
            sendsize = pi0['sendsize']
            recvsize = pi0['recvsize']
            return sendsize == 0 and recvsize == 0
        wait_until(send_recv_done, timeout=120)


if __name__ == '__main__':
    PeerFlood().main()
