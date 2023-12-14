#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
test_policy_freeze_: Checks that policy freeze is honored.
test_consensus_freeze_: Checks that consensus freeze is honored.

The test runs by using p2p connection for sending first and then re-runs with rpc.

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
    def __init__(self, tmpdir, log, node_no, p2p_connection, rpc_connection):
        self.p2p = p2p_connection
        self.rpc = rpc_connection
        self.node_no = node_no
        self.tmpdir = tmpdir
        self.log = log

    def send_block(self, block, expect_reject = False):
        raise NotImplementedError()

    def send_tx(self, tx, expect_reject = False):
        raise NotImplementedError()

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


class RPC_send_node(Send_node):
    def __init__(self, tmpdir, log, node_no, p2p_connection, rpc_connection):
        super().__init__(tmpdir, log, node_no, p2p_connection, rpc_connection)

    def send_block(self, block, expect_reject = False):
        self.rpc.submitblock(ToHex(block))

        if expect_reject:
            assert(self.check_frozen_tx_log(block.hash))
        else:
            assert_equal(block.hash, self.rpc.getbestblockhash())
            assert(self.check_frozen_tx_log(block.hash) == False)

    def send_tx(self, tx, expect_reject = False):
        if expect_reject:
            assert_raises_rpc_error(
                -26,
                "bad-txns-inputs-frozen",
                self.rpc.sendrawtransaction,
                ToHex(tx))
        else:
            self.rpc.sendrawtransaction(ToHex(tx))


class P2P_send_node(Send_node):
    rejected_txs = []

    def __init__(self, tmpdir, log, node_no, p2p_connection, rpc_connection):
        super().__init__(tmpdir, log, node_no, p2p_connection, rpc_connection)

        def on_reject(conn, msg):
            self.rejected_txs.append(msg)
        self.p2p.connection.cb.on_reject = on_reject

    def send_block(self, block, expect_reject = False):
        self.p2p.send_and_ping(msg_block(block))

        if expect_reject:
            self._reject_check(block)
            assert(self.check_frozen_tx_log(block.hash))
        else:
            assert_equal(block.hash, self.rpc.getbestblockhash())
            assert(self.check_frozen_tx_log(block.hash) == False)

    def send_tx(self, tx, expect_reject = False):
        self.p2p.send_and_ping(msg_tx(tx))

        if expect_reject:
            self._reject_check(tx)

    def _reject_check(self, tx):
        self.p2p.wait_for_reject()

        assert_equal(len(self.rejected_txs), 1)
        assert_equal(self.rejected_txs[0].data, tx.sha256)
        assert_equal(self.rejected_txs[0].reason, b'bad-txns-inputs-frozen')

        self.rejected_txs = []
        self.p2p.message_count["reject"] = 0


