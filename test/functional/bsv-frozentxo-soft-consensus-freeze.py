#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test soft consensus freeze

_test_soft_consensus_freeze: two consecutive blocks with two different frozen transactions
_test_soft_consensus_freeze_on_refreeze: block with consensus frozen transaction
                                         on which the freeze duration changes
                                         after the block has been frozen
_test_soft_consensus_freeze_clear_all: block with consensus frozen transaction
                                       where we clear all consensus freezes
                                       after the block has been frozen
_test_soft_consensus_freeze_invalidate_block: test invalidate and reconsider frozen block

See comments and log entries in above functions for detailed description of steps performed in each test.
"""
from soft_consensus_freeze_base import Send_node, SoftConsensusFreezeBase

from test_framework.test_framework import ChainManager
from test_framework.blocktools import PreviousSpendableOutput
from test_framework.script import CScript, OP_TRUE
from test_framework.util import assert_equal

class FrozenTXOSoftConsensusFreeze(SoftConsensusFreezeBase):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1", "-minrelaytxfee=0", "-limitfreerelay=999999", "-softconsensusfreezeduration=4"]]
        self.block_count = 0

    def _test_soft_consensus_freeze(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze checks")

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx( node, spendable_out[0] )
        second_frozen_tx = self._create_tx_mine_block_and_freeze_tx( node, spendable_out[1] )

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx( PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]) )
        first_frozen_block = self._mine_and_check_rejected( node, first_spend_frozen_tx )

        # block is accepted but ignored since freeze is in place for previous block
        second_spend_frozen_tx = self._create_tx( PreviousSpendableOutput(second_frozen_tx, 0), b'', CScript([OP_TRUE]) )
        second_frozen_block = self._mine_and_send_block(second_spend_frozen_tx, node, False, node.rpc.getbestblockhash())

        # both blocks are still frozen
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        # first block is unfrozen but since height restriction is not met due
        # to second block being frozen, we remain on the old tip
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        node.reject_check( second_frozen_block )

        # all blocks are unfrozen
        self._mine_and_send_block(None, node)

    def _test_soft_consensus_freeze_on_refreeze(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze on refreeze checks")

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx( node, spendable_out )

        tip_height = node.rpc.getblockcount()

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx( PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]) )
        first_frozen_block = self._mine_and_check_rejected( node, first_spend_frozen_tx )
        first_frozen_block_height = tip_height + 1

        freeze_for_two_blocks = first_frozen_block_height + 2

        # limit the duration of freeze
        self.log.info(f"Freezing TXO {first_frozen_tx.hash} on consensus blacklist until height {freeze_for_two_blocks}")
        result=node.rpc.addToConsensusBlacklist({
            "funds": [
            {
                "txOut" : {
                    "txId" : first_frozen_tx.hash,
                    "vout" : 0
                },
                "enforceAtHeight": [{"start": 0, "stop": freeze_for_two_blocks}],
                "policyExpiresWithConsensus": False
            }]
        });
        assert_equal(result["notProcessed"], [])

        # block is expected to still be frozen
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        # block is expected to still be frozen even though we've changed the freeze
        # duration as once the frozen calculation is performed on a block it is
        # never changed
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        # all blocks are unfrozen - this proves that the old duration remained
        # in place
        self._mine_and_send_block(None, node)

    def _test_soft_consensus_freeze_clear_all(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze on clear all")

        # perform initial clear so that other tests don't interfere with this one
        node.rpc.clearBlacklists( { "removeAllEntries": True } )

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx( node, spendable_out )

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx( PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]) )
        first_frozen_block = self._mine_and_check_rejected( node, first_spend_frozen_tx )

        # clear all frozen entries
        result = node.rpc.clearBlacklists( { "removeAllEntries": True } )
        assert_equal(result["numRemovedEntries"], 1)

        # block is expected to still be frozen even though we've changed the freeze
        # duration as once the frozen calculation is performed on a block it is
        # never changed
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        # all blocks are unfrozen - this proves that the old duration remained
        # in place
        self._mine_and_send_block(None, node)

    def _test_soft_consensus_freeze_invalidate_block(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze and invalidate block checks")

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx( node, spendable_out[0] )
        second_frozen_tx = self._create_tx_mine_block_and_freeze_tx( node, spendable_out[1] )

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx( PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]) )
        first_frozen_block = self._mine_and_check_rejected( node, first_spend_frozen_tx )

        # block is accepted but ignored since freeze is in place for previous block
        second_spend_frozen_tx = self._create_tx( PreviousSpendableOutput(second_frozen_tx, 0), b'', CScript([OP_TRUE]) )
        second_frozen_block = self._mine_and_send_block(second_spend_frozen_tx, node, False, node.rpc.getbestblockhash())

        block_before_frozen_blocks_hash = node.rpc.getbestblockhash()

        # both blocks are still frozen
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)

        # first block is unfrozen but since height restriction is not met due
        # to second block being frozen, we remain on the old tip
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)
        node.reject_check( second_frozen_block )

        # all blocks are unfrozen
        block = self._mine_and_send_block(None, node)

        # check that verifychain works after node restart
        assert node.rpc.verifychain(4, 0)
        self.restart_node(0)
        assert node.rpc.verifychain(4, 0)

        node.rpc.invalidateblock(block.hash)

        assert( block_before_frozen_blocks_hash == node.rpc.getbestblockhash() )

        # double check that reconsidering the block works as expected
        node.rpc.reconsiderblock(block.hash)

        assert(block.hash == node.rpc.getbestblockhash())

    def run_test(self):
        node = self._init()

        send_node = Send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])

        spendable_out_1 = [ self.chain.get_spendable_output(), self.chain.get_spendable_output() ]
        spendable_out_2 = self.chain.get_spendable_output()
        spendable_out_3 = self.chain.get_spendable_output()
        spendable_out_4 = [ self.chain.get_spendable_output(), self.chain.get_spendable_output() ]

        self._test_soft_consensus_freeze( spendable_out_1, send_node )
        self._test_soft_consensus_freeze_on_refreeze( spendable_out_2, send_node )
        self._test_soft_consensus_freeze_clear_all( spendable_out_3, send_node )
        self._test_soft_consensus_freeze_invalidate_block( spendable_out_4, send_node )

if __name__ == '__main__':
    FrozenTXOSoftConsensusFreeze().main()
