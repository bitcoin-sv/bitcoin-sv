#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Feed transactions into the mempool and check the block candidate building journal
accurately reflects the state of the mempool at all times, including after a reorg.

Build a block based on the journal and check that everything still looks consistent.

Note that most of the actual checking occurs inside the bitcoind process, we are just
setting up situations for that testing to happen here.
'''

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import *
from test_framework.util import *
from test_framework.cdefs import (ONE_MEGABYTE)
from test_framework.blocktools import merkle_root_from_merkle_proof, create_block_from_candidate
import math
import random


class MyNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.setup_finished = False

        # Dictionary of all blocks seen
        self.block_receive_map = {}

    def set_setup_finished(self):
        self.setup_finished = True

    def seen_block(self, sha256):
        return sha256 in self.block_receive_map

    def on_block(self, conn, message):
        super().on_block(conn, message)

        if(self.setup_finished):
            block = message.block
            block.rehash()

            for txn in block.vtx:
                txn.rehash()

            self.block_receive_map[block.hash] = block


# Split some UTXOs into some number of spendable outputs
def split_utxos(fee, node, count, utxos):
    # Split each UTXO into this many outputs
    split_into = max(1, math.ceil(count / len(utxos)))

    # Addresses we send them all to
    addrs = []
    for i in range(split_into):
        addrs.append(node.getnewaddress())

    # Calculate fee we need (based on assuming each outpoint consumes about 70 bytes)
    fee = satoshi_round(Decimal(max(fee, 70 * split_into * 0.00000001)))

    while count > 0 and utxos:
        utxo = utxos.pop()
        inputs = []
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        outputs = {}
        send_value = utxo['amount'] - fee
        if send_value <= 0:
            raise Exception("UTXO value is less than fee")

        for i in range(split_into):
            addr = addrs[i]
            outputs[addr] = satoshi_round(send_value / split_into)
        count -= split_into

        raw_tx = node.createrawtransaction(inputs, outputs)
        signed_tx = node.signrawtransaction(raw_tx)["hex"]
        node.sendrawtransaction(signed_tx)

        # Mine all the generated txns into blocks
        while (node.getmempoolinfo()['size'] > 0):
            node.generate(1)


# Feed some number of UTXOs into a nodes mempool
def fill_mempool(fee, node, num_reqd, ancestor_depth=1):
    # Get enough addresses
    addrs = []
    for i in range(ancestor_depth):
        addrs.append(node.getnewaddress())

    max_pad_size = 16384

    # Generate chains of txns
    utxos = node.listunspent()
    while utxos:
        if num_reqd == 0:
            break
        utxo = utxos.pop()

        # First txn in chain
        input_txid = utxo["txid"]
        input_vout = utxo["vout"]
        input_amount = utxo['amount']

        chain_len = 1
        reqd_chain_len = random.randint(1, ancestor_depth)
        for addr in addrs:
            if num_reqd == 0 or reqd_chain_len == 0:
                break

            inputs = []
            inputs.append({"txid": input_txid, "vout": input_vout})
            outputs = {}
            pad_size = random.randint(0, max_pad_size)
            bytes_used = 70 + pad_size

            # Estimate fee we need
            mfee = satoshi_round(Decimal(max(fee, bytes_used * 0.0000001)))

            # Add a standard spendable output
            send_value = input_amount - mfee
            outputs[addr] = satoshi_round(send_value)

            # Maybe pad txn with OP_RETURN to increase its size
            if(pad_size > max_pad_size / 2):
                mbytes = bytearray(pad_size)
                outputs["data"] = bytes_to_hex_str(mbytes)

            raw_tx = node.createrawtransaction(inputs, outputs)
            signed_tx = node.signrawtransaction(raw_tx)["hex"]
            decoded_raw = node.decoderawtransaction(signed_tx)
            node.sendrawtransaction(signed_tx)

            #print("Created {} byte txn {}, spends {} ({})".format(decoded_raw["size"],
            #    decoded_raw["txid"], decoded_raw["vin"][0]["txid"], "UTXO" if chain_len==1 else "PrevTxn"))

            # Setup for next txn in chain
            input_txid = decoded_raw["txid"]
            for i in range(len(decoded_raw["vout"])):
                value = decoded_raw["vout"][i]["value"]
                if(value > 0):
                    input_vout = i
                    break
            input_amount = send_value
            chain_len += 1
            num_reqd -= 1
            reqd_chain_len -= 1


def mempool_sizes(nodes):
    res = ""
    for node in nodes:
        res += str(node.getmempoolinfo()['size']) + ", "
    return res


class MiningJournal(BitcoinTestFramework):

    def set_test_params(self):
        max_tip_age = 365 * 24 * 60 * 60

        self.setup_clean_chain = True
        self.maxblocksize = 1 * ONE_MEGABYTE
        self.num_nodes = 2
        self.extra_args = [['-whitelist=127.0.0.1', '-maxmempool=300', '-maxmempoolsizedisk=0', '-mindebugrejectionfee=0.0000025',
                            '-maxtipage={}'.format(max_tip_age),
                            '-debug=journal', '-blockassembler=journaling',
                            '-blockmaxsize={}'.format(self.maxblocksize), '-persistmempool',
                            "-checkmempool=1",]] * self.num_nodes
        self.conncbs = []
        self.num_utxos = 5000
        self.ancestor_depth = 25

    def setup_network(self):
        self.setup_nodes()
        sync_blocks(self.nodes)

    def setup_for_submission(self, txnNode):
        self.log.info("Setting up for submission...")

        # Create UTXOs to build a bunch of transactions from
        utxos = create_confirmed_utxos(self.relayfee, self.nodes[txnNode], 50)

        # Create some transactions from the UTXOs
        split_utxos(self.relayfee, self.nodes[txnNode], self.num_utxos, utxos)
        self.sync_all([[self.nodes[txnNode]]])
        self.conncbs[txnNode].set_setup_finished()

    def create_and_submit_block(self, blockNode, candidate, get_coinbase):
        node = self.nodes[blockNode]

        # Do POW for mining candidate and submit solution
        block, coinbase_tx = create_block_from_candidate(candidate, get_coinbase)
        self.log.info("block hash before submit: " + str(block.hash))

        if (get_coinbase):
            self.log.info("Checking submission with provided coinbase")
            return node.submitminingsolution({'id': candidate['id'], 'nonce': block.nNonce})
        else:
            self.log.info("Checking submission with generated coinbase")
            return node.submitminingsolution({'id': candidate['id'],
                                              'nonce': block.nNonce,
                                              'coinbase': '{}'.format(ToHex(coinbase_tx))})

    def init_test(self):
        # Create a P2P connection to our nodes
        connections = []
        for i in range(len(self.nodes)):
            node = MyNode()
            self.conncbs.append(node)
            connections.append(NodeConn('127.0.0.1', p2p_port(i), self.nodes[i], node))
            node.add_connection(connections[i])

        # Start up network handling in another thread. This needs to be called after the P2P connections have been created.
        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        for conn in self.conncbs:
            conn.wait_for_verack()

        # Fee for txns
        self.relayfee = Decimal("250") / COIN

    # Fill the mempool with some different kinds of txns and check the journal accurately tracks it
    def test_initial_mempool(self, txnNode, numTxns=100, mainTest=False):
        if mainTest:
            self.log.info("Testing simple mempool fill...")

        # Setup txns in mempool
        fill_mempool(self.relayfee, self.nodes[txnNode], numTxns, self.ancestor_depth)
        info = self.nodes[txnNode].getmempoolinfo()
        assert_equal(info["size"], info["journalsize"])

    # Check the next mining candidate looks correct given the current contents of the journal
    def check_mining_candidate(self, txnNode):
        self.log.info("Testing mining candidate...")

        # Just check that a provided mining candidate can be mined
        infoBefore = self.nodes[txnNode].getmempoolinfo()
        assert_greater_than(infoBefore["size"], 0)
        assert_greater_than(infoBefore["journalsize"], 0)

        candidate = self.nodes[txnNode].getminingcandidate(True)
        merkleLen = len(candidate["merkleProof"])
        assert_greater_than(merkleLen, 0)
        submitResult = self.create_and_submit_block(txnNode, candidate, True)
        assert submitResult

        infoAfter = self.nodes[txnNode].getmempoolinfo()
        assert_equal(infoAfter["size"], 0)
        assert_equal(infoAfter["journalsize"], 0)

    # Mine a few blocks and check the journal and mempool still agree
    def test_mine_block(self, txnNode, numTxns=100, mainTest=False):
        if mainTest:
            self.log.info("Testing block creation...")

        for i in range(1,3):
            if mainTest or i > 1:
                # Topup and check the mempool again
                self.test_initial_mempool(txnNode, numTxns=numTxns)

            # Mine a block with the current mempool contents
            blockhash = self.nodes[txnNode].generate(nblocks=1)[0]
            wait_until(lambda: self.conncbs[txnNode].seen_block(blockhash))
            self.sync_all([[self.nodes[txnNode]]])
            info = self.nodes[txnNode].getmempoolinfo()
            assert_equal(info["size"], info["journalsize"])

    # Invalidate some blocks to force a reorg and check the mempool, journal and mining candidate
    # all still look correct
    def test_reorg(self, node0, node1):
        self.log.info("Testing reorgs...")
        # Get node1 out of IBD
        self.nodes[node1].generate(1)

        # Link up nodes and check node1 syncs to the same block height
        connect_nodes(self.nodes, node0, node1)
        sync_blocks(self.nodes)

        # Disconnect them again
        disconnect_nodes(self.nodes[node0], node1)

        # Mine 20 blocks on node0 and lots more on node1
        for i in range(1,11):
            self.test_mine_block(node0, numTxns=250)
        blockheight = self.nodes[node0].getblockchaininfo()['blocks']
        blockhash1 = self.nodes[node0].getblockhash(blockheight - 10)

        self.setup_for_submission(node1)
        for i in range(1,11):
            self.test_mine_block(node1, numTxns=100)

        info0 = self.nodes[node0].getmempoolinfo()
        info1 = self.nodes[node1].getmempoolinfo()
        assert_equal(info0["size"], info0["journalsize"])
        assert_equal(info1["size"], info1["journalsize"])

        # Reconnect to force node0 to reorg to node1s longer chain
        connect_nodes(self.nodes, node0, node1)
        sync_blocks(self.nodes, timeout=300)
        info0 = self.nodes[node0].getmempoolinfo()
        info1 = self.nodes[node1].getmempoolinfo()
        assert_equal(info0["size"], info0["journalsize"])
        assert_equal(info1["size"], info1["journalsize"])

        # Invalidate a block on node0 to force it to complex reorg back to the fork it was on earlier
        blockheight = self.nodes[node0].getblockchaininfo()['blocks']
        blockhash2 = self.nodes[node0].getblockhash(blockheight - 120)
        self.nodes[node0].invalidateblock(blockhash1)
        self.nodes[node0].invalidateblock(blockhash2)
        info0 = self.nodes[node0].getmempoolinfo()
        info1 = self.nodes[node1].getmempoolinfo()
        assert_equal(info0["size"], info0["journalsize"])
        assert_equal(info1["size"], info1["journalsize"])

        # Also invalidate a block on node1 to force another big reorg
        blockheight = self.nodes[node0].getblockchaininfo()['blocks']
        blockhash2 = self.nodes[node1].getblockhash(blockheight + 1)
        self.nodes[node1].invalidateblock(blockhash2)
        sync_blocks(self.nodes, timeout=120)
        info0 = self.nodes[node0].getmempoolinfo()
        info1 = self.nodes[node1].getmempoolinfo()
        assert_equal(info0["size"], info0["journalsize"])
        assert_equal(info1["size"], info1["journalsize"])

    # Test a shutdown/restart with txns persisted to disk
    def test_shutdown_restart(self, restartNode):
        self.log.info("Testing shutdown/restart...")

        self.stop_node(restartNode)
        self.start_node(restartNode)
        wait_until(lambda: len(self.nodes[restartNode].getrawmempool()) > 0)

        def check_journal():
            info = self.nodes[restartNode].getmempoolinfo()
            return info["size"] == info["journalsize"]
        wait_until(check_journal, timeout=10)

        # Sleep to ensure the node has fully restarted before we stop it again,
        # otherwise the test framework treats it as a test failure
        time.sleep(5)

    # Test the RPC rebuildJounral command
    def test_rebuild(self, rebuildNode):
        self.log.info("Testing journal rebuild...")

        # Verify good state at start
        status = self.nodes[rebuildNode].checkjournal()
        assert(status["ok"])

        # Rebuild and check again
        self.nodes[rebuildNode].rebuildjournal()
        status = self.nodes[rebuildNode].checkjournal()
        assert(status["ok"])

    def run_test(self):

        # Setup test
        self.init_test()
        self.setup_for_submission(0)

        # Initial fill of mempool
        self.test_initial_mempool(0, mainTest=True)

        # Check next mining candidate
        self.check_mining_candidate(0)

        # Test mining
        self.test_mine_block(0, mainTest=True)

        # Test reorg
        self.test_reorg(0, 1)

        # Test journal rebuild
        self.test_rebuild(0)

        # Shutdown/restart with mempool persist
        self.test_shutdown_restart(0)


if __name__ == '__main__':
    MiningJournal().main()
