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
Create a miner info doc and send it to the node via the createminerinfotx rpc function.
Create a chain of mineridinfo-txns where each such transaction funds the next.
Retive a transaction-id via getminerinfotxid rpc function.
Run these checks on another node in parallel using the same minerID.
'''
class AllKeys:
    def __init__(self):
        self.minerIdKeys = MinerIdKeys("01")
        self.revocationKeys = MinerIdKeys("02")
        self.prev_minerIdKeys = MinerIdKeys("03")
        self.prev_revocationKeys = MinerIdKeys("04")
        self.compromisedKeys = MinerIdKeys("06")
        self.fundingKeys = MinerIdKeys("10")

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
        self.TEST_call_create_twice = 2
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
        fundingSeed['firstFundingOutpoint'] = {'txid':txId, 'n': index}

        fundingKeyJson = json.dumps(fundingKey, indent=3)
        fundingSeedJson = json.dumps(fundingSeed, indent=3)

        with open(datapath + '/.minerinfotxsigningkey.dat', 'w') as f:
            f.write(fundingKeyJson)
        with open(datapath + '/minerinfotxfunding.dat', 'w') as f:
            f.write(fundingSeedJson)

    def one_test(self, allKeys, fundingSeedTx, test_case):
        # create the minerinfodoc transaction and ensure it is in the mempool
        # create a dummy transaction to compare behaviour
        height = self.nodes[0].getblockcount() + 1
        minerinfotx_parameters = {
                'height': height,
                'name': self.miner_names[0],
                'publicIP': '127.0.0.1',
                'publicPort': '8333',
                'minerKeys': allKeys.minerIdKeys,
                'revocationKeys': allKeys.revocationKeys,
                'prev_minerKeys': None,
                'prev_revocationKeys': None,
                'pubCompromisedMinerKeyHex': None }

        scriptPubKey = create_miner_info_scriptPubKey (minerinfotx_parameters)
        txid = self.nodes[0].createminerinfotx(bytes_to_hex_str(scriptPubKey))
        txid_x = self.nodes[1].createminerinfotx(bytes_to_hex_str(scriptPubKey))

        if test_case == self.TEST_call_create_twice:
            txid_twice = self.nodes[0].createminerinfotx(bytes_to_hex_str(scriptPubKey))
            txid_twice_x = self.nodes[1].createminerinfotx(bytes_to_hex_str(scriptPubKey))
            assert(txid == txid_twice)
            assert(txid_x == txid_twice_x)
            assert(txid == txid_x)

        if test_case == self.TEST_call_replace:
            minerinfotx_parameters['publicPort'] = '8334' # vary the port to get a different txid
            scriptPubKey = create_miner_info_scriptPubKey (minerinfotx_parameters)
            txid_twice = self.nodes[0].replaceminerinfotx(bytes_to_hex_str(scriptPubKey))
            txid_twice_x = self.nodes[1].replaceminerinfotx(bytes_to_hex_str(scriptPubKey))
            assert(txid != txid_twice)
            assert(txid_x != txid_twice_x)
            txid = txid_twice
            txid_x = txid_twice_x

        # ensure the minerinfotx was not relayed to the connected node
        wait_until(lambda: txid in self.nodes[0].getrawmempool(), timeout=10)
        wait_until(lambda: txid_x in self.nodes[1].getrawmempool(), timeout=10)
        sleep(0.3) # to give time for relaying
        tx0 = self.nodes[0].getminerinfotxid()
        tx1 = self.nodes[1].getminerinfotxid()
        assert(tx0 == txid)
        assert(tx1 == txid_x)

        # create a minerinfo block with coinbase referencing the minerinfo transaction
        minerInfoTx = FromHex(CTransaction(), self.nodes[0].getrawtransaction(txid))
        block = make_miner_id_block(self.nodes[0], None, minerInfoTx, height, allKeys.minerIdKeys)
        block_x = make_miner_id_block(self.nodes[1], None, minerInfoTx, height, allKeys.minerIdKeys)
        block_count = self.nodes[0].getblockcount()
        block_count_x = self.nodes[1].getblockcount()

        self.nodes[0].submitblock(ToHex(block))
        self.nodes[1].submitblock(ToHex(block))
        wait_until (lambda: block_count + 1 == self.nodes[0].getblockcount())
        wait_until (lambda: block_count + 1 == self.nodes[1].getblockcount())

        # check if the minerinfo-txn
        # was moved from the mempool into the new block
        assert(txid not in self.nodes[0].getrawmempool())
        bhash = self.nodes[0].getbestblockhash()
        block = self.nodes[0].getblock(bhash)
        assert(txid in block['tx'])

        assert(txid not in self.nodes[1].getrawmempool())
        bhash = self.nodes[1].getbestblockhash()
        block = self.nodes[1].getblock(bhash)
        assert(txid in block['tx'])

        tx0 = self.nodes[0].getminerinfotxid()
        tx1 = self.nodes[1].getminerinfotxid()
        assert(tx0 == None)
        assert(tx1 == None)
        assert(tx1 == tx0)


    def run_test(self):
        # create bip32 keys
        allKeys = AllKeys()

        # store funding keys and funding txid
        block, coinbase_tx = self.make_block_with_coinbase(self.nodes[0])
        self.nodes[0].submitblock(ToHex(block))
        self.nodes[0].generate(101)
        self.sync_all()
        fundingSeedTx = self.create_funding_seed(self.nodes[0], allKeys.fundingKeys, coinbase_tx)
        self.store_funding_info(0, allKeys.fundingKeys, fundingSeedTx.hash, index=0)
        self.store_funding_info(1, allKeys.fundingKeys, fundingSeedTx.hash, index=0)
        self.nodes[0].generate(1)
        self.sync_all()

        # repeate test just to be sure
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_replace)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create_twice)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create_twice)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create)

        # test persistence of funding information

        self.stop_nodes()
        self.start_nodes(self.extra_args)
        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create)


if __name__ == '__main__':
    CreateMinerInfoTest().main()
