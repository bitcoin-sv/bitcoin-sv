#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Base functions that are used by bsv-frozentxo-soft-consensus-freeze* tests.
"""

import threading
import glob
import re

from test_framework.util import (
    assert_equal,
    p2p_port
)
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block
)

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import create_transaction
from test_framework.script import CScript, OP_TRUE


class Send_node():
    rejected_blocks = []

    def __init__(self, tmpdir, log, node_no, p2p_connection, rpc_connection):
        self.p2p = p2p_connection
        self.rpc = rpc_connection
        self.node_no = node_no
        self.tmpdir = tmpdir
        self.log = log
        self._register_on_reject()

    def _register_on_reject(self):
        def on_reject(conn, msg):
            self.rejected_blocks.append(msg)
        self.p2p.connection.cb.on_reject = on_reject

    def restart_node(self, extra_args=None):
        self.log.info("Restarting node ...")
        self.rpc.stop_node()
        self.rpc.wait_until_stopped()
        self.rpc.start(True, extra_args)
        self.rpc.wait_for_rpc_connection()
        self.p2p = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.rpc, self.p2p)
        NetworkThread().start()
        self.p2p.add_connection(connection)
        self.p2p.wait_for_verack()
        self._register_on_reject()

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


class SoftConsensusFreezeBase(BitcoinTestFramework):

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

    def _mine_block(self, tx):
        block = self.chain.next_block(self.block_count)

        txs = []
        if tx:
            if isinstance(tx, list):
                txs = tx
            else:
                txs = [tx]

        self.chain.update_block(self.block_count, txs)

        self.log.info(f"attempting mining block: {block.hash}")

        self.block_count += 1

        return block

    def _mine_and_send_block(self, tx, node, expect_reject = False, expect_tip = None):
        block = self._mine_block(tx)

        expect_tip = expect_tip if expect_tip != None else block.hash

        node.send_block(block, expect_tip, expect_reject)

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
        assert(node.check_frozen_tx_log(self.chain.tip.hash))
        assert(node.check_log("Block was rejected because it included a transaction, which tried to spend a frozen transaction output.*"+self.chain.tip.hash))

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
        })
        assert_equal(result["notProcessed"], [])

        return freeze_tx

    def submit_block_and_check_tip(self, node, block, expect_tip):
        node.rpc.submitblock(block.serialize().hex())
        if expect_tip == None:
            expect_tip = block.hash
        assert_equal(expect_tip, node.rpc.getbestblockhash())

    def get_chain_tip(self):
        block_number = self.block_count - 1
        assert self.chain.blocks[block_number] == self.chain.tip
        return block_number

    def set_chain_tip(self, block_number):
        self.chain.set_tip(block_number)
