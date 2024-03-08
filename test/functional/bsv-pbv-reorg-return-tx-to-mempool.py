#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing that we correctly return transactions that were taken from blocks during
reorg on loosing branch.

We have the following case:
   1
 / | \
2  3  5
   |  |
   4  6
      |
      7


1. First we make chain 1->2 and make 2 as the best tip.
2. Then we send 3->4 for reorg but block 4 to simulate long validation.
3. While validating 3,4 we send empty competing chain 5->6->7 and let it win.
4. We release block 3 so it notices that the tip has changed and doesn't change
   the tip.

At the end the tip should be on block 7, and mempool shoud contain transactions
from block 2.
"""
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block
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

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()
        node1.wait_for_verack()

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(block_count)
        block_count += 1
        self.chain.save_spendable_output()
        node0.send_message(msg_block(block))

        num_blocks = 150
        for i in range(num_blocks):
            block = self.chain.next_block(block_count)
            block_count += 1
            self.chain.save_spendable_output()
            node0.send_message(msg_block(block))

        out = []
        for i in range(num_blocks):
            out.append(self.chain.get_spendable_output())

        self.log.info("waiting for block height 151 via rpc")
        self.nodes[0].waitforblockheight(num_blocks + 1)

        tip_block_num = block_count-1

        # left branch
        block2 = self.chain.next_block(block_count, spend=out[0:9], extra_txns=8)
        block_count += 1
        node0.send_message(msg_block(block2))
        self.log.info(f"block2 hash: {block2.hash}")

        self.nodes[0].waitforblockheight(num_blocks + 2)

        # send blocks 3,4 for parallel validation on left branch
        self.chain.set_tip(tip_block_num)
        block3 = self.chain.next_block(block_count, spend=out[10:19], extra_txns=10)
        block_count += 1

        block4 = self.chain.next_block(block_count, spend=out[20:29], extra_txns=8)
        block_count += 1

        # send two "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"block3 hash: {block3.hash}")
        self.log.info(f"block4 hash: {block4.hash}")
        self.nodes[0].waitaftervalidatingblock(block4.hash, "add")

        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block4.hash}, self.nodes[0], self.log)

        node1.send_message(msg_block(block3))
        node1.send_message(msg_block(block4))

        # make sure we started validating blocks
        wait_for_validating_blocks({block4.hash}, self.nodes[0], self.log)

        # right branch
        self.chain.set_tip(tip_block_num)
        block5 = self.chain.next_block(block_count)
        # Add some txns from block2 & block3 to block5, just to check that they get
        # filtered from the mempool and not re-added
        block5_duplicated_txns = block3.vtx[1:3] + block2.vtx[1:3]
        self.chain.update_block(block_count, block5_duplicated_txns)
        block_count += 1
        node0.send_message(msg_block(block5))
        self.log.info(f"block5 hash: {block5.hash}")

        # and two blocks to extend second branch to cause reorg
        # - they must be sent from the same node as otherwise they will be
        #   rejected with "prev block not found" as we don't wait for the first
        #   block to arrive so there is a race condition which block is seen
        #   first when using multiple connections
        block6 = self.chain.next_block(block_count)
        node0.send_message(msg_block(block6))
        self.log.info(f"block6 hash: {block6.hash}")
        block_count += 1
        block7 = self.chain.next_block(block_count)
        node0.send_message(msg_block(block7))
        self.log.info(f"block7 hash: {block7.hash}")
        block_count += 1

        self.nodes[0].waitforblockheight(num_blocks + 4)
        assert_equal(block7.hash, self.nodes[0].getbestblockhash())

        self.log.info("releasing wait status on parallel blocks to finish their validation")
        self.nodes[0].waitaftervalidatingblock(block4.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # block that arrived last on competing chain should be active
        assert_equal(block7.hash, self.nodes[0].getbestblockhash())

        # make sure that transactions from block2 and 3 (except coinbase, and those also
        # in block 5) are in mempool
        not_expected_in_mempool = set()
        for txn in block5_duplicated_txns:
            not_expected_in_mempool.add(txn.hash)
        expected_in_mempool = set()
        for txn in block2.vtx[1:] + block3.vtx[1:]:
            expected_in_mempool.add(txn.hash)
        expected_in_mempool = expected_in_mempool.difference(not_expected_in_mempool)

        mempool = self.nodes[0].getrawmempool()
        assert_equal(expected_in_mempool, set(mempool))


if __name__ == '__main__':
    PBVReorg().main()
