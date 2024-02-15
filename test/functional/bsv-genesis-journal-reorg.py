#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Verify that a large reorg across the Genesis boundary does not result in any
journal errors.
'''

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import *
from test_framework.util import *
from test_framework.script import CScript, OP_TRUE, OP_FALSE, OP_RETURN, OP_ADD, OP_DROP, OP_4, OP_CHECKSIG
from test_framework.blocktools import create_transaction
import codecs


class JournalReorg(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [['-whitelist=127.0.0.1',
                            '-debug=journal',
                            '-blockassembler=journaling',
                            '-genesisactivationheight=400',
                            '-maxgenesisgracefulperiod=0'],
                           ['-whitelist=127.0.0.1',
                            '-checkmempool=1',
                            '-debug=journal',
                            '-blockassembler=journaling',
                            '-genesisactivationheight=403',
                            '-maxgenesisgracefulperiod=0']]

    def mine_big_txns(self, node):
        # Mine 25 blocks with big txns
        utxos = node.listunspent()
        relayfee = node.getnetworkinfo()['relayfee'] * 100
        for i in range(25):
            # 4 big txns per block
            for j in range(4):
                txn = utxos.pop()
                send_value = txn['amount'] - relayfee
                inputs = []
                inputs.append({"txid": txn["txid"], "vout": txn["vout"]})
                outputs = {}
                addr = node.getnewaddress()
                outputs[addr] = satoshi_round(send_value)
                outputs["data"] = bytes_to_hex_str(bytearray(32000))

                rawTxn = node.createrawtransaction(inputs, outputs)
                signedTxn = node.signrawtransaction(rawTxn)["hex"]
                node.sendrawtransaction(signedTxn)

            node.generate(1)

    def send_genesis_txn(self, node):
        # Create and send a txn that is only valid post-genesis
        utxos = node.listunspent()
        spendTxn = utxos.pop()
        fee = Decimal(0.0000025) #node.getnetworkinfo()['relayfee']
        send_value1 = int((spendTxn['amount'] - fee) * 100000000)
        send_value2 = int((spendTxn['amount'] - fee*2) * 100000000)

        txOpAdd1 = CTransaction()
        txOpAdd1.vin.append(CTxIn(COutPoint(int(spendTxn["txid"], 16), spendTxn["vout"]), b'', 0xffffffff))
        txOpAdd1.vout.append(CTxOut(send_value1, CScript([b'\xFF'*4, b'\xFF'*4, OP_ADD, OP_4, OP_ADD, OP_DROP, OP_TRUE])))
        txOpAdd1.calc_sha256()
        tx1raw = bytes_to_hex_str(txOpAdd1.serialize())
        tx1raw = node.signrawtransaction(tx1raw)
        tx1decoded = node.decoderawtransaction(tx1raw["hex"])
        txOpAdd2 = CTransaction()
        txOpAdd2.vin.append(CTxIn(COutPoint(int(tx1decoded["txid"], 16), 0), b'', 0xffffffff))
        addr = node.getnewaddress()
        validatedAddr = node.validateaddress(addr)
        txOpAdd2.vout.append(CTxOut(send_value2, CScript([codecs.decode(validatedAddr["pubkey"], 'hex'), OP_CHECKSIG])))
        txOpAdd2.calc_sha256()
        tx2raw = bytes_to_hex_str(txOpAdd2.serialize())

        node.sendrawtransaction(tx1raw["hex"])
        node.sendrawtransaction(tx2raw)

    def got_chain_tip(self, node, height, status):
        tips = node.getchaintips()
        for tip in tips:
            if tip["height"] == height and tip["status"] == status:
                return True
        return False

    def run_test(self):
        # Create a P2P connection to one of the nodes
        node0 = NodeConnCB()
        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0))
        node0.add_connection(connections[0])

        # Start up network handling in another thread. This needs to be called
        # after the P2P connections have been created.
        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()

        # Generate blocks on each node to get us out of IBD and have some funds to spend
        for node in self.nodes:
            # Disconnect nodes before each generate RPC. On a busy environment generate
            # RPC might not create the provided number of blocks. While nodes are communicating
            # P2P messages can cause generateBlocks function to skip a block. Check the comment
            # in generateBlocks function for details.
            disconnect_nodes_bi(self.nodes, 0, 1)
            node.generate(150)
            connect_nodes_bi(self.nodes, 0, 1)
            self.sync_all()

        # Wait for coinbase to mature
        for node in self.nodes:
            # Disconnect nodes for the same reason as above.
            disconnect_nodes_bi(self.nodes, 0, 1)
            node.generate(50)
            connect_nodes_bi(self.nodes, 0, 1)
            self.sync_all()

        # Disconnect nodes so they can build their own competing chains
        disconnect_nodes_bi(self.nodes, 0, 1)

        # Make chain on node0 invalid for node1
        self.send_genesis_txn(self.nodes[0])

        # Mine lots of UTXOs on each node
        for node in self.nodes:
            self.mine_big_txns(node)

        # Reconnect nodes
        connect_nodes(self.nodes, 0, 1)

        # Force a reorg
        self.nodes[0].generate(1)
        wait_until(lambda: self.got_chain_tip(self.nodes[1], 426, "invalid"))
        wait_until(lambda: self.got_chain_tip(self.nodes[1], 425, "active"))

        # check we didn't hit a reorg error
        assert(not check_for_log_msg(self, "ERROR: Failed to find and remove txn", "/node1"))


if __name__ == '__main__':
    JournalReorg().main()
