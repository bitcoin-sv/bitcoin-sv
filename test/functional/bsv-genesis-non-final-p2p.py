#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test p2p behavior for non-final txns.

Scenario:

Send non-final txn to node0. It should be forwarded over P2P to node1.
Both nodes agree on contents of their mempools.

Send finalising txn to node0. It should be forwarded over P2P to node1.
Both nodes agree on contents of their mempools.

"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.script import CScript, OP_TRUE
from test_framework.blocktools import create_transaction
from test_framework.util import assert_equal, p2p_port, wait_until
from test_framework.mininode import ( NodeConn, NodeConnCB, NetworkThread, msg_tx, CTransaction, COutPoint,
        CTxIn, CTxOut, FromHex, ToHex)
import time, copy

class NonFinalP2PTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [['-debug', '-genesisactivationheight=%d' % 100,
                            '-txnpropagationfreq=1', '-txnvalidationasynchrunfreq=1']] * self.num_nodes

    def create_locked_transaction(self, prevtx, n, sig, value, scriptPubKey, lockTime, sequence):
        tx = self.create_transaction(prevtx, n, sig, value, scriptPubKey)
        tx.nLockTime = lockTime
        tx.vin[0].nSequence = sequence
        tx.rehash()
        return tx

    def run_test(self):
        # Create a P2P connection to the first node
        node0 = NodeConnCB()
        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0))
        node0.add_connection(connections[0])

        # Start up network handling in another thread. This needs to be called
        # after the P2P connections have been created.
        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()

        # Out of IBD
        self.nodes[0].generate(1)

        # First create funding transaction that pays to output that does not require signatures.
        out_value = 10000
        ftx = CTransaction()
        ftx.vout.append(CTxOut(out_value, CScript([OP_TRUE])))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex']
        ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()

        # Allow coinbase to mature
        self.nodes[0].generate(101)

        # Feed in funding txn and wait for both nodes to see it
        connections[0].send_message(msg_tx(ftx))
        wait_until(lambda: ftx.hash in self.nodes[0].getrawmempool(), timeout=5)
        wait_until(lambda: ftx.hash in self.nodes[1].getrawmempool(), timeout=5)

        # Create non-final txn.
        parent_txid = ftx.sha256
        send_value = out_value - 500;
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(parent_txid, 0), b'', 0x01))
        tx.vout.append(CTxOut(int(send_value), CScript([OP_TRUE])))
        tx.nLockTime = int(time.time()) + 300
        tx.rehash()

        # Send non-final txn to node0. It should be forwarded over P2P to node1.
        connections[0].send_message(msg_tx(tx))
        wait_until(lambda: tx.hash in self.nodes[0].getrawnonfinalmempool(), timeout=5)
        wait_until(lambda: tx.hash in self.nodes[1].getrawnonfinalmempool(), timeout=5)
        assert(tx.hash not in self.nodes[0].getrawmempool())
        assert(tx.hash not in self.nodes[1].getrawmempool())

        # Create finalising txn.
        finaltx = copy.deepcopy(tx)
        finaltx.vin[0].nSequence = 0xFFFFFFFF;
        finaltx.rehash()

        # Send finalising txn to node0. It should be forwarded over P2P to node1.
        connections[0].send_message(msg_tx(finaltx))
        wait_until(lambda: finaltx.hash in self.nodes[0].getrawmempool(), timeout=5)
        wait_until(lambda: finaltx.hash in self.nodes[1].getrawmempool(), timeout=5)
        assert(tx.hash not in self.nodes[0].getrawnonfinalmempool())
        assert(tx.hash not in self.nodes[1].getrawnonfinalmempool())

if __name__ == '__main__':
    NonFinalP2PTest().main()
