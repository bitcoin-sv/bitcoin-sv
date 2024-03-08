#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import msg_block, msg_getdata, CInv, CTransaction, NodeConnCB
from test_framework.blocktools import merkle_root_from_branch
from test_framework.miner_id import MinerIdKeys, make_miner_id_block, create_dataref_txn, create_dataref
from test_framework.util import create_confirmed_utxos, wait_until, assert_equal
from test_framework.comptool import mininode_lock
from decimal import Decimal

import copy
import ecdsa
import os

'''
Test P2P handling of getdata requests for dataref txns.

1) Create a miner ID block containing 2 dataref transactions.
2) Send a getdata request for those datarefs.
3) Verify we receive a datareftx P2P message for each.
4) Verify merkle proofs for returned datarefs.
5) Send a getdata request for non-existent dataref
6) Verify we get a notfound P2P response.
7) Send getdata for dataref that was in block but not referenced in miner-info doc.
8) Verify we get notfound response.
'''


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.notFound = []
        self.datarefTx = []

    def on_notfound(self, conn, message):
        super().on_notfound(conn, message)
        for nf in message.inv:
            self.notFound.append(nf.hash)

    def on_datareftx(self, conn, message):
        super().on_datareftx(conn, message)
        self.datarefTx.append(copy.deepcopy(message))

    def GetDatarefTx(self, txid):
        for msg in self.datarefTx:
            if(msg.tx.hash == txid):
                return msg
        return None


class GetdataDataref(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = ['-whitelist=127.0.0.1']
        self.curve = ecdsa.SECP256k1

        # Setup miner ID keys
        self.minerIdKey = MinerIdKeys("01")

        # And a single revocation key
        self.minerIdRevocationKey = MinerIdKeys("10")

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        with self.run_node_with_connections(title="GetDataRef", node_index=0, args=self.extra_args,
                                            number_of_connections=1, cb_class=TestNode) as (p2p_0):

            conn = p2p_0[0]

            # Get out of IBD and make some spendable outputs
            utxos = create_confirmed_utxos(Decimal("250") / 100000000, self.nodes[0], 5, nodes=self.nodes)

            # Create a few dataref txns
            dataref1_brfcid_json = {}
            dataref1_brfcid_json['example1'] = 'value1'
            dataref1_json = {}
            dataref1_json['data'] = {'id1' : dataref1_brfcid_json}
            dataref1 = create_dataref_txn(conn, dataref1_json, utxos.pop())

            dataref2_brfcid_json = {}
            dataref2_brfcid_json['example2'] = 'value2'
            dataref2_json = {}
            dataref2_json['data'] = {'id2' : dataref2_brfcid_json}
            dataref2 = create_dataref_txn(conn, dataref2_json, utxos.pop())

            dataref3_brfcid_json = {}
            dataref3_brfcid_json['example3'] = 'value3'
            dataref3_json = {}
            dataref3_json['data'] = {'id3' : dataref3_brfcid_json}
            dataref3 = create_dataref_txn(conn, dataref3_json, utxos.pop())

            # Reference just 2 of the datarefs in the miner-info document
            datarefs = [create_dataref(['id1'], dataref1.hash, 0),
                        create_dataref(['id2'], dataref2.hash, 0)]

            # Send a miner ID block containing dataref txns
            minerIdParams = {
                'height': self.nodes[0].getblockcount() + 1,
                'minerKeys': self.minerIdKey,
                'revocationKeys': self.minerIdRevocationKey,
                'datarefs': datarefs
            }
            block = make_miner_id_block(conn, minerIdParams, utxo=utxos.pop(), txns=[dataref1, dataref2, dataref3])
            conn.send_message(msg_block(block))
            wait_until(lambda: self.nodes[0].getbestblockhash() == block.hash)

            self.sync_all()

            minerids = self.nodes[0].dumpminerids()
            assert(len(minerids['miners']) == 1)
            assert(len(minerids['miners'][0]['minerids']) == 1)

            # Send getdata request for each dataref txn
            conn.send_message(msg_getdata([CInv(CInv.DATAREF_TX, dataref1.sha256), CInv(CInv.DATAREF_TX, dataref2.sha256)]))

            def datarefReceived(tx):
                for dataref in conn.cb.datarefTx:
                    if dataref.tx.hash == tx.hash:
                        return True
                return False
            wait_until(lambda: datarefReceived(dataref1), timeout=10, lock=mininode_lock)
            wait_until(lambda: datarefReceived(dataref2), timeout=10, lock=mininode_lock)

            # Check merkle proof on received dataref txns
            for dataref in [dataref1, dataref2]:
                msg = conn.cb.GetDatarefTx(dataref.hash)
                calculatedRootHash = merkle_root_from_branch(msg.proof.txOrId, msg.proof.index, [x.value for x in msg.proof.nodes])
                assert_equal(calculatedRootHash, block.hashMerkleRoot)

            # Send getdata request for non-existent dataref txn
            badTx = CTransaction()
            badTx.nLockTime = 1
            badTx.rehash()
            conn.send_message(msg_getdata([CInv(CInv.DATAREF_TX, badTx.sha256)]))
            wait_until(lambda: badTx.sha256 in conn.cb.notFound, timeout=10, lock=mininode_lock)

            # Send getdata for datref that wasn't referenced in the miner-info document
            conn.send_message(msg_getdata([CInv(CInv.DATAREF_TX, dataref3.sha256)]))
            wait_until(lambda: dataref3.sha256 in conn.cb.notFound, timeout=10, lock=mininode_lock)


if __name__ == '__main__':
    GetdataDataref().main()
