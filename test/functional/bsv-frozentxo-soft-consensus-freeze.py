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
_test_soft_consensus_freeze_invalidate_block: - test invalidate and reconsider frozen block
                                              - test send invalid block via p2p
_test_soft_consensus_freeze_submitblock: using submitblock to send frozen block
_test_soft_consensus_freeze_competing_chains: test reorgs between valid and frozen chain
_test_soft_consensus_freeze_invalid_frozen_block: test handling of frozen block that is also invalid.

See comments and log entries in above functions for detailed description of steps performed in each test.
"""
from soft_consensus_freeze_base import Send_node, SoftConsensusFreezeBase

from test_framework.test_framework import ChainManager
from test_framework.blocktools import PreviousSpendableOutput, create_coinbase, create_block
from test_framework.mininode import msg_block, COIN
from test_framework.script import CScript, OP_TRUE, OP_FALSE, OP_DROP
from test_framework.util import assert_equal


class FrozenTXOSoftConsensusFreeze(SoftConsensusFreezeBase):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1",
                            "-minrelaytxfee=0",
                            "-limitfreerelay=999999",
                            "-softconsensusfreezeduration=4",
                            "-disablesafemode=1"]]
        self.block_count = 0

    def _test_soft_consensus_freeze(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze checks")

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out[0])
        second_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out[1])

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]))
        first_frozen_block = self._mine_and_check_rejected(node, first_spend_frozen_tx)

        # block is accepted but ignored since freeze is in place for previous block
        second_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(second_frozen_tx, 0), b'', CScript([OP_TRUE]))
        second_frozen_block = self._mine_and_send_block(second_spend_frozen_tx, node, False, node.rpc.getbestblockhash())

        # both blocks are still frozen
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        # first block is unfrozen but since height restriction is not met due
        # to second block being frozen, we remain on the old tip
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        node.reject_check(second_frozen_block)

        # all blocks are unfrozen
        self._mine_and_send_block(None, node)

    def _test_soft_consensus_freeze_on_refreeze(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze on refreeze checks")

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out)

        tip_height = node.rpc.getblockcount()

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]))
        first_frozen_block = self._mine_and_check_rejected(node, first_spend_frozen_tx)
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
        })
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
        node.rpc.clearBlacklists({"removeAllEntries": True})

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out)

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]))
        first_frozen_block = self._mine_and_check_rejected(node, first_spend_frozen_tx)

        # clear all frozen entries
        result = node.rpc.clearBlacklists({"removeAllEntries": True})
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

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out[0])
        second_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out[1])

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]))
        first_frozen_block = self._mine_and_check_rejected(node, first_spend_frozen_tx)

        # block is accepted but ignored since freeze is in place for previous block
        second_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(second_frozen_tx, 0), b'', CScript([OP_TRUE]))
        second_frozen_block = self._mine_and_send_block(second_spend_frozen_tx, node, False, node.rpc.getbestblockhash())

        block_before_frozen_blocks_hash = node.rpc.getbestblockhash()

        # both blocks are still frozen
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)

        # first block is unfrozen but since height restriction is not met due
        # to second block being frozen, we remain on the old tip
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)
        node.reject_check(second_frozen_block)

        # save hash and time of the last soft frozen block for later
        last_soft_frozen_hash = self.chain.tip.hash
        last_soft_frozen_time = self.chain.tip.nTime

        # all blocks are unfrozen
        block = self._mine_and_send_block(None, node)
        node.rpc.invalidateblock(block.hash)

        assert(block_before_frozen_blocks_hash == node.rpc.getbestblockhash())

        # check that reconsidering the block works as expected
        node.rpc.reconsiderblock(block.hash)
        assert(block.hash == node.rpc.getbestblockhash())

        # check that verifychain works after node restart
        assert node.rpc.verifychain(4, 0)
        node.restart_node()
        assert node.rpc.verifychain(4, 0)

        # check that invalidateblock works after node restart
        node.restart_node()
        assert(block.hash == node.rpc.getbestblockhash())
        node.rpc.invalidateblock(block.hash)
        assert (block_before_frozen_blocks_hash == node.rpc.getbestblockhash())

        # create coinbase output that pays to much
        invalid_coinbase_tx = create_coinbase(height=node.rpc.getblockcount()+1, outputValue=300)
        invalid_block = create_block(int(last_soft_frozen_hash, 16), invalid_coinbase_tx, last_soft_frozen_time+1)
        invalid_block.solve()
        node.p2p.send_and_ping(msg_block(invalid_block))
        assert(node.check_log(f"ConnectBlock {invalid_block.hash} failed \\(bad-cb-amount \\(code 16\\)\\)"))

        # make sure tip is still the same
        assert (block_before_frozen_blocks_hash == node.rpc.getbestblockhash())

        node.rpc.reconsiderblock(block.hash)
        assert(block.hash == node.rpc.getbestblockhash())

    def _test_soft_consensus_freeze_submitblock(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze with submitblock RPC")

        frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out)

        last_valid_block_hash = node.rpc.getbestblockhash()

        # block should not become new tip as it contains transaction spending frozen TXO
        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        frozen_block = self._mine_block(spend_frozen_tx)
        self.submit_block_and_check_tip(node, frozen_block, last_valid_block_hash)

        # block is still frozen
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block_hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block_hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block_hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block_hash)

        # all blocks are now unfrozen
        new_valid_block = self._mine_block(None)
        self.submit_block_and_check_tip(node, new_valid_block, new_valid_block.hash)

    def _test_soft_consensus_freeze_competing_chains(self, spendable_txo, node):
        self.log.info("*** Performing soft consensus freeze with competing chains")

        frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_txo)

        root_chain_tip = self.get_chain_tip()

        # mine 5 blocks on valid chain (one less than is needed for the frozen chain to become active)
        self.log.info("Mining blocks on valid chain")
        self._mine_and_send_block(None, node)
        self._mine_and_send_block(None, node)
        self._mine_and_send_block(None, node)
        self._mine_and_send_block(None, node)
        last_valid_block = self._mine_and_send_block(None, node)

        valid_chain_tip = self.get_chain_tip()

        self.set_chain_tip(root_chain_tip)

        self.log.info("Mining blocks on frozen chain")
        # block should not become new tip as it contains transaction spending frozen TXO
        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        frozen_block = self._mine_block(spend_frozen_tx)
        self.submit_block_and_check_tip(node, frozen_block, last_valid_block.hash)

        # next 4 blocks are also considered soft consensus frozen and must not become new tip
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)

        # this block is high enough for the frozen chain to become active and should become new tip
        new_frozen_tip = self._mine_and_send_block(None, node)

        frozen_chain_tip = self.get_chain_tip()

        self.log.info("Mining blocks on valid chain")
        # 2 new blocks on valid chain should trigger reorg back to valid chain
        self.set_chain_tip(valid_chain_tip)
        next_frozen_tip = self._mine_block(None)
        self.submit_block_and_check_tip(node, next_frozen_tip, new_frozen_tip.hash)
        new_valid_tip = self._mine_block(None)
        node.p2p.send_and_ping(msg_block(new_valid_tip))
        assert_equal(new_valid_tip.hash, node.rpc.getbestblockhash())
        assert(node.check_frozen_tx_log(next_frozen_tip.hash)) # NOTE: Reject is expected because transaction spending frozen TXO is added back to mempool and its validation must fail when checked against new tip.

        self.log.info("Mining blocks on frozen chain")
        # 2 new blocks on frozen chain should trigger reorg back to frozen chain
        self.set_chain_tip(frozen_chain_tip)
        self.submit_block_and_check_tip(node, self._mine_block(None), new_valid_tip.hash)
        self._mine_and_send_block(None, node)

    def _test_soft_consensus_freeze_invalid_frozen_block(self, spendable_txos, node):
        self.log.info("*** Performing soft consensus freeze with invalid frozen block")

        frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_txos[0])

        root_chain_tip = self.get_chain_tip()

        # mine 5 blocks on valid chain (one less than is needed for the frozen chain to become active)
        self.log.info("Mining blocks on valid chain")
        self._mine_and_send_block(None, node)
        self._mine_and_send_block(None, node)
        self._mine_and_send_block(None, node)
        self._mine_and_send_block(None, node)
        last_valid_block = self._mine_and_send_block(None, node)

        self.set_chain_tip(root_chain_tip)

        self.log.info("Mining blocks on frozen chain")
        # block should not become new tip as it is not high enough
        # it should also be considered soft consensus frozen because it contains transaction spending frozen TXO
        # and is invalid because coinbase pays too much
        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        frozen_block = self._mine_block(spend_frozen_tx)
        frozen_block.vtx[0].vout[0].nValue = 300 * COIN # coinbase that pays too much
        frozen_block.vtx[0].rehash()
        self.chain.update_block(self.block_count-1, [])
        self.submit_block_and_check_tip(node, frozen_block, last_valid_block.hash)

        # next 4 blocks would also be considered soft consensus frozen and must not become new tip
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)

        # invalid block has not yet been validated
        frozen_block_block_checked_log_string = f"ConnectBlock {frozen_block.hash} failed \\(bad-cb-amount \\(code 16\\)\\)"
        assert(not node.check_log(frozen_block_block_checked_log_string))

        # this block is high enough for the frozen chain to become active but
        # it should not, because the block is invalid
        new_frozen_tip = self._mine_and_send_block(None, node, False, last_valid_block.hash)

        # invalid block has now been validated
        assert(node.check_log(frozen_block_block_checked_log_string))

        # same thing again but with frozen block that is also invalid because it contains invalid transaction
        self.set_chain_tip(root_chain_tip)
        frozen_block = self._mine_block(spend_frozen_tx)
        valid_tx = self._create_tx(spendable_txos[1], b'', CScript([OP_TRUE, OP_DROP] * 15))
        frozen_block.vtx.extend([valid_tx])
        invalid_tx = self._create_tx(PreviousSpendableOutput(valid_tx, 0), CScript([OP_FALSE]), CScript([OP_TRUE]))
        frozen_block.vtx.extend([invalid_tx])
        self.chain.update_block(self.block_count-1, [])
        self.submit_block_and_check_tip(node, frozen_block, last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        self.submit_block_and_check_tip(node, self._mine_block(None), last_valid_block.hash)
        frozen_block_block_checked_log_string = f"ConnectBlock {frozen_block.hash} failed \\(blk-bad-inputs"
        assert(not node.check_log(frozen_block_block_checked_log_string))
        self._mine_and_send_block(None, node, False, last_valid_block.hash)
        assert(node.check_log(frozen_block_block_checked_log_string))

    def run_test(self):
        node = self._init()

        send_node = Send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])

        spendable_out_1 = [self.chain.get_spendable_output(), self.chain.get_spendable_output()]
        spendable_out_2 = self.chain.get_spendable_output()
        spendable_out_3 = self.chain.get_spendable_output()
        spendable_out_4 = [self.chain.get_spendable_output(), self.chain.get_spendable_output()]
        spendable_out_5 = self.chain.get_spendable_output()
        spendable_out_6 = self.chain.get_spendable_output()
        spendable_out_7 = [self.chain.get_spendable_output(), self.chain.get_spendable_output()]

        self._test_soft_consensus_freeze(spendable_out_1, send_node)
        self._test_soft_consensus_freeze_on_refreeze(spendable_out_2, send_node)
        self._test_soft_consensus_freeze_clear_all(spendable_out_3, send_node)
        self._test_soft_consensus_freeze_invalidate_block(spendable_out_4, send_node)
        self._test_soft_consensus_freeze_submitblock(spendable_out_5, send_node)
        self._test_soft_consensus_freeze_competing_chains(spendable_out_6, send_node)
        self._test_soft_consensus_freeze_invalid_frozen_block(spendable_out_7, send_node)


if __name__ == '__main__':
    FrozenTXOSoftConsensusFreeze().main()
