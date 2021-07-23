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

See comments and log entries in above functions for detailed description of steps performed in each test.
"""
import threading
import glob
import re

from test_framework.util import (
    assert_equal,
    p2p_port,
    assert_raises_rpc_error
)
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    msg_tx,
    ToHex
)

from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.script import CScript, OP_NOP, OP_TRUE, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.key import CECKey
from test_framework.address import key_to_p2pkh

class Send_node():
    rejected_blocks = []

    def __init__(self, tmpdir, log, node_no, p2p_connection, rpc_connection):
        self.p2p = p2p_connection
        self.rpc = rpc_connection
        self.node_no = node_no
        self.tmpdir = tmpdir
        self.log = log

        def on_reject(conn, msg):
            self.rejected_blocks.append(msg)
        self.p2p.connection.cb.on_reject = on_reject

    def send_block(self, block, expect_tip, expect_reject = False):
        self.p2p.send_and_ping(msg_block(block))

        if expect_reject:
            assert_equal(expect_tip, self.rpc.getbestblockhash())
            self.reject_check(block)
            assert(self.check_frozen_tx_log(block.hash))
        else:
            assert_equal(expect_tip, self.rpc.getbestblockhash())
            assert(self.check_frozen_tx_log(block.hash) == False)

    def reject_check(self, block):
        self.p2p.wait_for_reject()

        assert_equal(len(self.rejected_blocks), 1)
        assert_equal(self.rejected_blocks[0].data, block.sha256)
        assert_equal(self.rejected_blocks[0].reason, b'bad-txns-inputs-frozen')

        self.rejected_blocks = []
        self.p2p.message_count["reject"] = 0

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

class FrozenTXOSoftConsensusFreeze(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1", "-minrelaytxfee=0", "-limitfreerelay=999999"]]
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
        tx = create_transaction(tx_out.tx, tx_out.n, unlock_script, 1, lock)

        if callable(unlock):
            tx.vin[0].scriptSig = unlock(tx, tx_out.tx)
            tx.calc_sha256()

        return tx

    def _mine_and_send_block(self, tx, node, expect_reject = False, expect_tip = None):
        block = self.chain.next_block(self.block_count)

        txs = []
        if tx:
            if isinstance(tx, list):
                txs = tx
            else:
                txs = [tx]

        self.chain.update_block(self.block_count, txs)

        self.log.info(f"attempting mining block: {block.hash}")

        expect_tip = expect_tip if expect_tip != None else block.hash

        node.send_block(block, expect_tip, expect_reject)
        self.block_count += 1

        return block

    # Helper to mine block with tx and check it is rejected because of frozen inputs
    def _mine_and_check_rejected(self, node, tx):
        if isinstance(tx, list):
            self.log.info("Mining block")
            for txn in tx:
                self.log.info(f"with transaction {txn.hash} spending TXO {txn.vin[0].prevout.hash:064x},{txn.vin[0].prevout.n}")
            self.log.info(" and checking that they are rejected")
        else:
            self.log.info(f"Mining block with transaction {tx.hash} spending TXO {tx.vin[0].prevout.hash:064x},{tx.vin[0].prevout.n} and checking that it is rejected")

        old_tip = self.chain.tip
        block = self._mine_and_send_block(tx, node, True, node.rpc.getbestblockhash())
        assert_equal(node.rpc.getbestblockhash(), old_tip.hash)
        assert(node.check_frozen_tx_log(self.chain.tip.hash));
        assert(node.check_log("Block was rejected because it included a transaction, which tried to spend a frozen transaction output.*"+self.chain.tip.hash));

        return block

    def _create_tx_mine_block_and_freeze_tx(self, node, spendable_out, stop = None):
        freeze_tx = self._create_tx(spendable_out, b'', CScript([OP_TRUE]))
        self.log.info(f"Mining block with transaction {freeze_tx.hash} whose output will be frozen later")
        self._mine_and_send_block(freeze_tx, node)

        if stop != None:
            enforce_at_height = [{"start": 0, "stop": stop}]
        else:
            enforce_at_height = [{"start": 0}]

        self.log.info(f"Freezing TXO {freeze_tx.hash},0 on consensus blacklist {stop}")
        result=node.rpc.addToConsensusBlacklist({
            "funds": [
            {
                "txOut" : {
                    "txId" : freeze_tx.hash,
                    "vout" : 0
                },
                "enforceAtHeight": enforce_at_height,
                "policyExpiresWithConsensus": False
            }]
        });
        assert_equal(result["notProcessed"], [])

        return freeze_tx

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

        # all blocks are unfrozen - this proves that the old duration remained
        # in place
        self._mine_and_send_block(None, node)

    def _test_soft_consensus_freeze_two_ranges(self, spendable_out, node):
        self.log.info("*** Performing soft consensus freeze on two ranges")

        freeze_until_1 = node.rpc.getblockcount() + 3
        first_frozen_tx = self._create_tx_mine_block_and_freeze_tx( node, spendable_out, freeze_until_1 )

        freeze_until_2 = freeze_until_1 + 10;

        # limit the duration of freeze and make sure it is honored
        self.log.info(f"Freezing TXO {first_frozen_tx.hash} on consensus blacklist on ranges between height {freeze_until_1 + 4} - {freeze_until_2}")
        result=node.rpc.addToConsensusBlacklist({
            "funds": [
            {
                "txOut" : {
                    "txId" : first_frozen_tx.hash,
                    "vout" : 0
                },
                "enforceAtHeight":
                    [
                        {"start": 0, "stop": freeze_until_1},
                        {"start": freeze_until_1 + 4, "stop": freeze_until_2}
                    ],
                "policyExpiresWithConsensus": False
            }]
        });
        assert_equal(result["notProcessed"], [])

        # block is rejected as consensus freeze is in effect for parent transaction
        first_spend_frozen_tx = self._create_tx( PreviousSpendableOutput(first_frozen_tx, 0), b'', CScript([OP_TRUE]) )
        first_frozen_block = self._mine_and_check_rejected( node, first_spend_frozen_tx )

        # block is expected to still be frozen
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())
        self._mine_and_send_block(None, node, False, node.rpc.getbestblockhash())

        # all blocks are unfrozen - this proves that even though there is some
        # area between frozen ranges that is not frozen, we still compare
        # the furthest stop with freeze setting in configuration
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

        # first block is unfrozen but since height restriction is not met due
        # to second block being frozen, we remain on the old tip
        self._mine_and_send_block(None, node, False, block_before_frozen_blocks_hash)
        node.reject_check( second_frozen_block )

        # all blocks are unfrozen
        block = self._mine_and_send_block(None, node)

        node.rpc.invalidateblock(block.hash)

        assert( block_before_frozen_blocks_hash == node.rpc.getbestblockhash() )

    def run_test(self):
        node = self._init()

        send_node = Send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])

        spendable_out_1 = [ self.chain.get_spendable_output(), self.chain.get_spendable_output() ]
        spendable_out_2 = self.chain.get_spendable_output()
        spendable_out_3 = self.chain.get_spendable_output()
        spendable_out_4 = self.chain.get_spendable_output()
        spendable_out_5 = [ self.chain.get_spendable_output(), self.chain.get_spendable_output() ]

        self._test_soft_consensus_freeze( spendable_out_1, send_node )
        self._test_soft_consensus_freeze_on_refreeze( spendable_out_2, send_node )
        self._test_soft_consensus_freeze_clear_all( spendable_out_3, send_node )
        self._test_soft_consensus_freeze_two_ranges( spendable_out_4, send_node )
        self._test_soft_consensus_freeze_invalidate_block( spendable_out_5, send_node )

if __name__ == '__main__':
    FrozenTXOSoftConsensusFreeze().main()
