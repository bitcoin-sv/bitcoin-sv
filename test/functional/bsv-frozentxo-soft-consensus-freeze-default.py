#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test soft consensus freeze - default freeze

Make sure that not setting softconsensusfreezeduration attribute freezes block
for expected duration and check rpc method preciousblock has no impact on soft freeze duration.
"""
from soft_consensus_freeze_base import Send_node, SoftConsensusFreezeBase

from test_framework.test_framework import ChainManager
from test_framework.blocktools import PreviousSpendableOutput
from test_framework.script import CScript, OP_TRUE


class FrozenTXOSoftConsensusFreeze(SoftConsensusFreezeBase):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1", "-minrelaytxfee=0", "-limitfreerelay=999999"]]
        self.block_count = 0

    def _test_default_freeze(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze checks")

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out)

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]))
        first_frozen_block = self._mine_and_check_rejected(node, first_spend_frozen_tx)

        # both blocks are still frozen
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        last_frozen_block = self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        bestblockhash = node.rpc.getbestblockhash()

        # check that precious block does not have impact on soft freeze duration
        node.rpc.preciousblock(last_frozen_block.hash)

        # assert preciousblock did not change the tip
        assert (bestblockhash == node.rpc.getbestblockhash())

        # all blocks are unfrozen
        self._mine_and_send_block(None, node)

    def run_test(self):
        node = self._init()

        send_node = Send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])

        spendable_out_1 = self.chain.get_spendable_output()

        self._test_default_freeze(spendable_out_1, send_node)


if __name__ == '__main__':
    FrozenTXOSoftConsensusFreeze().main()
