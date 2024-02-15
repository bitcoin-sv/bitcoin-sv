#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test soft consensus freeze:
 - unlimited freeze
 - minimal freeze for one block

Make sure that setting softconsensusfreezeduration attribute to 0 freezes block
indefinitely and that 1 freezes just for one block.
"""
from soft_consensus_freeze_base import Send_node, SoftConsensusFreezeBase

from test_framework.test_framework import ChainManager
from test_framework.blocktools import PreviousSpendableOutput
from test_framework.mininode import msg_headers
from test_framework.script import CScript, OP_TRUE


class FrozenTXOSoftConsensusFreeze(SoftConsensusFreezeBase):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1", "-minrelaytxfee=0", "-limitfreerelay=999999", "-softconsensusfreezeduration=0"]]
        self.block_count = 0

    def _test_minimal_freeze(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze checks (freeze for one block)")

        node.restart_node(["-whitelist=127.0.0.1", "-minrelaytxfee=0", "-limitfreerelay=999999", "-softconsensusfreezeduration=1"])

        frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out)

        # block is rejected as consensus freeze is in effect for parent transaction
        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        self._mine_and_check_rejected(node, spend_frozen_tx)

        # this block will still be frozen
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        # blocks are unfrozen
        self._mine_and_send_block(None, node)

    def _test_unlimited_freeze(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze checks (unlimited)")

        node.restart_node()

        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx(node, spendable_out)

        last_valid_tip_hash = node.rpc.getbestblockhash()

        # this block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx(PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]))
        self.log.info(f"Mining block with transaction {first_spend_frozen_tx.hash} spending TXO {first_spend_frozen_tx.vin[0].prevout.hash:064x},{first_spend_frozen_tx.vin[0].prevout.n}")
        first_frozen_block = self._mine_block(first_spend_frozen_tx)

        self.log.info(f"Mining descendants of block {first_frozen_block.hash}")
        subsequent_frozen_blocks = []
        for i in range(14): # since we cannot check unlimited number of blocks, number 14 was chosen arbitrarily and we assume it is close enough to infinity for the purpose of this test
            subsequent_frozen_blocks.append(self._mine_block(None))

        # Send block headers first to check that they are also all correctly marked as frozen after the actual block is received.
        self.log.info(f"Sending headers for block {first_frozen_block.hash} and descendants")
        msg_hdrs = msg_headers()
        msg_hdrs.headers.append(first_frozen_block)
        msg_hdrs.headers.extend(subsequent_frozen_blocks)
        node.p2p.send_and_ping(msg_hdrs)
        # check that headers were received
        for tip in node.rpc.getchaintips():
            if tip["status"] == "active" and tip["hash"] == last_valid_tip_hash:
                continue
            if tip["status"] == "headers-only" and tip["hash"] == subsequent_frozen_blocks[-1].hash:
                continue
            assert False, "Unexpected tip: "+str(tip)

        self.log.info(f"Sending block {first_frozen_block.hash} and checking that it is rejected")
        node.send_block(first_frozen_block, last_valid_tip_hash, True)
        assert(node.check_frozen_tx_log(first_frozen_block.hash))
        assert(node.check_log("Block was rejected because it included a transaction, which tried to spend a frozen transaction output.*"+first_frozen_block.hash))

        self.log.info(f"Sending descendants of block {first_frozen_block.hash} and checking that they do not become tip")
        for b in subsequent_frozen_blocks:
            node.send_block(b, last_valid_tip_hash, False)

    def run_test(self):
        node = self._init()

        send_node = Send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])

        spendable_out_1 = self.chain.get_spendable_output()
        spendable_out_2 = self.chain.get_spendable_output()

        self._test_minimal_freeze(spendable_out_1, send_node)
        self._test_unlimited_freeze(spendable_out_2, send_node)


if __name__ == '__main__':
    FrozenTXOSoftConsensusFreeze().main()
