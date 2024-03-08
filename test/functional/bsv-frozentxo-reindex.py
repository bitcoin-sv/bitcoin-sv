#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that blocks that contain consensus frozen transactions are not re-added to
active chain after reindex while those with policy frozen transactions are.
"""

import glob
import re

from test_framework.util import (
    assert_equal,
    p2p_port,
    wait_until,
    count_log_msg
)

from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    ToHex
)

from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.script import CScript, OP_TRUE


class Send_node():
    def __init__(self, tmpdir, log, node_no, p2p_connection, rpc_connection):
        self.p2p = p2p_connection
        self.rpc = rpc_connection
        self.node_no = node_no
        self.tmpdir = tmpdir
        self.log = log

    def send_block(self, block, expect_reject = False):
        self.rpc.submitblock(ToHex(block))

        if expect_reject:
            assert(self.check_frozen_tx_log(block.hash))
        else:
            assert_equal(block.hash, self.rpc.getbestblockhash())
            assert(self.check_frozen_tx_log(block.hash) == False)

    def check_frozen_tx_log(self, hash):
        for line in open(glob.glob(self.tmpdir + f"/node{self.node_no}" + "/regtest/blacklist.log")[0]):
            if hash in line:
                self.log.debug("Found line in blacklist.log: %s", line)
                return True
        return False

    def check_log(self, line_text):
        for line in open(glob.glob(self.tmpdir + f"/node{self.node_no}" + "/regtest/bitcoind.log")[0]):
            if re.search(line_text, line) is not None:
                self.log.debug("Found line in bitcoind.log: %s", line.strip())
                return True
        return False


class FrozenTXOReindex(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.block_count = 0

    def _init(self):
        node_no = 0

        # Create a P2P connections
        node = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[node_no], node)
        node.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node.wait_for_verack()

        self.chain.set_genesis_hash(int(self.nodes[node_no].getbestblockhash(), 16))
        block = self.chain.next_block(self.block_count)
        self.block_count += 1
        self.chain.save_spendable_output()
        node.send_message(msg_block(block))

        for i in range(100):
            block = self.chain.next_block(self.block_count)
            self.block_count += 1
            self.chain.save_spendable_output()
            node.send_message(msg_block(block))

        self.log.info("Waiting for block height 101 via rpc")
        self.nodes[node_no].waitforblockheight(101)

        return node

    def _create_tx(self, tx_out, unlock, lock):
        unlock_script = b'' if callable(unlock) else unlock

        return create_transaction(tx_out.tx, tx_out.n, unlock_script, 1, lock)

    def _mine_and_send_block(self, tx, node, expect_reject = False):
        block = self.chain.next_block(self.block_count)

        self.chain.update_block(self.block_count, [tx] if tx else [])

        self.log.debug(f"attempting mining block: {block.hash}")

        node.send_block(block, expect_reject)
        self.block_count += 1

        return block.hash

    def _remove_last_block(self):
        # remove last block from chain manager
        del self.chain.block_heights[self.chain.blocks[self.block_count-1].sha256]
        del self.chain.blocks[self.block_count-1]
        self.block_count -= 1
        self.chain.set_tip(self.block_count-1)

    def _mine_and_check_rejected(self, tx, node):
        self.log.info(f"Mining block with transaction {tx.hash} spending TXO {tx.vin[0].prevout.hash:064x},{tx.vin[0].prevout.n} and checking that it is rejected")
        old_tip = self.chain.tip
        rejected_block_hash = self._mine_and_send_block(tx, node, True)
        assert_equal(node.rpc.getbestblockhash(), old_tip.hash)
        assert(node.check_frozen_tx_log(self.chain.tip.hash))
        assert(node.check_log("Block was rejected because it included a transaction, which tried to spend a frozen transaction output.*"+self.chain.tip.hash))

        # remove rejected block from test node - the only remaining copy after this point is on remote node disk
        self._remove_last_block()

        return rejected_block_hash

    def _create_policy_freeze_block(self, spendable_out, node):
        freeze_tx = self._create_tx(spendable_out, b'', CScript([OP_TRUE]))
        self.log.info(f"Mining block with transaction {freeze_tx.hash} whose output will be frozen later")
        self._mine_and_send_block(freeze_tx, node)

        self.log.info(f"Freezing TXO {freeze_tx.hash},0 on policy blacklist")
        result = node.rpc.addToPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : freeze_tx.hash,
                        "vout" : 0
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(freeze_tx, 0), b'', CScript([OP_TRUE]))
        self.log.info(f"Mining block with transaction {spend_frozen_tx.hash} spending frozen TXO {freeze_tx.hash},0 and checking that is accepted")
        self._mine_and_send_block(spend_frozen_tx, node)
        # block is accepted as consensus freeze is not in effect
        assert_equal(node.rpc.getbestblockhash(), self.chain.tip.hash)

    def _create_consensus_freeze_block(self, spendable_out, node):
        freeze_tx = self._create_tx(spendable_out, b'', CScript([OP_TRUE]))
        self.log.info(f"Mining block with transaction {freeze_tx.hash} whose output will be frozen later")
        self._mine_and_send_block(freeze_tx, node)

        self.log.info(f"Freezing TXO {freeze_tx.hash},0 on consensus blacklist")
        result=node.rpc.addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : freeze_tx.hash,
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0}],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])

        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(freeze_tx, 0), b'', CScript([OP_TRUE]))

        # block is rejected as consensus freeze is in effect
        rejected_block_hash = self._mine_and_check_rejected(spend_frozen_tx, node)

        return (freeze_tx.hash, rejected_block_hash)

    def run_test(self):

        node = self._init()

        out_policy_freeze_txo = self.chain.get_spendable_output()
        out_consensus_freeze_txo = self.chain.get_spendable_output()

        send_node = Send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])
        self._create_policy_freeze_block(out_policy_freeze_txo, send_node)
        [freeze_tx_hash, rejected_block_hash] = self._create_consensus_freeze_block(out_consensus_freeze_txo, send_node)
        node_chain_info = send_node.rpc.getblockchaininfo()
        old_tip_hash = node_chain_info['bestblockhash']
        old_tip_height = node_chain_info['blocks']

        assert(rejected_block_hash != old_tip_hash)

        # Make sure that we get to the same height:
        # best block with transactions policy frozen - should get to this point
        # best block with transactions consensus frozen - should not get to this block
        self.stop_node(0)
        self.start_node(0, extra_args=["-reindex=1"])

        # Waiting for last valid block. Waiting just for old_tip_height is not enough becuase next block (to be rejected) may not be processed yet.
        send_node.rpc.waitforblockheight(old_tip_height)
        # Wait for next block to be rejected. We need to wait for the second occurrence of the same log because one rejection happens before
        wait_until(lambda: count_log_msg(self, "InvalidChainFound: invalid block="+rejected_block_hash+"  height=105", "/node0") == 2, timeout=5)

        assert_equal(send_node.rpc.getbestblockhash(), old_tip_hash)

        # Unfreeze and reconsider block to show that the block was still stored on disk
        result = self.nodes[0].clearBlacklists({"removeAllEntries": True})
        assert_equal(result["numRemovedEntries"], 2)

        self.stop_node(0)
        self.start_node(0, extra_args=["-reindex=1"])

        send_node.rpc.waitforblockheight(old_tip_height + 1)

        self.log.info(send_node.rpc.getblockchaininfo())
        assert_equal(send_node.rpc.getbestblockhash(), rejected_block_hash)


if __name__ == '__main__':
    FrozenTXOReindex().main()
