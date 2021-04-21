#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
The intent of this test is to make sure we can start the node even in case
one of the ancestor blocks in the chain never got block data and was instead
only announced with block header. In such case the child block data can be
accepted before the parent which shouldn't cause any issues during restart.
- Send parent and child block header
- Send only child block's content
- Make sure that parent block data really hasn't made its way to the node
- Restart node
- Node should start without an error
- Send parent block data
- Make sure that child block is now the best block
"""

from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    msg_headers,
    CBlockHeader
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import p2p_port, assert_equal


class PBVSameBlock(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        block_count = 0

        # Create a P2P connections
        node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()

        # send one to get out of IBD state
        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))

        ancestor_block_hash = self.nodes[0].getbestblockhash()

        parent_block = self.chain.next_block(block_count)
        block_count += 1

        headers_message = msg_headers()
        headers_message.headers = [CBlockHeader(parent_block)]
        connection.cb.send_message(headers_message)

        child_block = self.chain.next_block(block_count)
        node0.send_message(msg_block(child_block))

        # wait till validation of block finishes
        node0.sync_with_ping()

        assert_equal(ancestor_block_hash, self.nodes[0].getbestblockhash())

        self.stop_node(0)
        self.start_node(0)

        # Create a P2P connections
        node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()

        assert_equal(ancestor_block_hash, self.nodes[0].getbestblockhash())

        node0.send_message(msg_block(parent_block))

        # wait till validation of block finishes
        node0.sync_with_ping()

        assert_equal(child_block.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVSameBlock().main()
