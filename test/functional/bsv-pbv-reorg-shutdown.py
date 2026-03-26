#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing that we correctly return transactions that were taken from blocks during
reorg on shutdown.

We have the following case:
     1
   /   \
  2     3
        |
        4


1. First we make chain 1->2 and make 2 as the best tip
2. Then we send 3 and 4 for reorg but block 3 (block 3 doesn't contain
   transaction from block 2 that will be returned to mempool).
3. We call shutdown node.

Bitcoind shuoud gracefully shutdown.
"""
from test_framework.mininode import (
    P2PHandler,
    msg_block
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import p2p_port
from bsv_pbv_common import (
    wait_for_waiting_blocks,
    wait_for_validating_blocks
)


class PBVReorgShutdown(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        block_count = 0

        # Create P2P connections
        node0, node1 = P2PHandler.connect_multiple([
            ('127.0.0.1', p2p_port(0), self.nodes[0]),
            ('127.0.0.1', p2p_port(0), self.nodes[0]),
        ])

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(block_count)
        block_count += 1
        self.chain.save_spendable_output()
        node0.send_message(msg_block(block))

        for i in range(100):
            block = self.chain.next_block(block_count)
            block_count += 1
            self.chain.save_spendable_output()
            node0.send_message(msg_block(block))

        out = []
        for i in range(100):
            out.append(self.chain.get_spendable_output())

        self.log.info("waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)

        tip_block_num = block_count - 1

        # left branch
        block2 = self.chain.next_block(block_count, spend=out[0], extra_txns=8)
        block_count += 1
        node0.send_message(msg_block(block2))
        self.log.info(f"block2 hash: {block2.hash}")

        self.nodes[0].waitforblockheight(102)

        # send blocks 3,4 to force a reorg
        self.chain.set_tip(tip_block_num)
        block3 = self.chain.next_block(block_count, spend=out[1], extra_txns=10)
        block_count += 1
        block4 = self.chain.next_block(block_count, spend=out[1], extra_txns=8)
        block_count += 1

        self.log.info(f"block3 hash: {block3.hash}")
        self.nodes[0].waitaftervalidatingblock(block3.hash, "add")

        # make sure block hash is in waiting list
        wait_for_waiting_blocks({block3.hash}, self.nodes[0], self.log)

        node0.send_message(msg_block(block3))
        node0.send_message(msg_block(block4))

        # make sure we started validating blocks
        wait_for_validating_blocks({block3.hash}, self.nodes[0], self.log)

        self.stop_node(0)


if __name__ == '__main__':
    PBVReorgShutdown().main()
