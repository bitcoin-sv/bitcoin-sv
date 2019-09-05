#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing RPC functionality with parallel block validation
methods we are testing are:
    getcurrentlyvalidatingblocks
    waitaftervalidatingblock
    getwaitingblocks

The test:
We are validating 3 blocks on current tip, the situation we have is:
    1
 /  |  \
2   3   4

2 and 4 are hard blocks to validate and 3 is easy to validate so 3 should
win the validation race and be set on tip

We are artificially extending validation time by calling waitaftervalidatingblock
"""
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import p2p_port, assert_equal, wait_until


class PBVWaitAfterValidation(BitcoinTestFramework):

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

        tip_block_num = block_count - 1

        # adding extra transactions to get different block hashes
        block_hard1 = self.chain.next_block(block_count, spend=out[0], extra_txns=8)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        easier = self.chain.next_block(block_count, spend=out[0], extra_txns=2)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block_hard2 = self.chain.next_block(block_count, spend=out[0], extra_txns=10)
        block_count += 1

        # send two "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"hard block1 hash: {block_hard1.hash}")
        self.nodes[0].waitaftervalidatingblock(block_hard1.hash, "add")
        self.log.info(f"hard block2 hash: {block_hard2.hash}")
        self.nodes[0].waitaftervalidatingblock(block_hard2.hash, "add")
        # make sure block hashes are in waiting list
        def wait_for_hard1_in_waiting_list():
            waiting_hashes = self.nodes[0].getwaitingblocks()
            return block_hard1.hash in waiting_hashes and block_hard2.hash in waiting_hashes
        wait_until(wait_for_hard1_in_waiting_list)

        node0.send_message(msg_block(block_hard1))
        node1.send_message(msg_block(block_hard2))

        # make sure we started validating blocks
        oldArray = []
        def wait_for_hard1_and_2():
            nonlocal oldArray
            validating_blocks = self.nodes[0].getcurrentlyvalidatingblocks()
            if oldArray != validating_blocks:
                self.log.info("currently validating blocks: " + str(validating_blocks))
            oldArray = validating_blocks
            return block_hard1.hash in validating_blocks and block_hard2.hash in validating_blocks
        wait_until(wait_for_hard1_and_2)

        self.log.info(f"easier hash: {easier.hash}")
        node2.send_message(msg_block(easier))

        self.nodes[0].waitforblockheight(102)
        assert_equal(easier.hash, self.nodes[0].getbestblockhash())

        # now we can remove waiting status from blocks and finish their validation
        self.nodes[0].waitaftervalidatingblock(block_hard1.hash, "remove")
        self.nodes[0].waitaftervalidatingblock(block_hard2.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # easier block should still be on tip
        assert_equal(easier.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVWaitAfterValidation().main()
