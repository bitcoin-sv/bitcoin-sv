#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test -assumewhitelistedblockdepth option

See comments and log entries for detailed description of steps performed in each test.
"""

from test_framework.blocktools import make_block, create_transaction, PreviousSpendableOutput
from test_framework.key import CECKey
from test_framework.mininode import (
    ToHex,
    CTxOut
)
from test_framework.script import CScript, hash160, OP_CHECKSIG, OP_DUP, OP_EQUALVERIFY, OP_FALSE, OP_HASH160, OP_RETURN, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
    get_datadir_path,
    initialize_datadir,
    wait_until
)
import shutil
import time


# OP_RETURN protocol with confiscation transaction protocol id
CTX_OP_RETURN = [OP_FALSE, OP_RETURN, b'cftx']


class FrozenTXOConfiscation_AssumeWhitelisted(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.chain = ChainManager()
        self.extra_args_common = ["-whitelist=127.0.0.1", "-minrelaytxfee=0", "-minminingtxfee=0", "-limitfreerelay=999999"]
        self.extra_args = [self.extra_args_common,
                           self.extra_args_common+["-enableassumewhitelistedblockdepth=1", "-assumewhitelistedblockdepth=5"]]
        self.block_count = 0

    def _init(self):
        # Private key used in scripts with CHECKSIG
        self.prvkey = CECKey()
        self.prvkey.set_secretbytes(b"horsebattery")
        self.pubkey = self.prvkey.get_pubkey()

        class Node: pass # P2P connection is not needed, but we want RPC connection as member in node object for consistency with other confiscation tests
        node = Node()
        node.rpc = self.nodes[0]
        node1 = Node()
        node1.rpc = self.nodes[1]

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(self.block_count)
        self.block_count += 1
        self.chain.save_spendable_output()
        node.rpc.submitblock(block.serialize().hex())

        for i in range(100):
            block = self.chain.next_block(self.block_count)
            self.block_count += 1
            self.chain.save_spendable_output()
            node.rpc.submitblock(block.serialize().hex())

        self.log.info("Waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)
        self.nodes[1].waitforblockheight(101)

        return node, node1

    def _wait_for_mempool(self, node, contents):
        wait_until(lambda: node.rpc.getrawmempool() == contents, check_interval=0.15, timeout=10)

    def _wait_for_block_status(self, node, blockhash, status):
        def wait_predicate():
            for tips in node.rpc.getchaintips():
                if (status=="" or tips["status"] == status) and tips["hash"] == blockhash:
                    return True
            return False
        wait_until(wait_predicate, check_interval=0.15, timeout=10)

    def make_block_with_tx(self, node, tx, block_time_offset=None):
        block, _ = make_block(node.rpc)
        if block_time_offset != None:
            # Changing the block time can be used to create block with same contents and different hash
            block.nTime += block_time_offset
        block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.calc_sha256()
        block.solve()
        return block

    def _create_tx(self, tx_out, unlock, lock):
        unlock_script = b'' if callable(unlock) else unlock
        tx_out_value = tx_out.tx.vout[tx_out.n].nValue  # value of the spendable output
        tx = create_transaction(tx_out.tx, tx_out.n, unlock_script, tx_out_value, lock)

        if callable(unlock):
            tx.vin[0].scriptSig = unlock(tx, tx_out.tx)
            tx.calc_sha256()

        return tx

    def _create_confiscation_tx(self, tx_out, unlock, lock):
        ctx = self._create_tx(tx_out, unlock, lock)

        # Insert OP_RETURN output that makes this a confiscation transaction.
        ctx.vout.insert(0, CTxOut(0, CScript(CTX_OP_RETURN + [
            b'\x01' +                       # protocol version number
            hash160(b'confiscation order')  # hash of confiscation order document
        ])))
        ctx.rehash()

        return ctx

    def _create_and_send_tx(self, node):
        tx = self._create_tx(self.chain.get_spendable_output(), b'', CScript([OP_DUP, OP_HASH160, hash160(self.pubkey), OP_EQUALVERIFY, OP_CHECKSIG])) # TXO with standard P2PKH script that can normally only be spent if private key is known
        self.log.info(f"Sending transaction {tx.hash} and generating a new block")
        node.rpc.sendrawtransaction(ToHex(tx))
        assert_equal(node.rpc.getrawmempool(), [tx.hash])
        node.rpc.generate(1)
        assert_equal(node.rpc.getrawmempool(), [])
        return tx

    def run_test(self):
        node, node1 = self._init()

        frozen_tx = self._create_and_send_tx(node)

        root_block_height = node.rpc.getblockcount()
        root_block_hash = node.rpc.getbestblockhash()

        self.log.info(f"Block height before confiscation transactions: {root_block_height}")

        # Create a block with confiscation transaction
        confiscate_tx1 = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        block_rej1 = self.make_block_with_tx(node, confiscate_tx1)

        self.log.info(f"Submitting block {block_rej1.hash} containing a non-whitelisted confiscation transaction {confiscate_tx1.hash} and checking it is rejected")
        node.rpc.submitblock(block_rej1.serialize().hex())
        self._wait_for_block_status(node, block_rej1.hash, "invalid")
        assert_equal(root_block_hash, node.rpc.getbestblockhash())

        self.log.info("Restarting node with options -enableassumewhitelistedblockdepth=0, -assumewhitelistedblockdepth=0")
        node.rpc.stop_node()
        node.rpc.wait_until_stopped()
        node.rpc.start(True, self.extra_args[0]+["-enableassumewhitelistedblockdepth=0", "-assumewhitelistedblockdepth=0"])
        node.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        block_rej2 = self.make_block_with_tx(node, confiscate_tx1, 2) # Create another block with same confiscation transaction
        self.log.info(f"Submitting block {block_rej2.hash} containing a non-whitelisted confiscation transaction {confiscate_tx1.hash} and checking it is rejected because -assumewhitelistedblockdepth option is not enabled")
        node.rpc.submitblock(block_rej2.serialize().hex())
        self._wait_for_block_status(node, block_rej2.hash, "invalid")
        assert_equal(root_block_hash, node.rpc.getbestblockhash())

        self.log.info("Restarting node with options -enableassumewhitelistedblockdepth=1, -assumewhitelistedblockdepth=1")
        node.rpc.stop_node()
        node.rpc.wait_until_stopped()
        node.rpc.start(True, self.extra_args[0]+["-enableassumewhitelistedblockdepth=1", "-assumewhitelistedblockdepth=1"])
        node.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        block_rej3 = self.make_block_with_tx(node, confiscate_tx1, 3)
        self.log.info(f"Submitting block {block_rej3.hash} containing a non-whitelisted confiscation transaction {confiscate_tx1.hash} and checking it is rejected because -assumewhitelistedblockdepth=1 requires at least one descendant")
        node.rpc.submitblock(block_rej3.serialize().hex())
        self._wait_for_block_status(node, block_rej3.hash, "invalid")
        assert_equal(root_block_hash, node.rpc.getbestblockhash())

        self.log.info("Restarting node with options -enableassumewhitelistedblockdepth=1, -assumewhitelistedblockdepth=0")
        node.rpc.stop_node()
        node.rpc.wait_until_stopped()
        node.rpc.start(True, self.extra_args[0]+["-enableassumewhitelistedblockdepth=1", "-assumewhitelistedblockdepth=0"])
        node.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info(f"Sending confiscation transaction {confiscate_tx1.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is rejected because option -assumewhitelistedblockdepth does not apply to mempool")
        assert_raises_rpc_error(-26, "bad-ctx-not-whitelisted", node.rpc.sendrawtransaction, ToHex(confiscate_tx1))

        block = self.make_block_with_tx(node, confiscate_tx1, 4)
        self.log.info(f"Submitting block {block.hash} containing a non-whitelisted confiscation transaction {confiscate_tx1.hash} and checking it is accepted on node and rejected on node1")
        node.rpc.submitblock(block.serialize().hex())
        assert_equal(block.hash, node.rpc.getbestblockhash())
        self._wait_for_block_status(node1, block.hash, "invalid")
        assert_equal(root_block_hash, node1.rpc.getbestblockhash())

        self.log.info(f"Soft rejecting (numblocks=3) and reconsidering block {block.hash} on node1 to make node accept subsequent headers")
        node1.rpc.softrejectblock(block.hash, 3)
        node1.rpc.reconsiderblock(block.hash)

        self.log.info(f"Mining 4 blocks and checking that block {block.hash} and descendants are still considered invalid on node1")
        node.rpc.generate(4)
        best_block_hash = node.rpc.getbestblockhash()
        self._wait_for_block_status(node1, best_block_hash, "invalid")
        assert_equal(root_block_hash, node1.rpc.getbestblockhash())

        self.log.info(f"Soft rejecting (numblocks=4) and reconsidering block {block.hash} on node1 to make node accept subsequent headers")
        # Soft rejection with numblocks one less than assumewhitelistedblockdepth will result in the 5th block not being soft rejected anymore
        # and will be considered as a chain tip. Consequently, all previous block will be validated including the one containing non-whitelisted
        # confiscation transaction, which will now be considered valid, because it has required number of descendants.
        node1.rpc.softrejectblock(block.hash, 4)
        node1.rpc.reconsiderblock(block.hash)

        self.log.info(f"Mining 1 block and checking that block {block.hash} and descendants were accepted on node1")
        node.rpc.generate(1)
        best_block_hash = node.rpc.getbestblockhash()
        self._wait_for_block_status(node1, best_block_hash, "active")
        assert_equal(len(node1.rpc.getchaintips()), 1) # there should now be only one chain
        node1.rpc.acceptblock(block.hash) # remove (now redundant) soft rejection

        self.log.info("Stopping node1, clearing and reinitializing its data directory")
        node1.rpc.stop_node()
        node1.rpc.wait_until_stopped()
        node1_dir = get_datadir_path(self.options.tmpdir, 1)
        shutil.rmtree(node1_dir)
        initialize_datadir(self.options.tmpdir, 1)

        self.log.info("Starting node1 without option -assumewhitelistedblockdepth")
        node1.rpc.start(True, self.extra_args_common)
        node1.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info(f"Checking that IBD stops before block {block.hash}")
        self._wait_for_block_status(node1, root_block_hash, "active")
        self._wait_for_block_status(node1, best_block_hash, "")
        time.sleep(1)
        assert_equal(root_block_hash, node1.rpc.getbestblockhash())

        self.log.info("Stopping node1, clearing and reinitializing its data directory")
        node1.rpc.stop_node()
        node1.rpc.wait_until_stopped()
        node1_dir = get_datadir_path(self.options.tmpdir, 1)
        shutil.rmtree(node1_dir)
        initialize_datadir(self.options.tmpdir, 1)

        self.log.info("Starting node1 with option -assumewhitelistedblockdepth=6")
        node1.rpc.start(True, self.extra_args_common + ["-enableassumewhitelistedblockdepth=1", "-assumewhitelistedblockdepth=6"])
        node1.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info(f"Checking that IBD stops before block {block.hash}")
        self._wait_for_block_status(node1, root_block_hash, "active")
        self._wait_for_block_status(node1, best_block_hash, "")
        time.sleep(1)
        assert_equal(root_block_hash, node1.rpc.getbestblockhash())

        self.log.info("Stopping node1, clearing and reinitializing its data directory")
        node1.rpc.stop_node()
        node1.rpc.wait_until_stopped()
        node1_dir = get_datadir_path(self.options.tmpdir, 1)
        shutil.rmtree(node1_dir)
        initialize_datadir(self.options.tmpdir, 1)

        self.log.info("Starting node1 with option -assumewhitelistedblockdepth=5")
        node1.rpc.start(True) # option -assumewhitelistedblockdepth=5 is already included in extra_args for this node
        node1.rpc.wait_for_rpc_connection()

        self.log.info("Mining 1 block on node1 before connecting it to network to check that option assumewhitelistedblockdepth works in case of a large reorg")
        node1.rpc.generate(1)

        self.log.info("Connecting node 1 to network")
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("Checking that node1 successfully reorgs to the tip of the chain")
        self._wait_for_block_status(node1, best_block_hash, "active")
        assert_equal(best_block_hash, node1.rpc.getbestblockhash())

        self.log.info("Stopping node1, clearing and reinitializing its data directory")
        node1.rpc.stop_node()
        node1.rpc.wait_until_stopped()
        node1_dir = get_datadir_path(self.options.tmpdir, 1)
        shutil.rmtree(node1_dir)
        initialize_datadir(self.options.tmpdir, 1)

        self.log.info("Starting node1 with option -assumewhitelistedblockdepth=5")
        node1.rpc.start(True) # option -assumewhitelistedblockdepth=5 is already included in extra_args for this node
        node1.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("Checking that IBD finishes successfully at the tip of the chain")
        self._wait_for_block_status(node1, best_block_hash, "active")
        assert_equal(best_block_hash, node1.rpc.getbestblockhash())

        self.log.info("Checking that verifychain find no issues")
        assert node1.rpc.verifychain(4, 0)

        self.log.info("Restarting node1 without option -assumewhitelistedblockdepth")
        node1.rpc.stop_node()
        node1.rpc.wait_until_stopped()
        node1.rpc.start(True, self.extra_args_common)
        node1.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("Checking that verifychain reports a problem because there is a non-whitelisted confiscation transaction in the active chain")
        assert not node1.rpc.verifychain(4, 0)

        self.log.info("Restarting node1 with option -assumewhitelistedblockdepth=5")
        node1.rpc.stop_node()
        node1.rpc.wait_until_stopped()
        node1.rpc.start(True)
        node1.rpc.wait_for_rpc_connection()
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("Checking that verifychain find no issues after node restart")
        assert node1.rpc.verifychain(4, 0)


if __name__ == '__main__':
    FrozenTXOConfiscation_AssumeWhitelisted().main()
