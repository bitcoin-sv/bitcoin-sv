#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.miner_id import create_miner_info_scriptPubKey, MinerIdKeys, make_miner_id_block
from test_framework.test_framework import BitcoinTestFramework, sync_blocks
from test_framework.util import wait_until, assert_equal, bytes_to_hex_str, disconnect_nodes_bi, connect_nodes_bi
from test_framework.script import CScript, OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import CTransaction, ToHex, CTxIn, CTxOut, COutPoint, FromHex
from test_framework.blocktools import create_block, create_coinbase
from test_framework.address import key_to_p2pkh
from pathlib import Path
from time import sleep
import json

'''
- Create a miner info doc and send it to the node via the createminerinfotx rpc function.
- Create a chain of mineridinfo-txns where each such transaction funds the next.
- Retive a transaction-id via getminerinfotxid rpc function.
- Calling createminerinfotx rpc twice in sequence.
- Invalidate a minerinof-txn in the mempool by accepting a block from another miner.
- Call createminerinfotx with bad minerid document syntax.
- Stop/Restart nodes and check if the further minerinfo transactions can be created which
  proves the funding chain was persisted
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
        self.extra_args = [['-mindebugrejectionfee=0','-paytxfee=0.00003'],['-mindebugrejectionfee=0', '-paytxfee=0.00003']]

        self.TEST_call_create = 1
        self.TEST_call_create_twice = 2
        self.TEST_call_replace = 3
        self.TEST_call_replace_twice = 4
        self.TEST_other_invalidates_minerinfotx = 5
        self.TEST_call_create_with_bad_json_synatx = 6
        self.TEST_call_stop_start = 7

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
            'pubCompromisedMinerKeyHex': None
        }

        # create json with bad syntax. 'height' is wrongly a string here
        if test_case == self.TEST_call_create_with_bad_json_synatx:
            jsonOverrideWithBadSyntax = \
                '''
                {
                "version": "0.3",
                "height": "104.",
                "prevMinerId": "02f01cba3a948ebe7b54cae669257cf4d2cbbc558f04a5195a1e53e557c6ebf910",
                "prevMinerIdSig": "30450221009dcdcca62d6392d40bbadd88a56ff8e3c452d5d6dfa68d01df357d26c38c41ef02204d7a675c70aa968726725dd17caa019ab5d1cf52552630898302285d6ff11075",
                "minerId": "02f01cba3a948ebe7b54cae669257cf4d2cbbc558f04a5195a1e53e557c6ebf910",
                "prevRevocationKey": "0298af0027a004adb7e732d3486b042fb2c5b15171c9b11ef6830f2bea81130d05",
                "prevRevocationKeySig": "304502203ea19d12b0d8d6ce2a22807ae5beb56bfed31658a7b682cfdef1a61b85e0842d022100f67cd7d95a681bb5c4f6fd22b062ca69cbc52d837a12c18fdfbe971ce113685c",
                "revocationKey": "0298af0027a004adb7e732d3486b042fb2c5b15171c9b11ef6830f2bea81130d05",
                "extensions": {
                "PublicIP": "127.0.0.1",
                "PublicPort": "8333"
                }
                '''
        else:
            jsonOverrideWithBadSyntax = None

        scriptPubKey = create_miner_info_scriptPubKey (
            params=minerinfotx_parameters,
            json_override_string=jsonOverrideWithBadSyntax)

        txid = None
        try:
            txid = self.nodes[0].createminerinfotx(bytes_to_hex_str(scriptPubKey))
            wait_until(lambda: txid in self.nodes[0].getrawmempool(), timeout=10)
            if test_case == self.TEST_call_stop_start:
                self.stop_nodes()
                self.start_nodes()
                disconnect_nodes_bi(self.nodes, 0, 1)
                connect_nodes_bi(self.nodes, 0, 1)
                sleep(4)
                assert(txid not in self.nodes[0].getrawmempool())
                txid = self.nodes[0].createminerinfotx(bytes_to_hex_str(scriptPubKey))

        except Exception as e:
            if test_case != self.TEST_call_create_with_bad_json_synatx:
                raise e
            code = -1
            message = "failed to extract miner info document from scriptPubKey: doc parse error - ill-formed json"
            if code != e.error["code"]:
                raise AssertionError(
                    "Unexpected JSONRPC error code %i" % e.error["code"])
            if message not in e.error['message']:
                raise AssertionError(
                    "Expected substring not found:" + e.error['message'])
            return

        if test_case == self.TEST_call_create_twice:
            txid_twice = self.nodes[0].createminerinfotx(bytes_to_hex_str(scriptPubKey))
            assert(txid == txid_twice)

        if test_case == self.TEST_call_replace:
            minerinfotx_parameters['publicPort'] = '8334' # vary the port to get a different txid
            scriptPubKey = create_miner_info_scriptPubKey (minerinfotx_parameters)
            txid_twice = self.nodes[0].replaceminerinfotx(bytes_to_hex_str(scriptPubKey))
            assert(txid != txid_twice)
            txid = txid_twice

        # ensure the minerinfotx was not relayed to the connected node
        wait_until(lambda: txid in self.nodes[0].getrawmempool(), timeout=10)
        sleep(0.3) # to give time for relaying
        assert(txid not in self.nodes[1].getrawmempool())
        tx0 = self.nodes[0].getminerinfotxid()
        tx1 = self.nodes[1].getminerinfotxid()
        assert(tx0 == txid)
        assert(tx1 == None)

        # create a minerinfo block with coinbase referencing the minerinfo transaction
        minerInfoTx = FromHex(CTransaction(), self.nodes[0].getrawtransaction(txid))
        block = make_miner_id_block(self.nodes[0], minerinfotx_parameters, minerInfoTx=minerInfoTx)
        block_count = self.nodes[0].getblockcount()

        # node1 pushes a block just before we want to submit ours.
        # that should force out our minerinfo-txn from the mempool
        if (test_case == self.TEST_other_invalidates_minerinfotx):
            block_count0 = self.nodes[0].getblockcount()
            self.nodes[1].generate(1)
            wait_until(lambda: block_count0 + 1 == self.nodes[0].getblockcount(), timeout=10)
            wait_until (lambda: tx0 not in self.nodes[0].getrawmempool(), timeout=10, check_interval=1)
            return True

        self.nodes[0].submitblock(ToHex(block))
        wait_until (lambda: block_count + 1 == self.nodes[0].getblockcount())

        # check if the minerinfo-txn
        # was moved from the mempool into the new block
        assert(txid not in self.nodes[0].getrawmempool())
        bhash = self.nodes[0].getbestblockhash()
        block = self.nodes[0].getblock(bhash)
        assert(txid in block['tx'])

        # the connected node needs to validate the block containing
        # the minerinfotx successfully too.
        sync_blocks(self.nodes)
        bhash = self.nodes[1].getbestblockhash()
        block = self.nodes[1].getblock(bhash)
        wait_until(lambda: txid in block['tx'], timeout=10)

        tx0 = self.nodes[0].getminerinfotxid()
        tx1 = self.nodes[1].getminerinfotxid()
        assert(tx0 == None)
        assert(tx1 == None)

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
        self.nodes[0].generate(1)
        self.sync_all()

        # repeate test just to be sure
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_replace)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create_with_bad_json_synatx)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_stop_start)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create_twice)
        self.one_test(allKeys, fundingSeedTx, self.TEST_other_invalidates_minerinfotx)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create)

        # test persistence of funding information
        self.stop_nodes()
        self.start_nodes()
        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)
        self.one_test(allKeys, fundingSeedTx, self.TEST_call_create)


if __name__ == '__main__':
    CreateMinerInfoTest().main()
