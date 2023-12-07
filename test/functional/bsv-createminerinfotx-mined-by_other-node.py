#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.miner_id import create_miner_info_scriptPubKey, MinerIdKeys, make_miner_id_block
from test_framework.test_framework import BitcoinTestFramework, sync_blocks
from test_framework.util import wait_until, assert_equal, bytes_to_hex_str, sync_blocks, disconnect_nodes_bi, connect_nodes_bi, sync_mempools, connect_nodes_mesh, disconnect_nodes, connect_nodes
from test_framework.script import CScript, OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import CTransaction, ToHex, CTxIn, CTxOut, COutPoint, FromHex
from test_framework.blocktools import create_block, create_coinbase
from test_framework.address import key_to_p2pkh
from pathlib import Path
from time import sleep
import json

'''
This test checks a condition, where a valid minerinfo transactions becomes invalid in the
role of a minerinfo transaction. This transaction is still a valid transaction and must
still be considered as a valid member of the funding chain created by the minerinfo transactions.
'''


class AllKeys:
    last_seed_number = 1

    def __init__(self):
        self.minerIdKeys = MinerIdKeys("0{}".format(AllKeys.last_seed_number + 1))
        self.revocationKeys = MinerIdKeys("0{}".format(AllKeys.last_seed_number + 2))
        self.prev_minerIdKeys = MinerIdKeys("0{}".format(AllKeys.last_seed_number + 3))
        self.prev_revocationKeys = MinerIdKeys("0{}".format(AllKeys.last_seed_number + 4))
        self.compromisedKeys = MinerIdKeys("0{}".format(AllKeys.last_seed_number + 5))
        self.fundingKeys = MinerIdKeys("0{}".format(AllKeys.last_seed_number + 6))
        AllKeys.last_seed_number += 6 + 1


class CreateMinerInfoTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.miner_names = ["miner name 0","miner name 1"]
        args = ['-disablesafemode=1', '-mindebugrejectionfee=0', '-paytxfee=0.00003']
        self.extra_args = [args, args, args]

    def setup_network(self):
        self.setup_nodes()
        connect_nodes_mesh(self.nodes)
        self.sync_all()

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
        fundingSeed['fundingDestination'] = {'addressBase58': destination,}
        fundingSeed['firstFundingOutpoint'] = {'txid':txId, 'n': index}

        fundingKeyJson = json.dumps(fundingKey, indent=3)
        fundingSeedJson = json.dumps(fundingSeed, indent=3)

        with open(datapath + '/.minerinfotxsigningkey.dat', 'a') as f:
            f.write(fundingKeyJson)
        with open(datapath + '/minerinfotxfunding.dat', 'a') as f:
            f.write(fundingSeedJson)

    def one_test(self, allKeys, nodenum):

        # create the minerinfodoc transaction and ensure it is in the mempool
        # create a dummy transaction to compare behaviour
        node = self.nodes[nodenum]
        height = node.getblockcount() + 1

        minerinfotx_parameters = {
            'height': height,
            'name': self.miner_names[nodenum],
            'publicIP': '127.0.0.1',
            'publicPort': '8333',
            'minerKeys': allKeys.minerIdKeys,
            'revocationKeys': allKeys.revocationKeys,
            'prev_minerKeys': None,
            'prev_revocationKeys': None,
            'pubCompromisedMinerKeyHex': None
        }

        scriptPubKey = create_miner_info_scriptPubKey (minerinfotx_parameters)
        txid = node.createminerinfotx(bytes_to_hex_str(scriptPubKey))

        # create a minerinfo block with coinbase referencing the minerinfo transaction
        minerInfoTx = FromHex(CTransaction(), node.getrawtransaction(txid))
        block = make_miner_id_block(node, minerinfotx_parameters, minerInfoTx=minerInfoTx)
        block_count = node.getblockcount()
        node.submitblock(ToHex(block))
        wait_until (lambda: block_count + 1 == node.getblockcount())

        # check if the minerinfo-txn
        # was moved from the mempool into the new block
        assert(txid not in node.getrawmempool())
        bhash = node.getbestblockhash()
        block = node.getblock(bhash)
        assert(txid in block['tx'])

        return minerInfoTx, txid

    def run_test(self):
        # create bip32 keys
        allKeys0 = AllKeys()
        allKeys1 = AllKeys()

        # store funding keys and funding txid
        block0, coinbase_tx0 = self.make_block_with_coinbase(self.nodes[0])
        self.nodes[0].submitblock(ToHex(block0))
        self.nodes[0].generate(1)
        self.sync_all()

        block1, coinbase_tx1 = self.make_block_with_coinbase(self.nodes[1])
        self.nodes[1].submitblock(ToHex(block1))
        self.nodes[1].generate(101)
        self.sync_all()

        fundingSeedTx0 = self.create_funding_seed(self.nodes[0], keys=allKeys0.fundingKeys, coinbase_tx=coinbase_tx0)
        self.store_funding_info(nodenum=0, keys=allKeys0.fundingKeys, txId=fundingSeedTx0.hash, index=0)
        self.nodes[0].generate(1)
        self.sync_all()

        fundingSeedTx1 = self.create_funding_seed(self.nodes[1], keys=allKeys1.fundingKeys, coinbase_tx=coinbase_tx1)
        self.store_funding_info(nodenum=1, keys=allKeys1.fundingKeys, txId=fundingSeedTx1.hash, index=0)
        self.nodes[1].generate(1)
        self.sync_all()

        # Disconnect node 1 from the other 2
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[1], 2)

        # mine minerid block on node0 and sync with node2
        forkHeight = self.nodes[0].getblockcount()
        minerinfotx_saved, txid_saved = self.one_test(allKeys0, nodenum=0)
        sync_blocks([self.nodes[0], self.nodes[2]])

        assert(self.nodes[0].getblockcount() == forkHeight + 1)
        assert(self.nodes[1].getblockcount() == forkHeight)
        assert(self.nodes[2].getblockcount() == forkHeight + 1)

        # make the second nodes chain the longest
        minerinfotx, _ = self.one_test(allKeys1, 1)
        minerinfotx, _ = self.one_test(allKeys1, 1)

        assert(self.nodes[0].getblockcount() == forkHeight + 1)
        assert(self.nodes[1].getblockcount() == forkHeight + 2)
        assert(self.nodes[2].getblockcount() == forkHeight + 1)

        # reconnect nodes and force reorg for nodes 0 and 2
        connect_nodes(self.nodes, 0, 1)
        connect_nodes(self.nodes, 1, 2)
        sync_blocks(self.nodes)

        assert(self.nodes[0].getblockcount() == forkHeight + 2)
        assert(self.nodes[1].getblockcount() == forkHeight + 2)
        assert(self.nodes[2].getblockcount() == forkHeight + 2)

        # Manually insert miner-info txn from node0's lost block into node2's mempool
        self.nodes[2].sendrawtransaction(ToHex(minerinfotx_saved), True, True)
        assert(txid_saved in self.nodes[2].getrawmempool())

        # Mine the miner-info txn
        self.nodes[2].generate(1)
        assert(txid_saved not in self.nodes[2].getrawmempool())
        sync_blocks(self.nodes)

        assert(self.nodes[0].getblockcount() == forkHeight + 3)
        assert(self.nodes[1].getblockcount() == forkHeight + 3)
        assert(self.nodes[2].getblockcount() == forkHeight + 3)

        # Check node 0 can still fund another miner-info txn
        minerinfotx, _ = self.one_test(allKeys0, 0)
        sync_blocks(self.nodes)
        minerinfotx, _ = self.one_test(allKeys0, 0)
        sync_blocks(self.nodes)


if __name__ == '__main__':
    CreateMinerInfoTest().main()
