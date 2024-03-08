#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import msg_block, NodeConnCB, sha256
from test_framework.miner_id import MinerIdKeys, make_miner_id_block
from test_framework.util import create_confirmed_utxos, wait_until, bytes_to_hex_str, assert_raises_rpc_error
from decimal import Decimal

from bip32utils import BIP32Key

import ecdsa
import os

'''
Test handling of the MinerID revokeminerid RPC interface.
'''


class RovokeMinerIdRpc(BitcoinTestFramework):

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

    def make_revokeminerid_input(self, revocationKeyPublicKey, minerIdPublicKey, compromisedMinerId, revocationKeyPrivateKey, minerIdPrivateKey):
        hashCompromisedMinerId = sha256(compromisedMinerId)
        # Make revoke minerId rpc input.
        inputDoc = {
            "revocationKey": bytes_to_hex_str(revocationKeyPublicKey),
            "minerId": bytes_to_hex_str(minerIdPublicKey),
            "revocationMessage": {
                "compromised_minerId": bytes_to_hex_str(compromisedMinerId)
            },
            "revocationMessageSig": {
                "sig1": bytes_to_hex_str(revocationKeyPrivateKey.sign_digest(hashCompromisedMinerId, sigencode=ecdsa.util.sigencode_der)),
                "sig2": bytes_to_hex_str(minerIdPrivateKey.sign_digest(hashCompromisedMinerId, sigencode=ecdsa.util.sigencode_der))
            }
        }
        return inputDoc

    def test_invalid_input(self, conn):
        assert_raises_rpc_error(-3, "Expected type object, got string", conn.rpc.revokeminerid, "")
        assert_raises_rpc_error(-3, "Expected type object, got array", conn.rpc.revokeminerid, [])
        assert_raises_rpc_error(-8, "Invalid parameter: An empty json object", conn.rpc.revokeminerid, {})
        assert_raises_rpc_error(
            -8, "Invalid revocationKey key!", conn.rpc.revokeminerid,
                {
                    "revocationKey": "1",
                    "minerId": "1",
                    "revocationMessage": {
                        "compromised_minerId": "1"
                    },
                    "revocationMessageSig": {
                        "sig1": "1",
                        "sig2": "1"
                    }
                })
        assert_raises_rpc_error(
            -8, "Invalid minerId key!", conn.rpc.revokeminerid,
                {
                    "revocationKey": self.minerIdRevocationKey.publicKeyHex(),
                    "minerId": "1",
                    "revocationMessage": {
                        "compromised_minerId": "1"
                    },
                    "revocationMessageSig": {
                        "sig1": "1",
                        "sig2": "1"
                    }
                })
        assert_raises_rpc_error(
            -8, "Invalid compromised_minerId key!", conn.rpc.revokeminerid,
                {
                    "revocationKey": self.minerIdRevocationKey.publicKeyHex(),
                    "minerId": self.minerIdKeys[0].publicKeyHex(),
                    "revocationMessage": {
                        "compromised_minerId": "1"
                    },
                    "revocationMessageSig": {
                        "sig1": "1",
                        "sig2": "1"
                    }
                })
        assert_raises_rpc_error(
            -8, "Invalid sig1 signature!", conn.rpc.revokeminerid,
                {
                    "revocationKey": self.minerIdRevocationKey.publicKeyHex(),
                    "minerId": self.minerIdKeys[0].publicKeyHex(),
                    "revocationMessage": {
                        "compromised_minerId": self.minerIdKeys[0].publicKeyHex()
                    },
                    "revocationMessageSig": {
                        "sig1": "1",
                        "sig2": "1"
                    }
                })
        compromisedMinerId = self.minerIdKeys[0].publicKeyBytes()
        assert_raises_rpc_error(
            -8, "Invalid sig2 signature!", conn.rpc.revokeminerid,
                {
                    "revocationKey": self.minerIdRevocationKey.publicKeyHex(),
                    "minerId": self.minerIdKeys[0].publicKeyHex(),
                    "revocationMessage": {
                        "compromised_minerId": self.minerIdKeys[0].publicKeyHex()
                    },
                    "revocationMessageSig": {
                        "sig1": self.minerIdRevocationKey.sign_strmessage(compromisedMinerId),
                        "sig2": "1"
                    }
                })
        #
        # getmineridinfo checks.
        #
        assert_raises_rpc_error(
            -8, "Invalid minerid key!", conn.rpc.getmineridinfo, "1")
        # Test a non-existing key.
        randombip32key = BIP32Key.fromEntropy(os.urandom(16))
        assert(len(conn.rpc.getmineridinfo(bytes_to_hex_str(randombip32key.PublicKey()))) == 0)

    def check_getmineridinfo_result(self, actual, expected):
        assert(actual["minerId"] == expected["minerId"])
        assert(actual["minerIdState"] == expected["minerIdState"])
        assert(actual["prevMinerId"] == expected["prevMinerId"])
        assert(actual["revocationKey"] == expected["revocationKey"])
        assert(actual["prevRevocationKey"] == expected["prevRevocationKey"])

    def run_test(self):

        with self.run_all_nodes_connected(title="revokeminerid_rpc", args=self.extra_args, p2pConnections=[0,1,2]) as (p2p_0,p2p_1,p2p_2):

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

            currentMinerId = self.minerIdKeys[2].publicKeyBytes()
            # The expected result for all nodes.
            expectedResult = {
                "minerId": bytes_to_hex_str(currentMinerId),
                "minerIdState": 'CURRENT',
                "prevMinerId": self.minerIdKeys[1].publicKeyHex(),
                "revocationKey": self.minerIdRevocationKey.publicKeyHex(),
                "prevRevocationKey": self.minerIdRevocationKey.publicKeyHex()
            }
            # Check node0's Miner ID DB state for the requested key.
            self.check_getmineridinfo_result(
                self.nodes[0].getmineridinfo(bytes_to_hex_str(currentMinerId)),
                expectedResult)
            # Check node1's Miner ID DB state for the requested key.
            self.check_getmineridinfo_result(
                self.nodes[1].getmineridinfo(bytes_to_hex_str(currentMinerId)),
                expectedResult)
            # Check node2's Miner ID DB state for the requested key.
            self.check_getmineridinfo_result(
                self.nodes[2].getmineridinfo(bytes_to_hex_str(currentMinerId)),
                expectedResult)

            # The current minerId becomes compromised.
            compromisedMinerId = currentMinerId

            # Call revokeminerid rpc.
            self.nodes[0].revokeminerid(
                self.make_revokeminerid_input(
                    self.minerIdRevocationKey.publicKeyBytes(),
                    self.minerIdKeys[2].publicKeyBytes(),
                    compromisedMinerId,
                    self.minerIdRevocationKey.signingKey(),
                    self.minerIdKeys[2].signingKey()))

            # Check revoked minerId.
            revokedMinerId = bytes_to_hex_str(compromisedMinerId)
            wait_until(lambda: self.nodes[0].getmineridinfo(revokedMinerId)['minerIdState'] == 'REVOKED')
            wait_until(lambda: self.nodes[1].getmineridinfo(revokedMinerId)['minerIdState'] == 'REVOKED')
            wait_until(lambda: self.nodes[2].getmineridinfo(revokedMinerId)['minerIdState'] == 'REVOKED')

            self.test_invalid_input(p2p_0)


if __name__ == '__main__':
    RovokeMinerIdRpc().main()
