#!/usr/bin/env python3
# Copyright (c) 2026 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.miner_id import create_miner_info_scriptPubKey, MinerIdKeys, make_miner_id_block
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str, wait_until
from test_framework.script import CScript, OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import CTransaction, ToHex, CTxIn, CTxOut, COutPoint, FromHex
from test_framework.blocktools import create_block, create_coinbase
from test_framework.address import key_to_p2pkh

from pathlib import Path
import json

"""
Test for bad miner info coinbase transaction crash scenario.

This test verifies that a node no longer crashes when receiving a block with a bad
miner info coinbase transaction that has only 1 vout instead of the required 2.

A valid miner info coinbase should have:
  - vout[0]: Standard coinbase payment output
  - vout[1]: OP_RETURN output containing miner info reference and blockbind signature

The bad coinbase in this test has the miner info reference in vout[0] but
no vout[1].
"""


class AllKeys:
    def __init__(self):
        self.minerIdKeys = MinerIdKeys("01")
        self.revocationKeys = MinerIdKeys("02")
        self.prev_minerIdKeys = MinerIdKeys("03")
        self.prev_revocationKeys = MinerIdKeys("04")
        self.compromisedKeys = MinerIdKeys("06")
        self.fundingKeys = MinerIdKeys("10")


class CreateMinerInfoTest(BitcoinTestFramework):

    def make_block_with_coinbase(self, conn_rpc):
        tip = conn_rpc.getblock(conn_rpc.getbestblockhash())
        coinbase_tx = create_coinbase(tip["height"] + 1)
        block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
        block.solve()
        return block, coinbase_tx

    def create_funding_seed(self, node, keys, coinbase_tx):
        amount = coinbase_tx.vout[0].nValue
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(coinbase_tx.sha256, 0), b"", 0xffffffff))

        scriptPubKey = CScript([OP_DUP, OP_HASH160, hash160(keys.publicKeyBytes()), OP_EQUALVERIFY, OP_CHECKSIG])
        amount2 = int(amount / 2)
        tx.vout.append(CTxOut(amount2, scriptPubKey))
        tx.vout.append(CTxOut(amount2, scriptPubKey))

        tx.rehash()
        txid = node.sendrawtransaction(ToHex(tx), False, True)
        assert_equal(node.getrawmempool(), [txid])
        return tx

    def store_funding_info(self, nodenum, keys, txId, index):
        datapath = self.options.tmpdir + "/node{}/regtest/miner_id/Funding".format(nodenum)
        Path(datapath).mkdir(parents=True, exist_ok=True)

        destination = key_to_p2pkh(keys.publicKeyBytes())

        fundingKey = {}
        fundingSeed = {}
        fundingKey['fundingKey'] = {'privateBIP32': keys.privateKey()}
        fundingSeed['fundingDestination'] = {'addressBase58': destination, }
        fundingSeed['firstFundingOutpoint'] = {'txid': txId, 'n': index}

        fundingKeyJson = json.dumps(fundingKey, indent=3)
        fundingSeedJson = json.dumps(fundingSeed, indent=3)

        with open(datapath + '/.minerinfotxsigningkey.dat', 'w') as f:
            f.write(fundingKeyJson)
        with open(datapath + '/minerinfotxfunding.dat', 'w') as f:
            f.write(fundingSeedJson)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.miner_names = ["miner name 0"]
        self.extra_args = [['-mindebugrejectionfee=0', '-paytxfee=0.00003', '-minerid=1']] * self.num_nodes

    def create_bad_miner_info_block(self, node, allKeys):
        """Create a block with a bad miner info coinbase transaction.

        The strategy: Create a proper miner info block using make_miner_id_block,
        then modify the coinbase to have only 1 vout (keeping the miner info reference
        in vout[0] to trigger validation). This should cause modify_merkle_root() to
        fail the assertion: assert(block.vtx[0]->vout.size() >= 2)
        """
        # Get current tip
        tip = node.getblock(node.getbestblockhash())
        height = tip["height"] + 1

        # Create miner info transaction parameters
        minerinfotx_parameters = {
            'height': height,
            'name': self.miner_names[0],
            'publicIP': '127.0.0.1',
            'publicPort': '8333',
            'minerKeys': allKeys.minerIdKeys,
            'revocationKeys': allKeys.revocationKeys,
            'prev_minerKeys': None,
            'prev_revocationKeys': None,
            'pubCompromisedMinerKeyHex': None
        }

        # Create the miner info scriptPubKey
        scriptPubKey = create_miner_info_scriptPubKey(minerinfotx_parameters)

        # Create the miner info transaction using the RPC
        txid = node.createminerinfotx(bytes_to_hex_str(scriptPubKey))
        minerInfoTx = FromHex(CTransaction(), node.getrawtransaction(txid))

        # Create a proper miner info block first
        block = make_miner_id_block(node, minerinfotx_parameters, minerInfoTx=minerInfoTx)

        # Now make it bad: Keep only vout[0] but make it look like miner info output
        # The coinbase currently has 2 vouts:
        # vout[0]: payment
        # vout[1]: miner info reference with blockbind
        # We'll keep the miner info reference structure but put it in a single vout
        coinbaseTx = block.vtx[0]

        # Take the miner info reference from vout[1] and merge it into vout[0]
        # by appending the OP_RETURN script
        if len(coinbaseTx.vout) >= 2:
            # Save the miner info reference script from vout[1]
            minerInfoRefScript = coinbaseTx.vout[1].scriptPubKey
            # Now remove vout[1] so we only have 1 output
            coinbaseTx.vout = [coinbaseTx.vout[0]]
            # Modify vout[0] to include the miner info reference
            # This makes it detectable as miner info but with wrong vout count
            coinbaseTx.vout[0].scriptPubKey = minerInfoRefScript
            coinbaseTx.rehash()

            # Update the block
            block.vtx[0] = coinbaseTx
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()

        return block

    def run_test(self):
        # create bip32 keys
        allKeys = AllKeys()

        # store funding keys and funding txid
        block, coinbase_tx = self.make_block_with_coinbase(self.nodes[0])
        self.nodes[0].submitblock(ToHex(block))
        self.nodes[0].generate(101)
        fundingSeedTx = self.create_funding_seed(self.nodes[0], allKeys.fundingKeys, coinbase_tx)
        self.store_funding_info(0, allKeys.fundingKeys, fundingSeedTx.hash, index=0)
        self.nodes[0].generate(1)

        # Create and submit a bad miner info block
        self.log.info("Creating bad miner info block with single vout in coinbase")
        bad_block = self.create_bad_miner_info_block(self.nodes[0], allKeys)

        # Submit the bad (only from a minerid perspective) block
        self.log.info("Submitting bad block")
        block_count0 = self.nodes[0].getblockcount()
        self.nodes[0].submitblock(ToHex(bad_block))
        wait_until(lambda: block_count0 + 1 == self.nodes[0].getblockcount(), timeout=10)


if __name__ == '__main__':
    CreateMinerInfoTest().main()
