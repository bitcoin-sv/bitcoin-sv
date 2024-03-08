#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test parallel downloads for slow blocks.
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, connect_nodes
from test_framework.util import sync_blocks, disconnect_nodes_bi, mine_large_block, wait_until, check_for_log_msg


class ParallelBlockDownloadTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.nocleanup = True
        self.extra_args = [
            [
                '-whitelist=127.0.0.1',
                '-acceptnonstdtxn=1',
                '-mindebugrejection=0.0000025'
            ],
            [
                '-whitelist=127.0.0.1',
                '-streamsendratelimit=20000',
                '-acceptnonstdtxn=1',
                '-mindebugrejection=0.0000025'
            ],
            [
                '-whitelist=127.0.0.1',
                '-blockdownloadslowfetchtimeout=20',
                '-acceptnonstdtxn=1',
                '-mindebugrejection=0.0000025'
            ]
        ]

    def run_test(self):
        # Start by creating some coinbases we can spend later
        self.nodes[0].generate(150)
        sync_blocks(self.nodes[0:3])

        # Connect node2 just to node1 so that it is forced to request the next blocks
        # from a slow sending peer
        disconnect_nodes_bi(self.nodes, 0, 2)
        disconnect_nodes_bi(self.nodes, 1, 2)
        connect_nodes(self.nodes, 2, 1)

        # Extend the chain with a big block and some more small blocks
        utxos = []
        mine_large_block(self.nodes[0], utxos)
        large_block_hash = self.nodes[0].getbestblockhash()
        self.nodes[0].generate(5)
        sync_blocks(self.nodes[0:2])

        # Ensure node2 has started to request the big block from slow node1
        def blockInFlight(blockNum):
            inflight = self.nodes[2].getpeerinfo()[0]["inflight"]
            return blockNum in inflight
        wait_until(lambda: blockInFlight(151), check_interval=1)

        # Reconnect node2 to node0 so that it has another option from which to fetch blocks
        connect_nodes(self.nodes, 2, 0)
        sync_blocks(self.nodes[0:3])

        # Check that a parallel fetch to node0 was triggered from node2
        assert(check_for_log_msg(self, "Triggering parallel block download for {}".format(large_block_hash), "/node2"))


if __name__ == '__main__':
    ParallelBlockDownloadTest().main()
