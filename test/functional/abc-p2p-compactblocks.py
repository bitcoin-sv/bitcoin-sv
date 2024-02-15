#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
This test checks simple acceptance of bigger blocks via p2p.
It is derived from the much more complex p2p-fullblocktest.
The intention is that small tests can be derived from this one, or
this one can be extended, to cover the checks done for bigger blocks
(e.g. sigops limits).
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import *
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
import time
from test_framework.script import *


# TestNode: A peer we use to send messages to bitcoind, and store responses.
class TestNode(NodeConnCB):

    def __init__(self):
        self.last_sendcmpct = None
        self.last_cmpctblock = None
        self.last_getheaders = None
        self.last_headers = None
        super().__init__()

    def on_sendcmpct(self, conn, message):
        self.last_sendcmpct = message

    def on_cmpctblock(self, conn, message):
        self.last_cmpctblock = message
        self.last_cmpctblock.header_and_shortids.header.calc_sha256()

    def on_getheaders(self, conn, message):
        self.last_getheaders = message

    def on_headers(self, conn, message):
        self.last_headers = message
        for x in self.last_headers.headers:
            x.calc_sha256()

    def clear_block_data(self):
        with mininode_lock:
            self.last_sendcmpct = None
            self.last_cmpctblock = None


class FullBlockTest(ComparisonTestFramework):

    # Can either run this test as 1 node with expected answers, or two and compare them.
    # Change the "outcome" variable from each TestInstance object to only do
    # the comparison.

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # excessive_block_size needs to be > generated block size
        self.excessive_block_size = 64 * ONE_MEGABYTE
        self.extra_args = [['-minrelaytxfee=0',
                            '-whitelist=127.0.0.1',
                            '-limitancestorcount=999999',
                            '-maxmempool=99999',
                            '-maxmempoolsizedisk=0',
                            "-excessiveblocksize=%d"
                            % self.excessive_block_size]]

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option(
            "--runbarelyexpensive", dest="runbarelyexpensive", default=True)

    def add_node(self, i, extra_args, rpchost=None, timewait=None, binary=None, init_data_dir=False):
        # RPC timeout needs to be high because in debug build invalidateblock can take >90s to complete
        timewait=150
        return super().add_node(i, extra_args, rpchost, timewait, binary, init_data_dir)

    def run_test(self):
        self.nodes[0].setexcessiveblock(self.excessive_block_size)
        self.test.run()

    def get_tests(self):
        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))

        # shorthand for functions
        block = self.chain.next_block

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 99, 100)

        yield test

        # Check that compact block also work for big blocks
        node = self.nodes[0]
        peer = TestNode()
        peer.add_connection(NodeConn('127.0.0.1', p2p_port(0), node, peer))

        # Wait for connection to be etablished
        peer.wait_for_verack()

        # Wait for SENDCMPCT
        def received_sendcmpct():
            return (peer.last_sendcmpct != None)
        wait_until(received_sendcmpct, timeout=30)

        sendcmpct = msg_sendcmpct()
        sendcmpct.version = 1
        sendcmpct.announce = True
        peer.send_and_ping(sendcmpct)

        # Exchange headers
        def received_getheaders():
            return (peer.last_getheaders != None)
        wait_until(received_getheaders, timeout=30)

        # Return the favor
        peer.send_message(peer.last_getheaders)

        # Wait for the header list
        def received_headers():
            return (peer.last_headers != None)
        wait_until(received_headers, timeout=30)

        # It's like we know about the same headers !
        peer.send_message(peer.last_headers)

        # Send a block
        b1 = block(1, spend=out[0], block_size=ONE_MEGABYTE + 1)
        yield self.accepted()

        # Checks the node to forward it via compact block
        def received_block():
            return (peer.last_cmpctblock != None)
        wait_until(received_block, timeout=30)

        # Was it our block ?
        cmpctblk_header = peer.last_cmpctblock.header_and_shortids.header
        cmpctblk_header.calc_sha256()
        assert(cmpctblk_header.sha256 == b1.sha256)

        # Send a large block with numerous transactions.
        peer.clear_block_data()
        b2 = block(2, spend=out[1], extra_txns=70000,
                   block_size=self.excessive_block_size - 1000)
        yield self.accepted()

        # Checks the node forwards it via compact block
        wait_until(received_block, timeout=30)

        # Was it our block ?
        cmpctblk_header = peer.last_cmpctblock.header_and_shortids.header
        cmpctblk_header.calc_sha256()
        assert(cmpctblk_header.sha256 == b2.sha256)

        # In order to avoid having to resend a ton of transactions, we invalidate
        # b2, which will send all its transactions in the mempool.
        node.invalidateblock(node.getbestblockhash())

        # Let's send a compact block and see if the node accepts it.
        # Let's modify b2 and use it so that we can reuse the mempool.
        tx = b2.vtx[0]
        tx.vout.append(CTxOut(0, CScript([random.randint(0, 256), OP_RETURN])))
        tx.rehash()
        b2.vtx[0] = tx
        b2.hashMerkleRoot = b2.calc_merkle_root()
        b2.solve()

        # Now we create the compact block and send it
        comp_block = HeaderAndShortIDs()
        comp_block.initialize_from_block(b2)
        peer.send_and_ping(msg_cmpctblock(comp_block.to_p2p()))

        # Check that compact block is received properly
        assert(int(node.getbestblockhash(), 16) == b2.sha256)


if __name__ == '__main__':
    FullBlockTest().main()
