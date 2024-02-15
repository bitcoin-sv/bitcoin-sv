#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import msg_block, msg_revokemid, NodeConnCB
from test_framework.util import create_confirmed_utxos, wait_until
from test_framework.miner_id import MinerIdKeys, make_miner_id_block
from decimal import Decimal

import os
import time

'''
Test P2P handling of the MinerID revokemid message.
'''


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.revokemid_count = 0

    def on_revokemid(self, conn, message):
        super().on_revokemid(conn, message)
        self.revokemid_count += 1


class RevokeMid(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [['-whitelist=127.0.0.1']] * self.num_nodes

        # Setup miner ID keys
        self.minerIdKeys = []
        for i in range(4):
            self.minerIdKeys.append(MinerIdKeys("{}".format(i)))

        # And a single revocation key
        self.minerIdRevocationKey = MinerIdKeys("10")

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        with self.run_all_nodes_connected(title="RevokeMid", args=self.extra_args, p2pConnections=[0,1,2], cb_class=TestNode) as (p2p_0,p2p_1,p2p_2):

            # Get out of IBD and make some spendable outputs
            utxos = create_confirmed_utxos(Decimal("250") / 100000000, self.nodes[0], 5, nodes=self.nodes)

            # Produce a miner ID chain with 2 rotated keys and a current key
            minerIdParams = {
                'height': self.nodes[0].getblockcount() + 1,
                'minerKeys': self.minerIdKeys[0],
                'revocationKeys': self.minerIdRevocationKey
            }
            block = make_miner_id_block(p2p_0, minerIdParams, utxo=utxos.pop())
            p2p_0.send_message(msg_block(block))
            wait_until(lambda: self.nodes[0].getbestblockhash() == block.hash)

            minerIdParams = {
                'height': self.nodes[0].getblockcount() + 1,
                'minerKeys': self.minerIdKeys[1],
                'prev_minerKeys': self.minerIdKeys[0],
                'revocationKeys': self.minerIdRevocationKey
            }
            block = make_miner_id_block(p2p_0, minerIdParams, utxo=utxos.pop())

            block = make_miner_id_block(p2p_0, minerIdParams, utxo=utxos.pop())
            p2p_0.send_message(msg_block(block))
            wait_until(lambda: self.nodes[0].getbestblockhash() == block.hash)

            minerIdParams = {
                'height': self.nodes[0].getblockcount() + 1,
                'minerKeys': self.minerIdKeys[2],
                'prev_minerKeys': self.minerIdKeys[1],
                'revocationKeys': self.minerIdRevocationKey
            }
            block = make_miner_id_block(p2p_0, minerIdParams, utxo=utxos.pop())
            p2p_0.send_message(msg_block(block))
            wait_until(lambda: self.nodes[0].getbestblockhash() == block.hash)

            self.sync_all()

            minerids0 = self.nodes[0].dumpminerids()
            minerids1 = self.nodes[1].dumpminerids()
            minerids2 = self.nodes[2].dumpminerids()
            assert(len(minerids0['miners']) == 1)
            assert(len(minerids1['miners']) == 1)
            assert(len(minerids2['miners']) == 1)
            assert(len(minerids0['miners'][0]['minerids']) == 3)
            assert(len(minerids1['miners'][0]['minerids']) == 3)
            assert(len(minerids2['miners'][0]['minerids']) == 3)
            assert(minerids0['miners'][0]['reputation']['void'] == False)
            assert(minerids1['miners'][0]['reputation']['void'] == False)
            assert(minerids2['miners'][0]['reputation']['void'] == False)

            # Bad node steals honest nodes miner ID key and spoils their reputation
            minerIdParams['height'] = self.nodes[1].getblockcount() + 1
            badblock = make_miner_id_block(p2p_0, minerIdParams, utxo=utxos.pop(), makeValid=False)
            p2p_0.send_message(msg_block(badblock))

            wait_until(lambda: self.nodes[0].dumpminerids()['miners'][0]['reputation']['void'] == True)

            # Honest node sends revokemid message to revoke their stolen key
            revokemid = msg_revokemid(self.minerIdRevocationKey.signingKey(), self.minerIdRevocationKey.publicKeyBytes(),
                                      self.minerIdKeys[2].signingKey(), self.minerIdKeys[2].publicKeyBytes(),
                                      self.minerIdKeys[2].publicKeyBytes())
            p2p_0.send_message(revokemid)

            wait_until(lambda: self.nodes[0].dumpminerids()['miners'][0]['minerids'][0]['state'] == 'REVOKED')
            wait_until(lambda: self.nodes[1].dumpminerids()['miners'][0]['minerids'][0]['state'] == 'REVOKED')
            wait_until(lambda: self.nodes[2].dumpminerids()['miners'][0]['minerids'][0]['state'] == 'REVOKED')

            # Check we get the revokmid msg forwarded to us from the 2 peers we didn't initially send it to
            wait_until(lambda: p2p_0.cb.revokemid_count == 2)
            time.sleep(1)
            assert(p2p_0.cb.revokemid_count == 2)

            # Honest node revokes and rotates their stolen key in the next block
            minerIdParams = {
                'height': self.nodes[0].getblockcount() + 1,
                'minerKeys': self.minerIdKeys[3],
                'prev_minerKeys': self.minerIdKeys[2],
                'revocationKeys': self.minerIdRevocationKey,
                'pubCompromisedMinerKeyHex': self.minerIdKeys[2].publicKeyHex()
            }
            block = make_miner_id_block(p2p_0, minerIdParams, utxo=utxos.pop())
            p2p_0.send_message(msg_block(block))
            wait_until(lambda: self.nodes[0].getbestblockhash() == block.hash)

            self.sync_all()

            minerids0 = self.nodes[0].dumpminerids()
            minerids1 = self.nodes[1].dumpminerids()
            minerids2 = self.nodes[2].dumpminerids()
            assert(len(minerids0['miners']) == 1)
            assert(len(minerids1['miners']) == 1)
            assert(len(minerids2['miners']) == 1)
            assert(len(minerids0['miners'][0]['minerids']) == 4)
            assert(len(minerids1['miners'][0]['minerids']) == 4)
            assert(len(minerids2['miners'][0]['minerids']) == 4)
            assert(minerids0['miners'][0]['reputation']['void'] == False)
            assert(minerids1['miners'][0]['reputation']['void'] == False)
            assert(minerids2['miners'][0]['reputation']['void'] == False)
            assert(minerids0['miners'][0]['minerids'][0]['state'] == 'CURRENT')
            assert(minerids1['miners'][0]['minerids'][0]['state'] == 'CURRENT')
            assert(minerids2['miners'][0]['minerids'][0]['state'] == 'CURRENT')


if __name__ == '__main__':
    RevokeMid().main()
