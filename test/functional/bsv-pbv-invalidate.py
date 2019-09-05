#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
    1
 /    \
6      2
|    / | \
7   3  4  5
|
8

1. First we send 3,4,5 on branch 2 for parallel validation
2. Because 4 is easy to validate it will be validated first and set as active tip
3. After blocks 3,5 are also validated we then send competing chain 6,7,8
   At this point 8 is active.
4. invalidate block 7
Because blocks are ordered by validation completion time block 4 should be active.
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


class PBVInvalidate(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):

        block_count = 0

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

        self.log.info("Sending blocks to get spendable output")
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

        block2 = self.chain.next_block(block_count, spend=out[0], extra_txns=1)
        block2_count = block_count
        block_count += 1
        self.log.info(f"blockA hash: {block2.hash}")
        node0.send_message(msg_block(block2))
        self.nodes[0].waitforblockheight(102)

        block3_hard = self.chain.next_block(block_count, spend=out[1], extra_txns=8)
        block_count += 1
        self.chain.set_tip(block2_count)

        block4_easier = self.chain.next_block(block_count, spend=out[1], extra_txns=2)
        block_count += 1
        self.chain.set_tip(block2_count)

        block5_hard = self.chain.next_block(block_count, spend=out[1], extra_txns=10)
        block_count += 1

        # send two "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"hard block3 hash: {block3_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block3_hard.hash, "add")
        self.log.info(f"hard block5 hash: {block5_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block5_hard.hash, "add")
        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block3_hard.hash, block5_hard.hash}, self.nodes[0], self.log)

        self.log.info("Sending blocks 3,4,5 on branch 2 for parallel validation")
        node0.send_message(msg_block(block3_hard))
        node2.send_message(msg_block(block5_hard))

        # make sure we started validating blocks
        wait_for_validating_blocks({block3_hard.hash, block5_hard.hash}, self.nodes[0], self.log)

        self.log.info(f"easier hash: {block4_easier.hash}")
        node1.send_message(msg_block(block4_easier))
        self.nodes[0].waitforblockheight(103)

        # Because 4 is easy to validate it will be validated first and set as active tip
        assert_equal(block4_easier.hash, self.nodes[0].getbestblockhash())

        # now we can remove waiting status from blocks and finish their validation
        self.nodes[0].waitaftervalidatingblock(block3_hard.hash, "remove")
        self.nodes[0].waitaftervalidatingblock(block5_hard.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # easier block should still be on tip
        assert_equal(block4_easier.hash, self.nodes[0].getbestblockhash())

        self.log.info("Sending blocks 6,7,8 on competing chain to cause reorg")
        self.chain.set_tip(tip_block_num)

        block6 = self.chain.next_block(block_count, spend=out[0], extra_txns=2)
        block_count += 1
        self.log.info(f"block6 hash: {block6.hash}")
        node0.send_message(msg_block(block6))

        block7 = self.chain.next_block(block_count)
        block_count += 1
        self.log.info(f"block7: {block7.hash}")
        node0.send_message(msg_block(block7))

        # send one to cause reorg this should be active
        block8 = self.chain.next_block(block_count)
        block_count += 1
        self.log.info(f"block8: {block8.hash}")
        node0.send_message(msg_block(block8))

        self.nodes[0].waitforblockheight(104)
        assert_equal(block8.hash, self.nodes[0].getbestblockhash())

        self.log.info("Invalidating block7 on competing chain to reorg to first branch again")
        self.log.info(f"invalidating hash {block7.hash}")
        self.nodes[0].invalidateblock(block7.hash)

        #after invalidating, active block should be the one first validated on first branch
        assert_equal(block4_easier.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVInvalidate().main()
