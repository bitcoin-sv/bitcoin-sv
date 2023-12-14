#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
- Send tx1 that spends transaction output that will be frozen later.
- Send tx2 that spends output from tx1.
- Freeze output in parent transaction.
- Make sure that all child transactions are removed from mempool for both journaling
  and legacy block assembler.
- Unfreeze previously frozen output in parent transaction at height on both consensus and policy blacklist.
- Send tx3 that spends now unfrozen output.
- Send tx4 that spends output from tx3.
- Make sure both transactions appear in mempool.
- Force reorg to lower height so that output in parent transaction becomes frozen again.
- Make sure tx3 and tx4 are removed from mempool for both journaling and legacy block assembler.
- Unfreeze all outputs on both nodes.
- Freeze output in parent transaction on policy blacklist on node0 but not on node1.
- Send tx1a that spends not frozen output to node1.
- Send tx2a that spends output from tx1a to node1.
- Generate a block on node1 and make sure this block becomes tip on both nodes.
- Force reorg to lower height on both nodes.
- Make sure transactions do not appear in mempool of node0.
- Make sure transactions do appear in mempool of node1.
"""
import threading
import glob
import re
import time

from test_framework.util import (
    assert_equal,
    p2p_port,
    sync_blocks,
    sync_mempools
)
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    msg_tx
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.script import CScript, OP_TRUE, OP_NOP


class FrozenTXOTransactionMining(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.chain = ChainManager()
        self.extra_args = [["-disablesafemode=1", "-minrelaytxfee=0", "-limitfreerelay=999999", "-minminingtxfee=0", "-blockassembler=legacy"],
                           ["-disablesafemode=1", "-minrelaytxfee=0", "-limitfreerelay=999999", "-minminingtxfee=0", "-blockassembler=journaling"]]
        self.block_count = 0

    def init_(self, nodes_count):
        nodes = []

        for no in range(0, nodes_count):
            # Create a P2P connections
            node = NodeConnCB()
            connection = NodeConn('127.0.0.1', p2p_port(no), self.nodes[no], node)
            node.add_connection(connection)
            nodes.append(node)

        NetworkThread().start()

        for no in range(0, nodes_count):
            # wait_for_verack ensures that the P2P connection is fully up.
            nodes[no].wait_for_verack()

        self.init_chain_(nodes[0], nodes_count)

        return nodes

    def init_chain_(self, leading_node, nodes_count):
        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(self.block_count)
        self.block_count += 1
        self.chain.save_spendable_output()
        leading_node.send_message(msg_block(block))

        for i in range(100):
            block = self.chain.next_block(self.block_count)
            self.block_count += 1
            self.chain.save_spendable_output()
            leading_node.send_message(msg_block(block))

        self.log.info("Waiting for block height 101 via rpc")

        for no in range(0, nodes_count):
            self.nodes[no].waitforblockheight(101)

    def mine_and_send_block_(self, tx, node):
        block = self.chain.next_block(self.block_count)

        self.chain.update_block(self.block_count, [tx])
        node.send_and_ping(msg_block(block))
        self.block_count += 1

        self.log.debug(f"attempted mining block: {block.hash}")

        assert_equal(block.hash, self.nodes[0].getbestblockhash())

    def create_tx_(self, tx_out, unlock, lock):
        unlock_script = b'' if callable(unlock) else unlock
        tx = create_transaction(tx_out.tx, tx_out.n, unlock_script, 1, lock)

        if callable(unlock):
            tx.vin[0].scriptSig = unlock(tx, tx_out.tx)
            tx.calc_sha256()

        return tx

    def check_log(self, node, line_text):
        for line in open(glob.glob(node.datadir + "/regtest/bitcoind.log")[0]):
            if re.search(line_text, line) is not None:
                self.log.debug("Found line in bitcoind.log: %s", line.strip())
                return True
        return False

    def run_test(self):

        (node0, node1) = self.init_(2)

        out = self.chain.get_spendable_output()

        freeze_tx = self.create_tx_(out, b'', CScript([OP_TRUE]))
        self.log.info(f"Mining block with transaction {freeze_tx.hash} whose output will be frozen later")
        self.mine_and_send_block_(freeze_tx, node0)

        spend_frozen_tx = self.create_tx_(PreviousSpendableOutput(freeze_tx, 0), b'', CScript([OP_TRUE]))
        self.log.info(f"Sending transaction {spend_frozen_tx.hash} spending TXO {freeze_tx.hash},0")
        node0.send_and_ping(msg_tx(spend_frozen_tx))

        spend_frozen_tx2 = self.create_tx_(PreviousSpendableOutput(spend_frozen_tx, 0), b'', CScript([OP_TRUE]))
        self.log.info(f"Sending transaction {spend_frozen_tx2.hash} spending TXO {spend_frozen_tx.hash},0")
        node0.send_and_ping(msg_tx(spend_frozen_tx2))

        sync_mempools(self.nodes)

        self.log.info("Checking that transactions were accepted on both nodes")
        for no in range(0, 2):
            mp = self.nodes[no].getrawmempool()
            assert_equal(len(mp), 2)
            assert(spend_frozen_tx.hash in mp and spend_frozen_tx2.hash in mp)

            template_txns = self.nodes[no].getblocktemplate()["transactions"]
            assert_equal(len(template_txns), 2)
            bt = [template_txns[0]['txid'], template_txns[1]['txid']]
            assert(spend_frozen_tx.hash in mp and spend_frozen_tx2.hash in bt)

        current_height = self.nodes[0].getblockcount()
        self.log.info(f"Current height: {current_height}")

        enforce_height = current_height + 2
        self.log.info(f"Freezing TXO {freeze_tx.hash},0 on consensus blacklist at height {enforce_height} on both nodes")
        for no in range(0, 2):
            self.nodes[no].addToConsensusBlacklist({
                "funds": [
                    {
                        "txOut" : {
                            "txId" : freeze_tx.hash,
                            "vout" : 0
                        },
                        "enforceAtHeight": [{"start": enforce_height}],
                        "policyExpiresWithConsensus": False
                    }]
            })

        self.log.info("Checking that both transactions were removed from mempool and block template on both nodes")
        for no in range(0, 2):
            assert_equal(self.nodes[no].getrawmempool(), [])
            assert_equal(self.nodes[no].getblocktemplate()["transactions"], [])

        enforce_stop_height = enforce_height + 1
        self.log.info(f"Unfreezing TXO {freeze_tx.hash},0 from consensus and policy blacklists at height {enforce_stop_height} on both nodes")
        for no in range(0, 2):
            self.nodes[no].addToConsensusBlacklist({
                "funds": [
                    {
                        "txOut" : {
                            "txId" : freeze_tx.hash,
                            "vout" : 0
                        },
                        "enforceAtHeight": [{"start": enforce_height, "stop": enforce_stop_height}],
                        "policyExpiresWithConsensus": True
                    }]
            })

        self.log.info(f"Generating blocks so that mempool reaches height {enforce_stop_height+1}")
        while self.nodes[0].getblockcount() < enforce_stop_height:
            self.nodes[0].generate(1)
        sync_blocks(self.nodes)

        spend_unfrozen_tx3 = self.create_tx_(PreviousSpendableOutput(freeze_tx, 0), b'', CScript([OP_NOP, OP_TRUE]))
        self.log.info(f"Sending transaction {spend_unfrozen_tx3.hash} spending now unfrozen TXO {freeze_tx.hash},0")
        node0.send_and_ping(msg_tx(spend_unfrozen_tx3))

        spend_unfrozen_tx4 = self.create_tx_(PreviousSpendableOutput(spend_unfrozen_tx3, 0), b'', CScript([OP_NOP, OP_TRUE]))
        self.log.info(f"Sending transaction {spend_unfrozen_tx4.hash} spending TXO {spend_unfrozen_tx3.hash},0")
        node0.send_and_ping(msg_tx(spend_unfrozen_tx4))

        sync_mempools(self.nodes)

        self.log.info("Checking that transactions were accepted on both nodes")
        for no in range(0, 2):
            mp = self.nodes[no].getrawmempool()
            assert_equal(len(mp), 2)
            assert(spend_unfrozen_tx3.hash in mp and spend_unfrozen_tx4.hash in mp)

            template_txns = self.nodes[no].getblocktemplate()["transactions"]
            assert_equal(len(template_txns), 2)
            bt = [template_txns[0]['txid'], template_txns[1]['txid']]
            assert(spend_unfrozen_tx3.hash in mp and spend_unfrozen_tx4.hash in bt)

        self.log.info("Invalidating chain tip on both nodes to force reorg back one block")
        for no in range(0, 2):
            self.nodes[no].invalidateblock(self.nodes[no].getbestblockhash())
            assert(self.nodes[no].getblockcount() == enforce_height)

        mempool_scan_check_log_string = "Removing any transactions that spend TXOs, which were previously not considered policy frozen"
        self.log.info("Checking that transactions are still in mempool on both nodes")
        for no in range(0, 2):
            mp = self.nodes[no].getrawmempool()
            assert_equal(len(mp), 2)
            assert(spend_unfrozen_tx3.hash in mp and spend_unfrozen_tx4.hash in mp)

            template_txns = self.nodes[no].getblocktemplate()["transactions"]
            assert_equal(len(template_txns), 2)
            bt = [template_txns[0]['txid'], template_txns[1]['txid']]
            assert(spend_unfrozen_tx3.hash in mp and spend_unfrozen_tx4.hash in bt)

            # bitcoind sould not unnecessarily scan whole mempool to find transactions that spend TXOs, which could become frozen again.
            assert(not self.check_log(self.nodes[no], mempool_scan_check_log_string))

        self.log.info("Invalidating chain tip on both nodes to force reorg back to height where TXO is still frozen")
        for no in range(0, 2):
            self.nodes[no].invalidateblock(self.nodes[no].getbestblockhash())
            assert(self.nodes[no].getblockcount() == enforce_height - 1)

        self.log.info("Checking that both transactions were removed from mempool and block template on both nodes")
        for no in range(0, 2):
            assert_equal(self.nodes[no].getrawmempool(), [])
            assert_equal(self.nodes[no].getblocktemplate()["transactions"], [])

            # bitcoind now should scan whole mempool.
            assert(self.check_log(self.nodes[no], mempool_scan_check_log_string))

        self.log.info("Unfreezing all frozen outputs on both nodes")
        for no in range(0, 2):
            self.nodes[no].invalidateblock(self.nodes[no].getbestblockhash())
            result = self.nodes[no].clearBlacklists({"removeAllEntries" : True})
            assert_equal(result["numRemovedEntries"], 1)

        self.log.info(f"Freezing TXO {freeze_tx.hash},0 on policy blacklist on node0 (but not on node1)")
        result = self.nodes[0].addToPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : freeze_tx.hash,
                        "vout" : 0
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        spend_frozen_tx1a = self.create_tx_(PreviousSpendableOutput(freeze_tx, 0), b'', CScript([OP_NOP, OP_NOP, OP_TRUE]))
        self.log.info(f"Sending transaction {spend_frozen_tx1a.hash} spending not frozen TXO {freeze_tx.hash},0 to node1")
        node1.send_and_ping(msg_tx(spend_frozen_tx1a))

        spend_frozen_tx2a = self.create_tx_(PreviousSpendableOutput(spend_frozen_tx1a, 0), b'', CScript([OP_NOP, OP_NOP, OP_TRUE]))
        self.log.info(f"Sending transaction {spend_frozen_tx2a.hash} spending TXO {spend_frozen_tx1a.hash},0 to node1")
        node1.send_and_ping(msg_tx(spend_frozen_tx2a))

        self.log.info("Checking that transactions were accepted on node1")
        mp = self.nodes[1].getrawmempool()
        assert_equal(len(mp), 2)
        assert(spend_frozen_tx1a.hash in mp and spend_frozen_tx2a.hash in mp)
        time.sleep(6) # need to wait >5s for the block assembler to create new block
        template_txns = self.nodes[1].getblocktemplate()["transactions"]
        assert_equal(len(template_txns), 2)
        bt = [template_txns[0]['txid'], template_txns[1]['txid']]
        assert(spend_frozen_tx1a.hash in mp and spend_frozen_tx2a.hash in bt)

        self.log.info("Checking that transactions are not present in mempool on node0")
        assert_equal(self.nodes[0].getrawmempool(), [])

        self.log.info("Generate block that contains both transactions on node1")
        height_before_block_spending_policy_frozen_txo = self.nodes[1].getblockcount()
        hash_block_spending_policy_frozen_txo = self.nodes[1].generate(1)[0]
        sync_blocks(self.nodes)
        for no in range(0, 2):
            assert(self.nodes[no].getblockcount() == height_before_block_spending_policy_frozen_txo + 1)
            assert_equal(self.nodes[no].getrawmempool(), [])
            assert_equal(self.nodes[no].getblocktemplate()["transactions"], [])

        self.log.info("Invalidating chain tip on both nodes to force reorg back one block")
        for no in range(0, 2):
            self.nodes[no].invalidateblock(hash_block_spending_policy_frozen_txo)
            assert(self.nodes[no].getblockcount() == height_before_block_spending_policy_frozen_txo)

        self.log.info("Checking that transactions are not present in mempool on node0")
        assert_equal(self.nodes[0].getrawmempool(), [])
        assert_equal(self.nodes[0].getblocktemplate()["transactions"], [])

        self.log.info("Checking that transactions were put back to mempool on node1")
        mp = self.nodes[1].getrawmempool()
        assert_equal(len(mp), 2)
        assert(spend_frozen_tx1a.hash in mp and spend_frozen_tx2a.hash in mp)
        template_txns = self.nodes[1].getblocktemplate()["transactions"]
        assert_equal(len(template_txns), 2)
        bt = [template_txns[0]['txid'], template_txns[1]['txid']]
        assert(spend_frozen_tx1a.hash in mp and spend_frozen_tx2a.hash in bt)


if __name__ == '__main__':
    FrozenTXOTransactionMining().main()
