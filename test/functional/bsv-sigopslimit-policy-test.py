#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.


from time import sleep

from test_framework.blocktools import calc_needed_data_size
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.mininode import *
from test_framework.util import *
from test_framework.script import OP_CHECKSIG, OP_FALSE, OP_RETURN, CScript, OP_EQUALVERIFY, OP_HASH160, OP_DUP
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import bytes_to_hex_str

def hashToHex(hash):
    return format(hash, '064x')

class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.txinvs = []

    def on_inv(self, conn, message):
        for i in message.inv:
            if (i.type == 1):
                self.txinvs.append(format(i.hash, '064x'))

    def clear_invs(self):
        with mininode_lock:
            self.txinvs = []


class SigopPolicyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.genesisactivationheight = 205
        self.maxblocksigops = 30000
        self.setup_clean_chain = True

    def setup_network(self):
        # Add & start nodes
        self.add_nodes(self.num_nodes)
        # Create nodes
        self.start_node(0, ['-genesisactivationheight=%d' % self.genesisactivationheight])
        self.start_node(1, ['-genesisactivationheight=%d' % self.genesisactivationheight, '-maxtxsigopscountspolicy=10000'])

        sync_blocks(self.nodes)

    def run_test_node(self, node_index=0, dstaddr='127.0.0.1', dstportno=0, num_of_connections=1):
        test_node = TestNode()
        conn = NodeConn(dstaddr, p2p_port(dstportno), self.nodes[node_index], test_node)
        test_node.add_connection(conn)
        return test_node,conn

    def run_test(self):
        def new_tx(utxo=None, target_sigops=20000, target_tx_size=ONE_MEGABYTE):
            padding_size = calc_needed_data_size([OP_CHECKSIG] * target_sigops, target_tx_size)
            if utxo != None:
                tx_to_spend = utxo['txid']
                vout = utxo['vout']
                value = utxo['amount']
            tx = CTransaction()
            script = CScript([OP_CHECKSIG] * target_sigops)
            tx.vout.append(CTxOut(200000000, script))
            tx.vout.append(CTxOut(200000000, CScript([OP_DUP, OP_HASH160,
                                                      hex_str_to_bytes(
                                                          "ab812dc588ca9d5787dde7eb29569da63c3a238c"),
                                                      OP_EQUALVERIFY,
                                                      OP_CHECKSIG])))
            tx.vout.append(CTxOut(150000000, CScript([OP_FALSE, OP_RETURN] + [bytes(5)[:1] * padding_size])))
            if utxo == None:
                txHex = node.fundrawtransaction(ToHex(tx), {'feeRate': 2, 'changePosition': len(tx.vout)})[
                    'hex']
            else:
                tx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(tx_to_spend)[::-1]), vout)))
                txHex = ToHex(tx)
            txHex = node.signrawtransaction(txHex)['hex']
            tx = FromHex(CTransaction(), txHex)

            tx.rehash()
            return tx
        def generate_block_and_check(txs=None, i_start=0, target_sigops=993, target_tx_size=49768, len_mem0=20, len_mem1=0, num_txs=20):
            if txs == None:
                txs = generate_1mb_txs(i_start=i_start, target_sigops=target_sigops, target_tx_size=target_tx_size, num_txs=num_txs)

            for tx in txs:
                conn.send_message(msg_tx(tx))
            while(len(node.getrawmempool()) < len_mem0):
                sleep(1)

            mempool0 = node.getrawmempool()
            node.generate(1)
            mempool1 = node.getrawmempool()

            assert len(mempool0) == len_mem0, "Len of mempool before creating a block: " + str(len(mempool0)) + ". Should be: "+str(len_mem0)
            assert len(mempool1) == len_mem1, "Mempool after generate should contain: " +str(len_mem1) +", but contains "+ str(len(mempool1))
            return True

        def generate_1mb_txs(i_start=0, target_sigops=993, target_tx_size=49768, num_txs=20):
            txs = []
            i_utxo = i_start
            for i in range(0, num_txs):
                tx = new_tx(utxo=utxos[i_utxo], target_sigops=target_sigops, target_tx_size=target_tx_size)
                txs.append(tx)
                i_utxo += 1
            return txs



        node = self.nodes[0]
        node2 = self.nodes[1]
        test_node, conn = self.run_test_node(0)
        test_node2,conn2 = self.run_test_node(1, dstportno=1)
        thr = NetworkThread()
        thr.start()
        test_node.wait_for_verack()
        test_node2.wait_for_verack()

        hashes = node.generate(200)
        utxos = node.listunspent()

        # We are adding blocks at height 201 and 202, genesis_height is 205
        # Test that we can generate 1MB block with just under 20k sigops
        # (20 transactions, each containing 993+1 sigops, 100 are already in the block)
        # and when adding 1 sigop per transaction to get upto to 20k it fails
        generate_block_and_check(i_start=0, target_sigops=993, target_tx_size=49768, len_mem0=20, len_mem1=0)
        generate_block_and_check(i_start=20, target_sigops=994, target_tx_size=49768, len_mem0=20, len_mem1=1)
        # Get to genesis height
        node.generate(2)
        # Now we're adding a block at height 205 - genesis is enabled
        # Test that we can generate 1MB block with just under 30k sigops and when adding 1 sigop per transaction 
        # to get up to to the limit and it should not fail after genesis because we don't count sigops after Genesis
        generate_block_and_check(i_start=40, target_sigops=1493, target_tx_size=49768, len_mem0=20, len_mem1=0)
        generate_block_and_check(i_start=60, target_sigops=1494, target_tx_size=49768, len_mem0=20, len_mem1=0)

        # Node 2 does not have the policy limit set, so it defaults to consensus
        conn = conn2
        node = node2
        hashes = node.generate(300)
        utxos = node.listunspent()
        # Generate a block with 100 txs, 9999+1 sigops each. The block with sigop/b rate of 0,98 is still valid
        generate_block_and_check(i_start=0, target_sigops=9999, target_tx_size=10000, len_mem0=100, len_mem1=0, num_txs=100)


if __name__ == '__main__':
    SigopPolicyTest().main()