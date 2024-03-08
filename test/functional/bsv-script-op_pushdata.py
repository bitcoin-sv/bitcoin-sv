#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import TxCreator
from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import COIN, COutPoint, CTxIn, CTxOut, \
    CTransaction, ToHex, FromHex, NetworkThread, NodeConnCB, NodeConn, \
    msg_tx
from test_framework.script import CScript, OP_DROP, OP_2DROP, OP_EQUALVERIFY, \
    OP_FALSE, OP_SIZE, OP_TRUE
from test_framework.util import wait_until, p2p_port


class TestOP_PUSHDATA(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[]] * self.num_nodes

        self.genesisactivationheight = 100
        self.extra_args = [['-blockmaxsize=0',
                            '-excessiveblocksize=0',
                            '-genesisactivationheight=%d' % self.genesisactivationheight,
                            '-maxmempool=3GB',
                            '-minminingtxfee=0.00000001',
                            '-maxnonstdtxvalidationduration=50000',
                            '-maxscriptnumlengthpolicy=0',
                            '-maxscriptsizepolicy=0',
                            '-maxstackmemoryusagepolicy=0',
                            '-maxstackmemoryusageconsensus=0',
                            '-maxstdtxvalidationduration=1000',
                            '-maxtxnvalidatorasynctasksrunduration=100000',
                            '-maxtxsizepolicy=0',
                            '-txnvalidationqueuesmaxmemory=3000MB']]

    def add_node_connections(self):
        connections = []
        self.node0 = NodeConnCB()
        connections.append(NodeConn('127.0.0.1',
                                    p2p_port(0),
                                    self.nodes[0],
                                    self.node0))
        self.node0.add_connection(connections[0])

        NetworkThread().start()
        self.node0.wait_for_verack()

    def make_and_send_tx(self, inputs, outputs):
        tx = CTransaction()
        tx.vin = inputs
        tx.vout = outputs
        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx))["hex"]
        tx = FromHex(CTransaction(), tx_hex)
        self.node0.send_message(msg_tx(tx))
        return tx

    def make_funding_txns(self, utxos):
        for utxo in utxos:
            vin = [CTxIn(COutPoint(int(utxo["txid"], 16),
                                   utxo["vout"]),
                         CScript([OP_FALSE]), 0xffffffff)]
            vout = [CTxOut(10 * COIN, CScript([OP_TRUE]))]
            tx = self.make_and_send_tx(vin, vout)
            wait_until(lambda: tx.hash in self.nodes[0].getrawmempool())
            yield tx

    def run_test(self):
        self.add_node_connections()

        # Get out of IBD and create some spendable outputs
        self.nodes[0].generate(120)
        self.sync_all()

        # Produce some trivially spendable outputs
        utxos = self.nodes[0].listunspent()
        funds = self.make_funding_txns(utxos)
        self.nodes[0].generate(1)
        self.sync_all()

        tx_creator = TxCreator()

        def test_pushdata1(funds):
            scriptPubKey = CScript([bytearray([42] * 0xff),
                                    OP_SIZE,
                                    bytearray([0xff, 0]),  # extra byte for sign bit
                                    OP_EQUALVERIFY,
                                    OP_2DROP,
                                    OP_DROP,
                                    OP_TRUE])
            tx0 = tx_creator.create_signed_transaction(funds,
                                                       value=9 * COIN,
                                                       fee_rate=1,
                                                       scriptPubKey=scriptPubKey)
            self.node0.send_message(msg_tx(tx0))
            wait_until(lambda: tx0.hash in self.nodes[0].getrawmempool())

            tx1 = tx_creator.create_signed_transaction(tx0,
                                                       value=8 * COIN,
                                                       fee_rate=1,
                                                       scriptPubKey=CScript([OP_TRUE]))
            self.node0.send_message(msg_tx(tx1))
            wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        def test_pushdata2(funds):
            scriptPubKey = CScript([bytearray([42] * 0xffff),
                                    OP_SIZE,
                                    bytearray([0xff, 0xff, 0]),  # extra byte for sign bit
                                    OP_EQUALVERIFY,
                                    OP_2DROP,
                                    OP_DROP,
                                    OP_TRUE])
            tx0 = tx_creator.create_signed_transaction(funds,
                                                       value=9 * COIN,
                                                       fee_rate=1,
                                                       scriptPubKey=scriptPubKey)
            self.node0.send_message(msg_tx(tx0))
            wait_until(lambda: tx0.hash in self.nodes[0].getrawmempool())

            tx1 = tx_creator.create_signed_transaction(tx0,
                                                       value=8 * COIN,
                                                       fee_rate=1,
                                                       scriptPubKey=CScript([OP_TRUE]))
            self.node0.send_message(msg_tx(tx1))
            wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # Note: Testing 0x8000'0000 requires changing MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS
        # (a consensus change). Also, remember to add an extra byte for the sign bit (see
        # tests for OP_PUSHDATA1 and OP_PUSHDATA2
        def test_pushdata4(funds):
            scriptPubKey = CScript([bytearray([42] * 0x3b110000),  # implicit OP_PUSHDATA4
                                    OP_SIZE,
                                    bytearray([0x0, 0x0, 0x11, 0x3b]),
                                    OP_EQUALVERIFY,
                                    OP_2DROP,
                                    OP_DROP,
                                    OP_TRUE
                                    ])
            tx0 = tx_creator.create_signed_transaction(funds,
                                                       value=9 * COIN,
                                                       fee_rate=1,
                                                       scriptPubKey=scriptPubKey)
            self.node0.send_message(msg_tx(tx0))
            wait_until(lambda: tx0.hash in self.nodes[0].getrawmempool())

            tx1 = tx_creator.create_signed_transaction(tx0,
                                                       value=8 * COIN,
                                                       fee_rate=1,
                                                       scriptPubKey=CScript([OP_TRUE]))
            self.node0.send_message(msg_tx(tx1))
            wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        test_pushdata1(next(funds))
        test_pushdata2(next(funds))
        test_pushdata4(next(funds))


if __name__ == '__main__':
    TestOP_PUSHDATA().main()
