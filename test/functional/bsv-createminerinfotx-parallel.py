#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.miner_id import create_miner_info_scriptPubKey, MinerIdKeys, make_miner_id_block
from test_framework.test_framework import BitcoinTestFramework, sync_blocks
from test_framework.util import wait_until, assert_equal, bytes_to_hex_str, disconnect_nodes_bi, connect_nodes_bi, connect_nodes
from test_framework.script import CScript, OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import CTransaction, ToHex, CTxIn, CTxOut, COutPoint, FromHex
from test_framework.blocktools import create_block, create_coinbase
from test_framework.address import key_to_p2pkh
from test_framework.cdefs import ONE_GIGABYTE, ONE_MEGABYTE
from pathlib import Path
from time import sleep
import json

'''
Create miner info transactions in two nodes in parallel using the same minerid but
independent funding chains.
Stop node1 and continue mining on node2.
Reconnect and continue mining on both nodes
The two funding seeds are the two outputs of a coinbase signed with different keys
to ensure the funding chains are different
'''


class AllKeys:
    def __init__(self):
        self.minerIdKeys = MinerIdKeys("01")
        self.revocationKeys = MinerIdKeys("02")
        self.prev_minerIdKeys = MinerIdKeys("03")
        self.prev_revocationKeys = MinerIdKeys("04")
        self.compromisedKeys = MinerIdKeys("06")
        self.fundingKey0 = MinerIdKeys("10")
        self.fundingKey1 = MinerIdKeys("11")


class CreateMinerInfoTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.miner_names = ["miner name 0","miner name 1"]

        args = [
            '-mindebugrejectionfee=0',
            '-paytxfee=0.00003'
        ]

        self.extra_args = [args,args]

        self.TEST_call_create = 1
        self.TEST_call_replace = 3

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

        scriptPubKey0 = CScript([OP_DUP, OP_HASH160, hash160(keys[0].publicKeyBytes()), OP_EQUALVERIFY, OP_CHECKSIG])
        scriptPubKey1 = CScript([OP_DUP, OP_HASH160, hash160(keys[1].publicKeyBytes()), OP_EQUALVERIFY, OP_CHECKSIG])
        amount2 = int(amount / 2)
        tx.vout.append(CTxOut(amount2, scriptPubKey0))
        tx.vout.append(CTxOut(amount2, scriptPubKey1))

        tx.rehash()
        txid = node.sendrawtransaction(ToHex(tx), False, True)
        assert_equal(node.getrawmempool(), [txid])
        return tx

    def store_funding_info(self, nodenum, key, txId, index):
        datapath = self.options.tmpdir + "/node{}/regtest/miner_id/Funding".format(nodenum)
        Path(datapath).mkdir(parents=True, exist_ok=True)

        destination = key_to_p2pkh(key.publicKeyBytes())

        fundingKey = {}
        fundingSeed = {}
        fundingKey['fundingKey'] = {'privateBIP32': key.privateKey()}
        fundingSeed['fundingDestination'] = {'addressBase58': destination,}
        fundingSeed['firstFundingOutpoint'] = {'txid':txId, 'n': index}

        fundingKeyJson = json.dumps(fundingKey, indent=3)
        fundingSeedJson = json.dumps(fundingSeed, indent=3)

        with open(datapath + '/.minerinfotxsigningkey.dat', 'w') as f:
            f.write(fundingKeyJson)
        with open(datapath + '/minerinfotxfunding.dat', 'w') as f:
            f.write(fundingSeedJson)

    def one_test(self, allKeys, winner, oneNodeOnly=False):
        # create the minerinfodoc transaction and ensure it is in the mempool
        height = self.nodes[winner].getblockcount() + 1
        minerinfotx_parameters = {
            'height': height,
            'name': self.miner_names[winner],
            'publicIP': '127.0.0.1',
            'publicPort': '8333',
            'minerKeys': allKeys.minerIdKeys,
            'revocationKeys': allKeys.revocationKeys,
            'prev_minerKeys': None,
            'prev_revocationKeys': None,
            'pubCompromisedMinerKeyHex': None
        }

        scriptPubKey = create_miner_info_scriptPubKey (minerinfotx_parameters)
        txid = self.nodes[winner].createminerinfotx(bytes_to_hex_str(scriptPubKey))

        # create a minerinfo block with coinbase referencing the minerinfo transaction
        minerInfoTx = FromHex(CTransaction(), self.nodes[winner].getrawtransaction(txid))
        block = make_miner_id_block(self.nodes[winner], minerinfotx_parameters, minerInfoTx=minerInfoTx)
        block_count = self.nodes[winner].getblockcount()

        self.nodes[winner].submitblock(ToHex(block))
        wait_until (lambda: block_count + 1 == self.nodes[winner].getblockcount())

        # check if the minerinfo-txn
        # was moved from the mempool into the new block
        assert(txid not in self.nodes[winner].getrawmempool())
        bhash = self.nodes[winner].getbestblockhash()
        block = self.nodes[winner].getblock(bhash)
        assert(txid in block['tx'])
        if not oneNodeOnly:
            if winner == 0:
                looser = 1
            else:
                looser = 0
            self.sync_all()
            assert(len(self.nodes[looser].getrawmempool()) == 0)
            assert(bhash == self.nodes[looser].getbestblockhash())

    def run_test(self):
        # create bip32 keys
        allKeys = AllKeys()

        # store funding keys and funding txid
        block, coinbase_tx = self.make_block_with_coinbase(self.nodes[0])
        self.nodes[0].submitblock(ToHex(block))
        self.nodes[0].generate(101)
        self.sync_all()

        fundingSeedTx = self.create_funding_seed(self.nodes[0], [allKeys.fundingKey0, allKeys.fundingKey1], coinbase_tx)

        self.store_funding_info(nodenum=0, key=allKeys.fundingKey0, txId=fundingSeedTx.hash, index=0)
        self.nodes[0].generate(1)
        self.sync_all()

        self.store_funding_info(nodenum=1, key=allKeys.fundingKey1, txId=fundingSeedTx.hash, index=1)
        self.nodes[1].generate(1)
        self.sync_all()

        self.one_test(allKeys, winner=1)
        self.one_test(allKeys, winner=0)
        self.one_test(allKeys, winner=0)
        self.one_test(allKeys, winner=1)
        self.one_test(allKeys, winner=0)
        self.one_test(allKeys, winner=1)
        self.one_test(allKeys, winner=0)

        # test persistence of funding information

        self.stop_node(1)
        self.one_test(allKeys, winner=0, oneNodeOnly=True)
        self.one_test(allKeys, winner=0, oneNodeOnly=True)
        self.one_test(allKeys, winner=0, oneNodeOnly=True)

        self.start_node(1, self.extra_args[1])
        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all()
        self.one_test(allKeys, winner=0)
        self.one_test(allKeys, winner=1)
        self.one_test(allKeys, winner=0)
        self.one_test(allKeys, winner=1)


if __name__ == '__main__':
    CreateMinerInfoTest().main()
