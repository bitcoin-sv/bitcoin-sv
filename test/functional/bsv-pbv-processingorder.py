#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing that we correctly accept blocks even if due to race condition child
block arrives to best chain activation stage before parent.

We have the following case:
1
|
2

First we send block 1 but block it during validation, then we
send block 2 and after a short while resume block 1. Block 2 should be the best
tip in the end.

What happens:
1. block 1 is added to validation blocking list
2. block 1 is received
3. cs_main lock is obtained, block 1 is added to block index pool (mapBlockIndex),
   stored to disk and
4. cs_main is released
*-1
5. block 2 is received
6. cs_main lock is obtained, block 2 is added to block index pool (mapBlockIndex),
   stored to disk and
*-2
7. both blocks are in validation stage and one of the validations is canceled
   as the other is already running (there can't be more than one block from a
   group in validation if that group consists of block that are descendants of
   one another) - which validation stage is rejected depends on when the race
   condition occurs (locations marked with a *-number)

   both blocks arrive to ActivateBestChain() function and activation of one
   block is canceled as the other is already running.
   Reason: If two blocks share common ancestor (paths towards those two blocks
   contain common blocks) we prevent validation of both as we don't want to
   validate a single block twice and compete between themselves for the common
   goal.
   Which validation paths is rejected depends on when the race condition occurs
   (locations marked with a *-number).
   The one ActivateBestChain() call that remains will continue until there are
   no more better chain candidates so even if block 1 starts the validation first
   and block 2 activation is canceled block 2 will be added by block 1
   ActivateBestChain() call.
8. block 1 is removed from validation blocking list
9. both blocks are now part of the active chain and a log entry for the 7. exists

Race condition can occur anywhere where cs_main is released before the end of
block processing but if that case doesn't manifest itself on its own we make
sure it occurs deterministically by blocking the validation of the block 1 as
even if block 2 gets to validation stage first it still has to validate block 1
since in that case the parent (block 1) has not been validated yet - this way
we certainly get an attempt of trying to validate block 1 by two checker queues
at the same time.
"""
from test_framework.blocktools import (create_block, create_coinbase)
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    p2p_port,
    wait_until
)
from bsv_pbv_common import wait_for_waiting_blocks
from test_framework.script import *
from test_framework.blocktools import create_transaction
from test_framework.key import CECKey
import glob


class PBVProcessingOrder(BitcoinTestFramework):

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

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        block1 = self.chain.next_block(block_count, spend=out[0], extra_txns=8)
        block_count += 1
        # send block but block him at validation point
        self.nodes[0].waitaftervalidatingblock(block1.hash, "add")
        node0.send_message(msg_block(block1))
        self.log.info(f"block1 hash: {block1.hash}")

        # make sure block hash is in waiting list
        wait_for_waiting_blocks({block1.hash}, self.nodes[0], self.log)

        # send child block
        block2 = self.chain.next_block(block_count, spend=out[1], extra_txns=10)
        block_count += 1
        node0.send_message(msg_block(block2))
        self.log.info(f"block2 hash: {block2.hash}")

        def wait_for_log():
            line_text = block2.hash + " will not be considered by the current"
            for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
                if line_text in line:
                    self.log.info("Found line: %s", line)
                    return True
            return False
        wait_until(wait_for_log)

        self.nodes[0].waitaftervalidatingblock(block1.hash, "remove")

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # block that arrived last on competing chain should be active
        assert_equal(block2.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVProcessingOrder().main()
