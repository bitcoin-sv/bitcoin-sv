#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
We will test the following situation where block 1 is the tip and three blocks
are sent for parallel validation:
    1
 /  |  \
2   3   4
Blocks 2,4 are hard to validate and block 3 is easy to validate.
- Blocks 2,3 are sent via p2p.
- Block 4 is submitted via rpc command submitblock.
Block 3 should be active in the end because it was easiest to validate and
therefore won the validation race.

*This test is similar to bsv-pbv-submitminingsolution.py which uses different RPC call to
 submit the block.

Additionally this test also checks that blocks with same height but later arrival
are also announced to the network after being validated. (lines marked with ***
at the beginning of comments)
"""
import threading

from test_framework.blocktools import prepare_init_chain
from test_framework.util import (
    assert_equal,
    p2p_port,
    get_rpc_proxy,
    rpc_url,
    get_datadir_path,
    wait_until
)
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    msg_sendcmpct,
    msg_getheaders,
    ToHex,
    CInv
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from bsv_pbv_common import (
    wait_for_waiting_blocks,
    wait_for_validating_blocks
)


class PBVSubmitBlock(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        block_count = 0

        # Create a P2P connections
        node0 = NodeConnCB()
        connection0 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection0)

        node1 = NodeConnCB()
        connection1 = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node1)
        node1.add_connection(connection1)

        # *** Prepare node connection for early announcements testing
        node2 = NodeConnCB()
        node2.add_connection(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node2))

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()
        node1.wait_for_verack()

        # *** Activate early announcement functionality for this connection
        #     After this point the early announcements are not received yet -
        #     we still need to set latest announced block (CNode::pindexBestKnownBlock)
        #     which is set for e.g. by calling best headers message with locator
        #     set to non-null
        node2.wait_for_verack()
        node2.send_message(msg_sendcmpct(announce=True))

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))

        _, outs, block_count = prepare_init_chain(self.chain, 101, 1, block_0=False, start_block=0, node=node0)
        out = outs[0]

        self.log.info("waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)

        tip_block_num = block_count - 1

        # adding extra transactions to get different block hashes
        block2_hard = self.chain.next_block(block_count, spend=out, extra_txns=8)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block3_easier = self.chain.next_block(block_count, spend=out, extra_txns=2)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block4_hard = self.chain.next_block(block_count, spend=out, extra_txns=10)
        block_count += 1

        # send three "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"hard block2 hash: {block2_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block2_hard.hash, "add")
        self.log.info(f"hard block4 hash: {block4_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block4_hard.hash, "add")

        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block2_hard.hash, block4_hard.hash}, self.nodes[0], self.log)

        # *** Complete early announcement setup by sending getheaders message
        #     with a non-null locator (pointing to the last block that we know
        #     of on python side - we claim that we know of all the blocks that
        #     bitcoind node knows of)
        #
        #     We also set on_cmpctblock handler as early announced blocks are
        #     announced via compact block messages instead of inv messages
        node2.send_and_ping(msg_getheaders(locator_have=[int(self.nodes[0].getbestblockhash(), 16)]))
        receivedAnnouncement = False
        waiting_for_announcement_block_hash = block2_hard.sha256

        def on_cmpctblock(conn, message):
            nonlocal receivedAnnouncement
            message.header_and_shortids.header.calc_sha256()
            if message.header_and_shortids.header.sha256 == waiting_for_announcement_block_hash:
                receivedAnnouncement = True
        node2.on_cmpctblock = on_cmpctblock

        # send one block via p2p and one via rpc
        node0.send_message(msg_block(block2_hard))

        # *** make sure that we receive announcement of the block before it has
        #     been validated
        wait_until(lambda: receivedAnnouncement)

        # making rpc call submitblock in a separate thread because waitaftervalidation is blocking
        # the return of submitblock
        submitblock_thread = threading.Thread(target=self.nodes[0].submitblock, args=(ToHex(block4_hard),))
        submitblock_thread.start()

        # because self.nodes[0] rpc is blocked we use another rpc client
        rpc_client = get_rpc_proxy(rpc_url(get_datadir_path(self.options.tmpdir, 0), 0),
                                   0,
                                   coveragedir=self.options.coveragedir)

        wait_for_validating_blocks({block2_hard.hash, block4_hard.hash}, rpc_client, self.log)

        # *** prepare to intercept block3_easier announcement - it will not be
        #     announced before validation is complete as early announcement is
        #     limited to announcing one block per height (siblings are ignored)
        #     but after validation is complete we should still get the announcing
        #     compact block message
        receivedAnnouncement = False
        waiting_for_announcement_block_hash = block3_easier.sha256

        self.log.info(f"easy block3 hash: {block3_easier.hash}")
        node1.send_message(msg_block(block3_easier))

        # *** Make sure that we receive compact block announcement of the block
        #     after the validation is complete even though it was not the first
        #     block that was received by bitcoind node.
        #
        #     Also make sure that we receive inv announcement of the block after
        #     the validation is complete by the nodes that are not using early
        #     announcement functionality.
        wait_until(lambda: receivedAnnouncement)
        node0.wait_for_inv([CInv(CInv.BLOCK, block3_easier.sha256)])
        # node 1 was the sender but receives inv for block non the less
        # (with early announcement that's not the case - sender does not receive the announcement)
        node1.wait_for_inv([CInv(CInv.BLOCK, block3_easier.sha256)])

        rpc_client.waitforblockheight(102)
        assert_equal(block3_easier.hash, rpc_client.getbestblockhash())

        # now we can remove waiting status from blocks and finish their validation
        rpc_client.waitaftervalidatingblock(block2_hard.hash, "remove")
        rpc_client.waitaftervalidatingblock(block4_hard.hash, "remove")
        submitblock_thread.join()

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # easier block should still be on tip
        assert_equal(block3_easier.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVSubmitBlock().main()
