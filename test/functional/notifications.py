#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""Test the -blocknotify and -walletnotify options."""
import os
import shutil

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, wait_until, connect_nodes_bi


class NotificationsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_network(self):
        self.dir_notify_block = os.path.join(self.options.tmpdir, "notify_block")
        self.dir_notify_tx = os.path.join(self.options.tmpdir, "notify_tx")
        os.mkdir(self.dir_notify_block)
        os.mkdir(self.dir_notify_tx)

        # -blocknotify on node0, walletnotify on node1
        self.extra_args = [["-blocknotify=echo b > {}".format(os.path.join(self.dir_notify_block, '%s'))],
                           ["-rescan",
                            "-walletnotify=echo t > {}".format(os.path.join(self.dir_notify_tx, '%s'))]]
        super().setup_network()

    def run_test(self):
        self.log.info("test -blocknotify")
        block_count = 10
        blocks = self.nodes[1].generate(block_count)

        # wait at most 10 seconds for expected number of block notification files
        wait_until(lambda: len(os.listdir(self.dir_notify_block)) == block_count, timeout=10)

        # file names in notify_block directory should equal the generated blocks hashes
        assert_equal(sorted(blocks), sorted(os.listdir(self.dir_notify_block)))

        self.log.info("test -walletnotify")
        # wait at most 10 seconds for expected number of transaction notification files
        wait_until(lambda: len(os.listdir(self.dir_notify_tx)) == block_count, timeout=10)

        # file names in notify_tx directory should equal the generated transaction hashes
        txids_rpc = list(map(lambda t: t['txid'], self.nodes[1].listtransactions("*", block_count)))
        assert_equal(sorted(txids_rpc), sorted(os.listdir(self.dir_notify_tx)))

        shutil.rmtree(self.dir_notify_tx)
        os.mkdir(self.dir_notify_tx)

        self.log.info("test -walletnotify after rescan")
        # restart node to rescan to force wallet notifications
        self.restart_node(1)
        connect_nodes_bi(self.nodes, 0, 1)

        # wait at most 10 seconds for expected number of transaction notification files
        wait_until(lambda: len(os.listdir(self.dir_notify_tx)) == block_count, timeout=10)

        # file names in notify_tx directory should equal the generated transaction hashes
        txids_rpc = list(map(lambda t: t['txid'], self.nodes[1].listtransactions("*", block_count)))
        assert_equal(sorted(txids_rpc), sorted(os.listdir(self.dir_notify_tx)))


if __name__ == '__main__':
    NotificationsTest().main()
