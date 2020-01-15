#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
First valid active in this sense means the block that validated first is
set as new active tip

    1
 /  |  \
2   3   4
    |
    5
 /  |  \
6   7   8

1. we send 2,3,4. Blocks 2 and 4 are hard to validate and 3 is easy to validate so 3 should
   win the validation race and be set on tip.
   Blocks 2,3,4 are sent via diffrent p2p node connection
2. we send block 5 and make sure it becomes active tip
   even if some other block (block4) is still validating
3. we send 6,8 that are hard to validate and 7 that is easy to validate.
   6,7,8 are sent via same p2p node connection. 7 should be active tip in the end
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
    wait_for_validating_blocks,
    wait_for_not_validating_blocks
)


class PBVFirstValidActive(BitcoinTestFramework):

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

        block2_hard = self.chain.next_block(block_count, spend=out[0], extra_txns=8)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block3_easier = self.chain.next_block(block_count, spend=out[0], extra_txns=2)
        easier_block_num = block_count
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block4_hard = self.chain.next_block(block_count, spend=out[0], extra_txns=10)
        block_count += 1

        # make child block of easier block
        self.chain.set_tip(easier_block_num)
        block5 = self.chain.next_block(block_count)
        block5_num = block_count
        block_count += 1

        # send two "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"hard block2 hash: {block2_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block2_hard.hash, "add")
        self.log.info(f"hard block4 hash: {block4_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block4_hard.hash, "add")

        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block2_hard.hash, block4_hard.hash}, self.nodes[0], self.log)

        # send blocks via different p2p connection
        node0.send_message(msg_block(block2_hard))
        node1.send_message(msg_block(block4_hard))

        # make sure we started validating blocks
        wait_for_validating_blocks({block2_hard.hash, block4_hard.hash}, self.nodes[0], self.log)

        # send easier block through different p2p connection too
        node2.send_message(msg_block(block3_easier))
        self.log.info(f"easier block hash: {block3_easier.hash}")
        self.nodes[0].waitforblockheight(102)
        assert_equal(block3_easier.hash, self.nodes[0].getbestblockhash())

        # child block of block3_easier
        self.log.info(f"child block hash: {block5.hash}")
        self.nodes[0].waitaftervalidatingblock(block5.hash, "add")

        # make sure child block is in waiting list and then send it
        wait_for_not_validating_blocks({block5.hash}, self.nodes[0], self.log)
        node2.send_message(msg_block(block5))

        # make sure we started validating child block
        wait_for_validating_blocks({block5.hash}, self.nodes[0], self.log)

        # finish validation on block2_hard
        self.nodes[0].waitaftervalidatingblock(block2_hard.hash, "remove")
        wait_for_not_validating_blocks({block2_hard.hash}, self.nodes[0], self.log)

        # finish validation on child block
        self.nodes[0].waitaftervalidatingblock(block5.hash, "remove")
        wait_for_not_validating_blocks({block5.hash}, self.nodes[0], self.log)

        # block5 should be active at this point
        assert_equal(block5.hash, self.nodes[0].getbestblockhash())

        # finish validation on block4_hard
        self.nodes[0].waitaftervalidatingblock(block4_hard.hash, "remove")
        wait_for_not_validating_blocks({block4_hard.hash}, self.nodes[0], self.log)

        # block5 should still be active at this point
        assert_equal(block5.hash, self.nodes[0].getbestblockhash())

        # Make three siblings and send them via same p2p connection.
        block6_hard = self.chain.next_block(block_count, spend=out[1], extra_txns=8)
        block_count += 1

        self.chain.set_tip(block5_num)

        block7_easier = self.chain.next_block(block_count, spend=out[1], extra_txns=2)
        block_count += 1

        self.chain.set_tip(block5_num)

        block8_hard = self.chain.next_block(block_count, spend=out[1], extra_txns=10)
        block_count += 1

        self.log.info(f"hard block6 hash: {block6_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block6_hard.hash, "add")
        self.log.info(f"hard block8 hash: {block8_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block8_hard.hash, "add")
        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block6_hard.hash, block8_hard.hash}, self.nodes[0], self.log)

        # sending blocks via same p2p connection
        node0.send_message(msg_block(block6_hard))
        node0.send_message(msg_block(block8_hard))

        # make sure we started validating blocks
        wait_for_validating_blocks({block6_hard.hash, block8_hard.hash}, self.nodes[0], self.log)

        # send easier block through same p2p connection too
        node0.send_message(msg_block(block7_easier))

        self.nodes[0].waitforblockheight(104)
        assert_equal(block7_easier.hash, self.nodes[0].getbestblockhash())

        # now we can remove waiting status from blocks and finish their validation
        self.nodes[0].waitaftervalidatingblock(block6_hard.hash, "remove")
        self.nodes[0].waitaftervalidatingblock(block8_hard.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # easier block should still be on tip
        assert_equal(block7_easier.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVFirstValidActive().main()
