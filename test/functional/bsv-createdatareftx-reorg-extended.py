#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.miner_id import create_miner_info_scriptPubKey, MinerIdKeys, make_miner_id_block, create_dataref
from test_framework.test_framework import BitcoinTestFramework, sync_blocks
from test_framework.util import wait_until, assert_equal, bytes_to_hex_str, sync_blocks, disconnect_nodes_bi, connect_nodes_bi, hashToHex
from test_framework.script import CScript, OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG, OP_FALSE, OP_RETURN
from test_framework.mininode import CTransaction, ToHex, CTxIn, CTxOut, COutPoint, FromHex, COIN
from test_framework.blocktools import create_block, create_coinbase
from test_framework.address import key_to_p2pkh
from pathlib import Path
from time import sleep
import json

'''
Create and send a dataref transaction with two scripts to the node.
Send a minerinfo transaction referencing said dataref transaction in the miner info doc.
Check reorg conditions. This test reorgs 1000 blocks deep.
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
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.miner_names = ["miner name 0","miner name 1"]
        args = ['-disablesafemode=1', '-mindebugrejectionfee=0', '-paytxfee=0.00003', '-txindex=1']
        self.extra_args = [args, args]

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

    def create_dataref_txn (self, node):

        brfcDataA = {
            '62b21572ca46': {'mydata':'hello world1'},
            'a224052ad433': {'mydata':'hello world2'}}
        brfcDataB = {
            '77721572ca46': {'mydata':'byby world1'},
            '8884052ad433': {'mydata':'byby world2'}}
        brfcDataC = {
            'AAAA1572ca46': {'mydata':'byby world1'},
            'AAAA052ad433': {'mydata':'byby world2'}}
        brfcDataD = {
            'BBBB1572ca46': {'mydata':'byby world1'},
            'BBBB052ad433': {'mydata':'byby world2'}}

        brfcDatas = [brfcDataA, brfcDataB, brfcDataC, brfcDataD]

        def datarefToScript (data):
            brfcDataJson = json.dumps(data, indent=0)
            brfcDataJson = brfcDataJson.replace('\n', '')
            brfcDataJson = brfcDataJson.replace(' ', '')
            brfcDataJson = brfcDataJson.encode('utf8')
            return brfcDataJson

        scriptPubKeys = []
        for b in brfcDatas:
            script = CScript([OP_FALSE, OP_RETURN, bytearray([0x60, 0x1d, 0xfa, 0xce]), bytearray([0x0]) ,datarefToScript(b)])
            scriptPubKeys.append (bytes_to_hex_str(script))

        txid = node.createdatareftx(scriptPubKeys)
        wait_until(lambda: txid in node.getrawmempool())

        dataRefs = []
        for b in brfcDatas:
            dataRefs.append(create_dataref(list(b.keys()), txid, vout=0))

        return dataRefs

    def second_spends_first (self, node, txid1, txid2):
        tx1 = FromHex(CTransaction(), node.getrawtransaction(txid1))
        tx2 = FromHex(CTransaction(), node.getrawtransaction(txid2))
        tx1.rehash()
        tx2.rehash()

        for x in tx2.vin:
            if hashToHex(x.prevout.hash) == tx1.hash:
                return True
        return False

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

    def one_test(self, allKeys, nodenum, do_mining=True, datarefs=None):

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
            'pubCompromisedMinerKeyHex': None,
            'datarefs': datarefs
        }

        scriptPubKey = create_miner_info_scriptPubKey(minerinfotx_parameters)
        txid = node.createminerinfotx(bytes_to_hex_str(scriptPubKey))
        wait_until (lambda: txid in node.getrawmempool())

        if datarefs:
            wait_until(lambda: datarefs[0]['txid'] in node.getrawmempool())

        if not do_mining:
            return
        # create a minerinfo block with coinbase referencing the minerinfo transaction
        minerInfoTx = FromHex(CTransaction(), node.getrawtransaction(txid))
        datarefTxns = {}
        if datarefs:
            for dataref in datarefs:
                datarefTxn = FromHex(CTransaction(), node.getrawtransaction(dataref['txid']))
                datarefTxn.rehash()
                datarefTxns[datarefTxn.hash] = datarefTxn

        block = make_miner_id_block(node, minerinfotx_parameters, datarefTxns=list(datarefTxns.values()), minerInfoTx=minerInfoTx)
        block_count = node.getblockcount()
        node.submitblock(ToHex(block))
        wait_until (lambda: block_count + 1 == node.getblockcount())

        # check if the minerinfo-txn
        # was moved from the mempool into the new block
        assert(txid not in node.getrawmempool())
        bhash = node.getbestblockhash()
        block = node.getblock(bhash)
        assert(txid in block['tx'])
        return txid

    def run_test(self):
        # create bip32 keys
        allKeys0 = AllKeys()
        allKeys1 = AllKeys()

        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)

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

        # mine minerid blocks and sync
        self.one_test(allKeys0, nodenum=0)
        sync_blocks(self.nodes)
        self.one_test(allKeys0, nodenum=0, do_mining=True)
        # disconnect and mine independently.
        # make the second nodes chain the longest
        forkHeight = self.nodes[0].getblockcount()
        disconnect_nodes_bi(self.nodes,0,1)

        # create a minerinfo txn with a dataref and prove that
        # the minerinfo txn spends the dataref transaction
        datarefs = self.create_dataref_txn (self.nodes[0])
        minerinfo_tx = self.one_test(allKeys0, 0, datarefs=datarefs)
        dataref_tx = datarefs[0]['txid']
        assert(self.second_spends_first(self.nodes[0], dataref_tx, minerinfo_tx))

        # create more minerinfo txns
        self.one_test(allKeys0, 0, do_mining=False)

        for _ in range(1000):
            self.one_test(allKeys1, 1)
        for _ in range(100):
            self.one_test(allKeys0, 0)

        self.one_test(allKeys1, 1)
        self.one_test(allKeys1, 1)
        self.one_test(allKeys1, 1, do_mining=False)
        last_block0 = self.nodes[0].getbestblockhash()
        last_block1 = self.nodes[1].getbestblockhash()
        last_height0 = self.nodes[0].getblockcount()
        last_height1 = self.nodes[1].getblockcount()

        assert(last_block0 != last_block1)
        assert(last_height0 != last_height1)

        # connect nodes. All nodes should now mine on the longer chain
        # mined by the second node
        connect_nodes_bi(self.nodes,0,1)
        sync_blocks(self.nodes)

        # no change for the second node which has the longer chain
        assert(last_block1 == self.nodes[1].getbestblockhash())
        assert(last_height1 == self.nodes[1].getblockcount())

        # but first node has now reorged to the second node
        last_block0 = self.nodes[0].getbestblockhash()
        last_height0 = self.nodes[0].getblockcount()

        assert(last_height0 == last_height1)
        assert(last_block0 == last_block1)

        # mine on the new chain. After syncing all blocks should share the same tip
        self.one_test(allKeys0, 0)
        self.one_test(allKeys0, 0)
        sync_blocks(self.nodes)

        assert(self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())

        # but we invalidate the longer chain and continue on the first one
        # we have to disconnect and connect again to force sync
        blockHash = self.nodes[1].getblockhash(forkHeight + 1)
        self.nodes[0].invalidateblock(blockHash)
        self.nodes[1].invalidateblock(blockHash)
        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes)
        assert(self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())

        self.one_test(allKeys0, 0)
        self.one_test(allKeys0, 0)
        self.one_test(allKeys0, 0)


if __name__ == '__main__':
    CreateMinerInfoTest().main()
