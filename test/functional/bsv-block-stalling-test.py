#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test stalling isn't triggered just for large blocks.
"""
from test_framework.blocktools import prepare_init_chain
from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import assert_equal, p2p_port, connect_nodes
from test_framework.comptool import TestInstance
from test_framework.cdefs import ONE_GIGABYTE, ONE_MEGABYTE, ONE_KILOBYTE
from test_framework.util import get_rpc_proxy, wait_until, check_for_log_msg


class StallingTest(ComparisonTestFramework):

    def set_test_params(self):
        self.bitcoind_proc_wait_timeout = 210
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.genesisactivationheight = 101
        self.nocleanup = True
        self.extra_args = [
            [
                '-whitelist=127.0.0.1',
                '-excessiveblocksize=%d' % (ONE_GIGABYTE * 6),
                '-blockmaxsize=%d' % (ONE_GIGABYTE * 6),
                '-maxmempool=%d' % (ONE_GIGABYTE * 10),
                '-maxmempoolsizedisk=0',
                '-maxtxsizepolicy=%d' % ONE_GIGABYTE,
                '-maxscriptsizepolicy=0',
                '-rpcservertimeout=1000',
                '-genesisactivationheight=%d' % self.genesisactivationheight,
                "-txindex"
            ]
        ] * self.num_nodes

        self.num_blocks = 120

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block
        node = get_rpc_proxy(self.nodes[0].url, 1, timeout=6000, coveragedir=self.nodes[0].coverage_dir)

        # Create a new block & setup initial chain with spendable outputs
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))
        block(0)
        yield self.accepted()
        test, out, _ = prepare_init_chain(self.chain, self.num_blocks, self.num_blocks+1)
        yield test

        # Create 1GB block
        block(1, spend=out[0], block_size=1*ONE_GIGABYTE)
        yield self.accepted(420) # larger timeout is needed to prevent timeouts on busy machine and debug builds

        # Create long chain of smaller blocks
        test = TestInstance(sync_every_block=False)
        for i in range(self.num_blocks):
            block(6000 + i, spend=out[i + 1], block_size=64*ONE_KILOBYTE)
            test.blocks_and_transactions.append([self.chain.tip, True])
        yield test

        # Launch another node with config that should avoid a stall during IBD
        self.log.info("Launching extra nodes")
        self.add_node(2,
                      extra_args = [
                          '-whitelist=127.0.0.1',
                          '-excessiveblocksize=%d' % (ONE_GIGABYTE * 6),
                          '-blockmaxsize=%d' % (ONE_GIGABYTE * 6),
                          '-maxtxsizepolicy=%d' % ONE_GIGABYTE,
                          '-maxscriptsizepolicy=0',
                          '-rpcservertimeout=1000',
                          '-genesisactivationheight=%d' % self.genesisactivationheight,
                          "-txindex",
                          "-maxtipage=0",
                          "-blockdownloadwindow=64",
                          "-blockstallingtimeout=6"
                      ],
                      init_data_dir=True)
        self.start_node(2)

        # Connect the new nodes up so they do IBD
        self.log.info("Starting IBD")
        connect_nodes(self.nodes, 0, 2)
        connect_nodes(self.nodes, 1, 2)
        self.sync_all(timeout=240*self.options.timeoutfactor) # larger timeout is needed to prevent timeouts on busy machine and debug builds

        # Check we didn't hit a stall for node2
        assert(not check_for_log_msg(self, "stalling block download", "/node2"))


if __name__ == '__main__':
    StallingTest().main()
