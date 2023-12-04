#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
The test:
We are validating 3 blocks on current tip, the situation we have is:
    1
 /  |  \
2   3   4

2 and 4 are hard blocks to validate and 3 is easy to validate so 3 should
win the validation race and be set on tip

after validating we call preciousblock on block 4 that should be active
in the end.
"""
from test_framework.blocktools import prepare_init_chain
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


class PBVPreciousBlock(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        block_count = 0

        # Create a P2P connection
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

        _, out, block_count = prepare_init_chain(self.chain, 101, 100, block_0=False, start_block=0, node=node0)

        self.log.info("waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)

        tip_block_num = block_count - 1

        block2_hard = self.chain.next_block(block_count, spend=out[0], extra_txns=8)
        block_count += 1
        self.chain.set_tip(tip_block_num)

        block3_easier = self.chain.next_block(block_count, spend=out[0], extra_txns=2)
        block_count += 1
        self.chain.set_tip(tip_block_num)

        block4_hard = self.chain.next_block(block_count, spend=out[0], extra_txns=10)
        block_count += 1

        # send two "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"hard block2 hash: {block2_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block2_hard.hash, "add")
        self.log.info(f"hard block4 hash: {block4_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block4_hard.hash, "add")
        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block2_hard.hash, block4_hard.hash}, self.nodes[0], self.log)

        node0.send_message(msg_block(block2_hard))
        node1.send_message(msg_block(block4_hard))

        # make sure we started validating blocks
        wait_for_validating_blocks({block2_hard.hash, block4_hard.hash}, self.nodes[0], self.log)

        self.log.info(f"easier hash: {block3_easier.hash}")
        node2.send_message(msg_block(block3_easier))

        self.nodes[0].waitforblockheight(102)
        assert_equal(block3_easier.hash, self.nodes[0].getbestblockhash())

        # now we can remove waiting status from blocks and finish their validation
        self.nodes[0].waitaftervalidatingblock(block2_hard.hash, "remove")
        self.nodes[0].waitaftervalidatingblock(block4_hard.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # now we want our precious block to be one of the harder blocks (block4_hard)
        self.nodes[0].preciousblock(block4_hard.hash)
        assert_equal(block4_hard.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVPreciousBlock().main()