class FrozenTXOTransactionFreeze(BitcoinTestFramework):

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

    def _mine_and_send_block(self, tx, node, expect_reject = False):
        block = self.chain.next_block(self.block_count)

        self.chain.update_block(self.block_count, [tx] if tx else [])

        self.log.debug(f"attempting mining block: {block.hash}")

        node.send_block(block, expect_reject)
        self.block_count += 1

    def _remove_last_block(self):
        # remove last block from chain manager
        del self.chain.block_heights[self.chain.blocks[self.block_count-1].sha256]
        del self.chain.blocks[self.block_count-1]
        self.block_count -= 1
        self.chain.set_tip(self.block_count-1)

    def _test_policy_freeze(self, spendable_out, node):
        self.log.info("*** Performing policy freeze checks")

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
        self.log.info(f"Sending transaction spending frozen TXO {freeze_tx.hash},0 and checking that it is rejected")
        # must not be accepted as parent transaction is frozen
        node.send_tx(spend_frozen_tx, True)
        assert_equal(node.rpc.getrawmempool(), [])
        assert(node.check_frozen_tx_log(spend_frozen_tx.hash))
        assert(node.check_log("Transaction was rejected because it tried to spend a frozen transaction output.*"+spend_frozen_tx.hash))

        self.log.info(f"Mining block with transaction {spend_frozen_tx.hash} spending frozen TXO {freeze_tx.hash},0 and checking that is accepted")
        self._mine_and_send_block(spend_frozen_tx, node)
        # block is still accepted as consensus freeze is not in effect
        assert_equal(node.rpc.getbestblockhash(), self.chain.tip.hash)

        spend_frozen_tx2 = self._create_tx(PreviousSpendableOutput(spend_frozen_tx, 0), b'', CScript([OP_TRUE]))
        self.log.info(f"Sending transaction {spend_frozen_tx2.hash} spending TXO {spend_frozen_tx.hash},0 that is not yet frozen")
        node.send_tx(spend_frozen_tx2)
        assert_equal(node.rpc.getrawmempool(), [spend_frozen_tx2.hash])

        self.log.info(f"Freezing TXO {spend_frozen_tx.hash},0 on policy blacklist")
        result = node.rpc.addToPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : spend_frozen_tx.hash,
                        "vout" : 0
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info(f"Checking that transaction {spend_frozen_tx2.hash} is removed from mempool")
        assert_equal(node.rpc.getrawmempool(), [])
        assert(node.check_frozen_tx_log(spend_frozen_tx2.hash))
        assert(node.check_log("Transaction was rejected because it tried to spend a frozen transaction output.*"+spend_frozen_tx2.hash))

        self.log.info(f"Unfreezing TXO {spend_frozen_tx.hash},0 from policy blacklist")
        result = node.rpc.removeFromPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : spend_frozen_tx.hash,
                        "vout" : 0
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info(f"Sending transaction {spend_frozen_tx2.hash} again and checking that it is accepted")
        node.send_tx(spend_frozen_tx2)
        assert_equal(node.rpc.getrawmempool(), [spend_frozen_tx2.hash])

        self.log.info(f"Checking that transaction {spend_frozen_tx2.hash} is removed from mempool if TXO is re-frozen")
        result = node.rpc.addToPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : spend_frozen_tx.hash,
                        "vout" : 0
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])
        assert_equal(node.rpc.getrawmempool(), [])

    def _test_consensus_freeze(self, spendable_out, node):
        self.log.info("*** Performing consensus freeze checks")

        # Helper to send tx and check it is rejected because of frozen inputs
        def SendTxAndCheckRejected(tx):
            self.log.info(f"Sending transaction {tx.hash} spending TXO {tx.vin[0].prevout.hash:064x},{tx.vin[0].prevout.n} and checking that it is rejected")
            node.send_tx(tx, True)
            assert_equal(node.rpc.getrawmempool(), [])
            assert(node.check_frozen_tx_log(tx.hash))
            assert(node.check_log("Transaction was rejected because it tried to spend a frozen transaction output.*"+tx.hash))

        # Helper to send tx and check it is accepted
        def SendTxAndCheckAccepted(tx):
            self.log.info(f"Sending transaction {tx.hash} spending TXO {tx.vin[0].prevout.hash:064x},{tx.vin[0].prevout.n} and checking that it is accepted")
            node.send_tx(tx)
            assert_equal(node.rpc.getrawmempool(), [tx.hash])

        # Helper to mine block with tx and check it is rejected because of frozen inputs
        def MineAndCheckRejected(tx):
            self.log.info(f"Mining block with transaction {tx.hash} spending TXO {tx.vin[0].prevout.hash:064x},{tx.vin[0].prevout.n} and checking that it is rejected")
            old_tip = self.chain.tip
            self._mine_and_send_block(tx, node, True)
            assert_equal(node.rpc.getbestblockhash(), old_tip.hash)
            assert(node.check_frozen_tx_log(self.chain.tip.hash))
            assert(node.check_log("Block was rejected because it included a transaction, which tried to spend a frozen transaction output.*"+self.chain.tip.hash))
            self._remove_last_block()

        # Helper to mine block with tx and check it is accepted
        def MineAndCheckAccepted(tx):
            self.log.info(f"Mining block with transaction {tx.hash} spending TXO {tx.vin[0].prevout.hash:064x},{tx.vin[0].prevout.n} and checking that is accepted")
            self._mine_and_send_block(tx, node)
            assert_equal(node.rpc.getbestblockhash(), self.chain.tip.hash)

        def MineEmptyBlock():
            self.log.info(f"Mining block with no transactions to increase height")
            self._mine_and_send_block(None, node)
            assert_equal(node.rpc.getbestblockhash(), self.chain.tip.hash)

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

        # must not be accepted as parent transaction is frozen
        spend_frozen_tx = self._create_tx(PreviousSpendableOutput(freeze_tx, 0), b'', CScript([OP_TRUE]))
        SendTxAndCheckRejected(spend_frozen_tx)

        # block is rejected as consensus freeze is in effect
        MineAndCheckRejected(spend_frozen_tx)

        current_height = node.rpc.getblockcount()
        self.log.info(f"Current height: {current_height}")
        enforce_height = current_height + 2
        self.log.info(f"Freezing TXO {freeze_tx.hash},0 on consensus blacklist at height {enforce_height}")
        result=node.rpc.addToConsensusBlacklist({
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
        assert_equal(result["notProcessed"], [])

        # must not be accepted even if consensus blacklist is not yet enforced
        spend_frozen_tx2 = self._create_tx(PreviousSpendableOutput(freeze_tx, 0), b'', CScript([OP_TRUE, OP_NOP]))
        SendTxAndCheckRejected(spend_frozen_tx2)

        # block is accepted as consensus freeze is not yet enforced at this height
        MineAndCheckAccepted(spend_frozen_tx2)

        self.log.info(f"Freezing TXO {spend_frozen_tx2.hash},0 on consensus blacklist at height {enforce_height}")
        result=node.rpc.addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : spend_frozen_tx2.hash,
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": enforce_height}],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])

        # block is rejected as consensus freeze is enforced at this height
        spend_frozen_tx3 = self._create_tx(PreviousSpendableOutput(spend_frozen_tx2, 0), b'', CScript([OP_TRUE]))
        MineAndCheckRejected(spend_frozen_tx3)

        self.log.info(f"Unfreezing TXO {spend_frozen_tx2.hash},0 from consensus blacklist at height {enforce_height+2}")
        result=node.rpc.addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : spend_frozen_tx2.hash,
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": enforce_height, "stop": enforce_height+2}],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])

        MineEmptyBlock()

        # block is rejected as consensus freeze is still enforced at this height
        spend_frozen_tx3_1 = self._create_tx(PreviousSpendableOutput(spend_frozen_tx2, 0), b'', CScript([OP_TRUE, OP_NOP]))
        MineAndCheckRejected(spend_frozen_tx3_1)

        MineEmptyBlock()

        # must not be accepted because policy blacklist enforcement does not expire with consensus
        spend_frozen_tx3_2 = self._create_tx(PreviousSpendableOutput(spend_frozen_tx2, 0), b'', CScript([OP_TRUE, OP_NOP, OP_NOP]))
        SendTxAndCheckRejected(spend_frozen_tx3_2)

        self.log.info(f"Unfreezing TXO {spend_frozen_tx2.hash},0 from consensus and policy blacklist at height {enforce_height+2}")
        result=node.rpc.addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : spend_frozen_tx2.hash,
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": enforce_height, "stop": enforce_height+2}],
                    "policyExpiresWithConsensus": True
                }]
        })
        assert_equal(result["notProcessed"], [])

        # must be accepted because policy blacklist enforcement expires with consensus at this height
        spend_frozen_tx3_3 = self._create_tx(PreviousSpendableOutput(spend_frozen_tx2, 0), b'', CScript([OP_TRUE, OP_NOP, OP_NOP, OP_NOP]))
        SendTxAndCheckAccepted(spend_frozen_tx3_3)

        # block is accepted as consensus freeze is not enforced anymore at this height
        spend_frozen_tx3_4 = self._create_tx(PreviousSpendableOutput(spend_frozen_tx2, 0), b'', CScript([OP_TRUE, OP_NOP, OP_NOP, OP_NOP, OP_NOP]))
        MineAndCheckAccepted(spend_frozen_tx3_4)

        self.log.info("*** Performing consensus freeze checks with several block height enforcement intervals")

        # Helper to freeze output 0 of given tx on heights [h+1,h+3), [h+5,h+7), where h is current block height
        # and return a tx that spends that output.
        def FreezeTXO0(tx):
            h = node.rpc.getblockcount()
            self.log.info(f"Current height: {h}")

            self.log.info(f"Freezing TXO {tx.hash},0 on consensus blacklist at heights [{h+1}, {h+3}), [{h+5}, {h+7})")
            result=node.rpc.addToConsensusBlacklist({
                "funds": [
                    {
                        "txOut" : {
                            "txId" : tx.hash,
                            "vout" : 0
                        },
                        "enforceAtHeight": [{"start": h+1, "stop": h+3}, {"start": h+5, "stop": h+7}],
                        "policyExpiresWithConsensus": False
                    }]
            })
            assert_equal(result["notProcessed"], [])
            tx2=self._create_tx(PreviousSpendableOutput(tx, 0), b'', CScript([OP_TRUE]))
            self.log.info(f"Creating transaction {tx2.hash} spending TXO {tx.vin[0].prevout.hash:064x},{tx.vin[0].prevout.n}")
            return tx2

        tx = spend_frozen_tx3_4

        # Check first interval
        tx = FreezeTXO0(tx)
        MineAndCheckRejected(tx) # block is rejected in first interval
        MineEmptyBlock()
        MineAndCheckRejected(tx)
        MineEmptyBlock()
        SendTxAndCheckRejected(tx) # tx is rejected because policy freeze also applies in gaps between enforcement intervals
        MineAndCheckAccepted(tx) # block is accepted as consensus freeze is not enforced in a gap between enforcement intervals

        # Same as above, but check the second block in a gap between enforcement intervals
        tx=FreezeTXO0(tx)
        MineAndCheckRejected(tx)
        MineEmptyBlock()
        MineAndCheckRejected(tx)
        MineEmptyBlock()
        MineEmptyBlock()
        SendTxAndCheckRejected(tx)
        MineAndCheckAccepted(tx)

        # Check second interval
        tx=FreezeTXO0(tx)
        MineAndCheckRejected(tx)
        MineEmptyBlock()
        MineAndCheckRejected(tx)
        MineEmptyBlock()
        MineEmptyBlock()
        MineEmptyBlock()
        MineAndCheckRejected(tx) # block is rejected in second interval
        MineEmptyBlock()
        MineAndCheckRejected(tx)
        MineEmptyBlock()
        SendTxAndCheckRejected(tx) # tx is rejected because policy freeze also applies after enforcement intervals if policyExpiresWithConsensus=false
        MineAndCheckAccepted(tx) # block is accepted after the last interval

    def run_test(self):

        node = self._init()

        out_policy_freeze_txo_p2p = self.chain.get_spendable_output()
        out_consensus_freeze_txo_p2p = self.chain.get_spendable_output()

        out_policy_freeze_txo_rpc = self.chain.get_spendable_output()
        out_consensus_freeze_txo_rpc = self.chain.get_spendable_output()

        # p2p send test
        p2p_send_node = P2P_send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])
        self._test_policy_freeze(out_policy_freeze_txo_p2p, p2p_send_node)
        self._test_consensus_freeze(out_consensus_freeze_txo_p2p, p2p_send_node)

        # rpc send test
        rpc_send_node = RPC_send_node(self.options.tmpdir, self.log, 0, node, self.nodes[0])
        self._test_policy_freeze(out_policy_freeze_txo_rpc, rpc_send_node)
        self._test_consensus_freeze(out_consensus_freeze_txo_rpc, rpc_send_node)


if __name__ == '__main__':
    FrozenTXOTransactionFreeze().main()
