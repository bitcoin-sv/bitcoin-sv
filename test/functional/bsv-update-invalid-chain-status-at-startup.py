#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that at startup node updates header-only tip statuses of forks that contain invalid blocks.
Chain was generated with old version of node that in some cases did not correctly set block status
withFailedParent to fork blocks with invalid ancestors. In that case getchaintips method would return
status headers-only but it should be invalid.

Pre-generated chain contains active chain with height 12, valid fork of 5 blocks, invalid fork of 20 blocks
that has status headers-only in old version (it has data of first 15 blocks and only headers for the rest,
first block is invalid) and another invalid fork of 5 blocks (headers only) that starts at block 12
of first invalid fork and has status headers-only in old version.

Chain schema:

              H-H..H-T4
             /
   I-B-B-B..B-B-H-H-H...H-T3
  /
B-B-B......B-B-T1
  \
   B-...B-T2

* B - Block, I - Invalid block, H - Block header, Tn - Tip

T1 status = active, old status = active, hash = 7da1d835f7759f97958fb878d040f25847e4c43c1081781bdf23e4d4eafb641d
T2 status = valid-fork, old status = valid-fork, hash = 058af9eeaf5cc2916afd0e4cc37efa7dedc5038d3826e728b73eef050569a517
T3 status = invalid, old status = headers-only, hash = 129cae8395e28cdf8acda1e78853d45b037b4945edc7f13e799db2ab5354488f
T4 status = invalid, old status = headers-only, hash = 4fdd65af7b30f80d97b89d7584ac66a6e5d5f81ce971b218b7380202a076146c

After node starts, statuses of invalid forks should be invalid instead of headers-only
"""
import shutil

from time import sleep
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.blocktools import wait_for_tip, wait_for_tip_status


class UpdateInvalidChainAtStartup(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_chain(self):
        self.log.info("Initializing test directory " + self.options.tmpdir)

        #copy pregenerated data with node version 1.0.0 to tmpdir
        for i in range(self.num_nodes):
            initialize_datadir(self.options.tmpdir, i)
            from_dir = os.path.normpath(os.path.dirname(os.path.realpath(__file__)) + "/../../test/functional/data/dataTemplate_InvalidateChain/node"+str(i)+"/regtest")
            to_dir = os.path.join(self.options.tmpdir, "node"+str(i)+"/regtest")
            shutil.copytree(from_dir, to_dir)

    def run_test(self):

        # check tip statuses after node start
        wait_for_tip(self.nodes[0], "7da1d835f7759f97958fb878d040f25847e4c43c1081781bdf23e4d4eafb641d")
        wait_for_tip_status(self.nodes[0], "058af9eeaf5cc2916afd0e4cc37efa7dedc5038d3826e728b73eef050569a517", "valid-fork")
        wait_for_tip_status(self.nodes[0], "129cae8395e28cdf8acda1e78853d45b037b4945edc7f13e799db2ab5354488f", "invalid")
        wait_for_tip_status(self.nodes[0], "4fdd65af7b30f80d97b89d7584ac66a6e5d5f81ce971b218b7380202a076146c", "invalid")


if __name__ == '__main__':
    UpdateInvalidChainAtStartup().main()
