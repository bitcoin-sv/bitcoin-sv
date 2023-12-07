#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Check the -maxblocktxnpercent parameter correctly limits blocktxn responses.

1) Make a block with 1000 txns.
2) Verify if we ask for < 98% of txns via getblocktxn we get a blocktxn response.
3) Verify if we ask for > 98% of txns via getblocktxn we get a plain block response.
"""

from test_framework.blocktools import prepare_init_chain
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.mininode import BlockTransactionsRequest, NodeConn, NodeConnCB, msg_getblocktxn, mininode_lock
from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import wait_until, p2p_port

import random


# TestNode: A peer we use to send messages to bitcoind, and store responses.
class TestNode(NodeConnCB):

    def __init__(self):
        self.last_block = None
        self.last_blocktxn = None
        super().__init__()

    def on_blocktxn(self, conn, message):
        super().on_blocktxn(conn, message)
        self.last_blocktxn = message

    def on_block(self, conn, message):
        super().on_block(conn, message)
        self.last_block = message

    def clear_block_data(self):
        with mininode_lock:
            self.last_block = None
            self.last_blocktxn = None


class MaxBlockTxn(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-whitelist=127.0.0.1', '-maxblocktxnpercent=98']]

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # Out of IBD
        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block
        block(0)
        yield self.accepted()

        # Get spendable output
        test, out, _ = prepare_init_chain(self.chain, 99, 100)
        yield test

        # Wait for connection to be etablished
        node = self.nodes[0]
        peer = TestNode()
        peer.add_connection(NodeConn('127.0.0.1', p2p_port(0), node, peer))
        peer.wait_for_verack()

        # Send a large block with numerous transactions.
        b1 = block(1, spend=out[0], extra_txns=1000, block_size=ONE_MEGABYTE + 1)
        yield self.accepted()

        # Wait for block message for above block to arrive
        wait_until(lambda: peer.last_block != None)
        assert(peer.last_block.block.sha256 == b1.sha256)
        peer.clear_block_data()

        # Request last block via sparse getblocktxn
        msg = msg_getblocktxn()
        msg.block_txn_request = BlockTransactionsRequest(b1.sha256, [])
        msg.block_txn_request.from_absolute(sorted(random.sample(range(len(b1.vtx)), int(len(b1.vtx) / 2))))
        peer.send_message(msg)
        wait_until(lambda: peer.last_blocktxn != None)
        assert(peer.last_blocktxn.block_transactions.blockhash == b1.sha256)
        peer.clear_block_data()

        # Request last block via full getblocktxn
        msg = msg_getblocktxn()
        msg.block_txn_request = BlockTransactionsRequest(b1.sha256, [])
        msg.block_txn_request.from_absolute(range(len(b1.vtx)))
        peer.send_message(msg)
        wait_until(lambda: peer.last_block != None)
        assert(peer.last_block.block.sha256 == b1.sha256)
        peer.clear_block_data()


if __name__ == '__main__':
    MaxBlockTxn().main()
