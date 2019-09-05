#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing that we correctly reorg to longer chain even if we are still validating blocks
on lower chains

We have the following case:
     1
   /   \
  2     5
 / \    |
3  4    6
        |
        7

First we make chain 1->2, then we send 3,4 for parallel validation
While validating 3,4 we send competing chain 5->6->7
We should reorg to 7 even if we are still validating 3,4

After we reorged to 7 we finish validations of 3,4 and 7
should still be active
"""
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    p2p_port
)
from bsv_pbv_common import (
    wait_for_waiting_blocks,
    wait_for_validating_blocks
)


class PBVReorg(BitcoinTestFramework):

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

        node1 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node1)
        node1.add_connection(connection)

        node2 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node2)
        node2.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()
        node1.wait_for_verack()
        node2.wait_for_verack()

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

        tip_block_num = block_count-1

        # left branch
        block2 = self.chain.next_block(block_count, spend=out[0], extra_txns=8)
        block2_num = block_count
        block_count += 1
        node0.send_message(msg_block(block2))
        self.log.info(f"block2 hash: {block2.hash}")

        self.nodes[0].waitforblockheight(102)

        # send blocks 3,4 for parallel validation on left branch
        block3 = self.chain.next_block(block_count, spend=out[1], extra_txns=10)
        block_count += 1

        self.chain.set_tip(block2_num)

        block4 = self.chain.next_block(block_count, spend=out[1], extra_txns=8)
        block_count += 1

        # send two "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"block3 hash: {block3.hash}")
        self.nodes[0].waitaftervalidatingblock(block3.hash, "add")
        self.log.info(f"block4 hash: {block4.hash}")
        self.nodes[0].waitaftervalidatingblock(block4.hash, "add")
        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block3.hash, block4.hash}, self.nodes[0], self.log)

        node0.send_message(msg_block(block3))
        node2.send_message(msg_block(block4))

        # make sure we started validating blocks
        wait_for_validating_blocks({block3.hash, block4.hash}, self.nodes[0], self.log)

        # right branch
        self.chain.set_tip(tip_block_num)
        block5 = self.chain.next_block(block_count, spend=out[0], extra_txns=10)
        block_count += 1
        node1.send_message(msg_block(block5))
        self.log.info(f"block5 hash: {block5.hash}")

        # and two blocks to extend second branch to cause reorg
        # - they must be sent from the same node as otherwise they will be
        #   rejected with "prev block not found"
        block6 = self.chain.next_block(block_count)
        node1.send_message(msg_block(block6))
        self.log.info(f"block6 hash: {block6.hash}")
        block_count += 1

        block7 = self.chain.next_block(block_count)
        node1.send_message(msg_block(block7))
        self.log.info(f"block7 hash: {block7.hash}")
        block_count += 1

        self.nodes[0].waitforblockheight(104)
        assert_equal(block7.hash, self.nodes[0].getbestblockhash())

        self.log.info("releasing wait status on parallel blocks to finish their validation")
        self.nodes[0].waitaftervalidatingblock(block3.hash, "remove")
        self.nodes[0].waitaftervalidatingblock(block4.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # block that arrived last on competing chain should be active
        assert_equal(block7.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVReorg().main()
