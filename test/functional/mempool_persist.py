#!/usr/bin/env python3
# Copyright (c) 2014-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool persistence.

By default, bitcoind will dump mempool on shutdown and
then reload it on startup. This can be overridden with
the -persistmempool=0 command line option.

Test is as follows:

  - start node0, node1 and node2. node1 has -persistmempool=0
  - create 5 transactions on node2 to its own address. Note that these
    are not sent to node0 or node1 addresses because we don't want
    them to be saved in the wallet.
  - check that node0 and node1 have 5 transactions in their mempools
  - shutdown all nodes.
  - startup node0. Verify that it still has 5 transactions
    in its mempool. Shutdown node0. This tests that by default the
    mempool is persistent.
  - startup node1. Verify that its mempool is empty. Shutdown node1.
    This tests that with -persistmempool=0, the mempool is not
    dumped to disk when the node is shut down.
  - Restart node0 with -persistmempool=0. Verify that its mempool is
    empty. Shutdown node0. This tests that with -persistmempool=0,
    the mempool is not loaded from disk on start up.
  - Restart node0 with -persistmempool. Verify that it has 5
    transactions in its mempool. This tests that -persistmempool=0
    does not overwrite a previously valid mempool stored on disk.

"""
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import CTransaction, CTxOut, CTxIn, COutPoint, ToHex, FromHex
from test_framework.script import CScript, OP_TRUE


class MempoolPersistTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [["-genesisactivationheight=100"],
                           ["-persistmempool=0", "-genesisactivationheight=100"],
                           ["-genesisactivationheight=100"]]

    def run_test(self):
        chain_height = self.nodes[0].getblockcount()
        assert_equal(chain_height, 200)

        self.log.debug("Mine a single block to get out of IBD")
        self.nodes[0].generate(1)
        self.sync_all()

        # Create funding transaction that pays to outputs that don't require signatures.
        out_value = 10000
        ftx = CTransaction()
        ftx.vout.append(CTxOut(out_value, CScript([OP_TRUE])))
        ftx.vout.append(CTxOut(out_value, CScript([OP_TRUE])))
        ftxHex = self.nodes[2].fundrawtransaction(ToHex(ftx),{'changePosition' : len(ftx.vout)})['hex']
        ftxHex = self.nodes[2].signrawtransaction(ftxHex)['hex']
        self.nodes[2].sendrawtransaction(ftxHex)
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()

        # Create & send a couple of non-final txns.
        for i in range(2):
            parent_txid = ftx.sha256
            send_value = out_value - 500
            non_final_tx = CTransaction()
            non_final_tx.vin.append(CTxIn(COutPoint(parent_txid, i), b'', 0x01))
            non_final_tx.vout.append(CTxOut(int(send_value), CScript([OP_TRUE])))
            non_final_tx.nLockTime = int(time.time()) + 300
            non_final_txHex = self.nodes[2].signrawtransaction(ToHex(non_final_tx))['hex']
            self.nodes[2].sendrawtransaction(non_final_txHex)
        self.sync_all()
        self.log.debug("Verify that all nodes have 2 transactions in their non-final mempools")
        assert_equal(len(self.nodes[0].getrawnonfinalmempool()), 2)
        assert_equal(len(self.nodes[1].getrawnonfinalmempool()), 2)
        assert_equal(len(self.nodes[2].getrawnonfinalmempool()), 2)

        self.log.debug("Send another 4 transactions from node2 (to its own address)")
        for i in range(4):
            self.nodes[2].sendtoaddress(
                self.nodes[2].getnewaddress(), Decimal("10"))
        self.sync_all()

        self.log.debug("Verify that all nodes have 5 transactions in their main mempools")
        assert_equal(len(self.nodes[0].getrawmempool()), 5)
        assert_equal(len(self.nodes[1].getrawmempool()), 5)
        assert_equal(len(self.nodes[2].getrawmempool()), 5)

        self.log.debug(
            "Stop-start node0 and node1. Verify that node0 has the transactions in its mempools and node1 does not.")
        self.stop_nodes()
        self.start_node(0)
        self.start_node(1)
        # Give bitcoind a second to reload the mempool
        time.sleep(1)
        wait_until(lambda: len(self.nodes[0].getrawmempool()) == 5)
        wait_until(lambda: len(self.nodes[0].getrawnonfinalmempool()) == 2)
        assert_equal(len(self.nodes[1].getrawmempool()), 0)
        assert_equal(len(self.nodes[1].getrawnonfinalmempool()), 0)

        self.log.debug(
            "Stop-start node0 with -persistmempool=0. Verify that it doesn't load its mempool.dat file.")
        self.stop_nodes()
        self.start_node(0, extra_args=["-persistmempool=0"])
        # Give bitcoind a second to reload the mempool
        time.sleep(1)
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        assert_equal(len(self.nodes[0].getrawnonfinalmempool()), 0)

        self.log.debug(
            "Stop-start node0. Verify that it has the transactions in its mempool.")
        self.stop_nodes()
        self.start_node(0)
        wait_until(lambda: len(self.nodes[0].getrawmempool()) == 5)
        wait_until(lambda: len(self.nodes[0].getrawnonfinalmempool()) == 2)


if __name__ == '__main__':
    MempoolPersistTest().main()
