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
- Block 4 is submitted via rpc command submitminingsolution.
Block 3 should be active in the end because it was easiest to validate and
therefore won the validation race.

*This test is similar to bsv-pbv-submitblock.py which uses different RPC call to
 submit the block.
"""
import threading

from test_framework.util import (
    assert_equal,
    p2p_port,
    get_rpc_proxy,
    rpc_url,
    get_datadir_path,
)
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    ToHex,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from bsv_pbv_common import (
    wait_for_waiting_blocks,
    wait_for_validating_blocks
)


class PBVSubmitMiningSolution(BitcoinTestFramework):

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

        for i in range(100):
            block = self.chain.next_block(block_count)
            block_count += 1
            self.chain.save_spendable_output()
            node0.send_message(msg_block(block))

        out = self.chain.get_spendable_output()

        self.log.info("waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)

        tip_block_num = block_count - 1

        # adding extra transactions to get different block hashes
        block2_hard = self.chain.next_block(block_count, spend=out, extra_txns=8)
        block_count += 1

        self.chain.set_tip(tip_block_num)

        block3_easier = self.chain.next_block(block_count, spend=out, extra_txns=2)
        block_count += 1

        mining_candidate = self.nodes[0].getminingcandidate()
        block4_hard = self.chain.next_block(block_count)
        block4_hard.hashPrevBlock = int(mining_candidate["prevhash"], 16)
        block4_hard.nTime = mining_candidate["time"]
        block4_hard.nVersion = mining_candidate["version"]
        block4_hard.solve()

        mining_solution = {"id": mining_candidate["id"],
                           "nonce": block4_hard.nNonce,
                           "coinbase": ToHex(block4_hard.vtx[0]),
                           "time": mining_candidate["time"],
                           "version": mining_candidate["version"]}

        # send three "hard" blocks, with waitaftervalidatingblock we artificially
        # extend validation time.
        self.log.info(f"hard block2 hash: {block2_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block2_hard.hash, "add")
        self.log.info(f"hard block4 hash: {block4_hard.hash}")
        self.nodes[0].waitaftervalidatingblock(block4_hard.hash, "add")

        # make sure block hashes are in waiting list
        wait_for_waiting_blocks({block2_hard.hash, block4_hard.hash}, self.nodes[0], self.log)

        # send one block via p2p and one via rpc
        node0.send_message(msg_block(block2_hard))

        # making rpc call submitminingsolution in a separate thread because waitaftervalidation is blocking
        # the return of submitminingsolution
        submitminingsolution_thread = threading.Thread(target=self.nodes[0].submitminingsolution, args=(mining_solution,))
        submitminingsolution_thread.start()

        # because self.nodes[0] rpc is blocked we use another rpc client
        rpc_client = get_rpc_proxy(rpc_url(get_datadir_path(self.options.tmpdir, 0), 0), 0,
                                   coveragedir=self.options.coveragedir)

        wait_for_validating_blocks({block2_hard.hash, block4_hard.hash}, rpc_client, self.log)

        self.log.info(f"easy block3 hash: {block3_easier.hash}")
        node1.send_message(msg_block(block3_easier))

        rpc_client.waitforblockheight(102)
        assert_equal(block3_easier.hash, rpc_client.getbestblockhash())

        # now we can remove waiting status from blocks and finish their validation
        rpc_client.waitaftervalidatingblock(block2_hard.hash, "remove")
        rpc_client.waitaftervalidatingblock(block4_hard.hash, "remove")
        submitminingsolution_thread.join()

        # wait till validation of block or blocks finishes
        node0.sync_with_ping()

        # easier block should still be on tip
        assert_equal(block3_easier.hash, self.nodes[0].getbestblockhash())


if __name__ == '__main__':
    PBVSubmitMiningSolution().main()
