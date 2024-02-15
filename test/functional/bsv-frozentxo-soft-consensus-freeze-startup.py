#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test soft consensus freeze during node startup and IBD

See comments and log entries in above functions for detailed description of steps performed in test.
"""
from soft_consensus_freeze_base import Send_node, SoftConsensusFreezeBase

from test_framework.blocktools import PreviousSpendableOutput
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import ChainManager
from test_framework.util import assert_equal, connect_nodes, disconnect_nodes
import time


class FrozenTXOSoftConsensusFreezeStartup(SoftConsensusFreezeBase):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1", "-minminingtxfee=0", "-acceptnonstdtxn=1", "-minrelaytxfee=0", "-softconsensusfreezeduration=4"],
                           ["-whitelist=127.0.0.1", "-minminingtxfee=0", "-acceptnonstdtxn=1", "-minrelaytxfee=0", "-softconsensusfreezeduration=4"]]
        self.block_count = 0

    def _init(self):
        # stop node that will be later used to test IDB
        self.stop_node(1)

        return super()._init()

    def run_test(self):
        node_cb = self._init()
        node = Send_node(self.options.tmpdir, self.log, 0, node_cb, self.nodes[0])

        self.log.info("*** Testing soft consensus freeze during node startup/IBD")

        spendable_out = self.chain.get_spendable_output()

        frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out)

        last_valid_tip_hash = node.rpc.getbestblockhash()
        last_valid_tip_height = node.rpc.getblockcount()

        # this block must not become tip because it contains a transaction trying to spend consensus frozen output
        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        self._mine_and_check_rejected(node, spend_frozen_tx)

        # child blocks are still considered frozen
        self._mine_and_send_block(None, node, False, last_valid_tip_hash)
        self._mine_and_send_block(None, node, False, last_valid_tip_hash)
        self._mine_and_send_block(None, node, False, last_valid_tip_hash)

        node.restart_node()

        # must remain at the same tip as before
        assert_equal(last_valid_tip_hash, node.rpc.getbestblockhash())

        self.log.info("Starting second node")
        self.start_node(1)

        self.log.info(f"Freezing TXO {frozen_tx.hash},0 on consensus blacklist on second node")
        result=self.nodes[1].addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : frozen_tx.hash,
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0}],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Connecting first and second node")
        connect_nodes(self.nodes, 1, 0)

        self.log.info(f"Waiting for block height {last_valid_tip_height} via rpc on second node")
        self.nodes[1].waitforblockheight(last_valid_tip_height)

        self.log.info("Checking that tip on second node stays on the last valid block")
        time.sleep(2)
        assert_equal(last_valid_tip_hash, self.nodes[1].getbestblockhash())

        self.log.info("Disconnecting first and second node")
        disconnect_nodes(self.nodes[1], 0)

        # mine another block that should still be frozen
        self._mine_and_send_block(None, node, False, last_valid_tip_hash)

        self.log.info("Connecting first and second node")
        connect_nodes(self.nodes, 1, 0)

        self.log.info("Checking that tip on second node stays on the last valid block")
        time.sleep(2)
        assert_equal(last_valid_tip_hash, self.nodes[1].getbestblockhash())

        # all blocks are unfrozen
        new_valid_tip = self._mine_and_send_block(None, node)
        self.nodes[1].waitforblockheight(last_valid_tip_height+6)
        assert_equal(new_valid_tip.hash, self.nodes[0].getbestblockhash())
        assert_equal(new_valid_tip.hash, self.nodes[1].getbestblockhash())


if __name__ == '__main__':
    FrozenTXOSoftConsensusFreezeStartup().main()
