#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.


from test_framework.miner_id import MinerIdKeys, make_miner_id_block
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_equal, bytes_to_hex_str, hex_str_to_bytes, disconnect_nodes_bi, connect_nodes_bi, rpc_port, p2p_port
from test_framework.script import CScript, OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import CTransaction, ToHex, CTxIn, CTxOut, COutPoint, FromHex, sha256
from test_framework.blocktools import create_block, create_coinbase
from test_framework.address import key_to_p2pkh
from pathlib import Path
from time import sleep

import json
import http.client as httplib
import os

'''
This test is identical to bsv-authconntest.py but wrong ip addresses are advertised
resulting in an expected authentication failure
'''


class AllKeys:
    last_seed_number = 1

    def __init__(self):
        self.fundingKeys = MinerIdKeys("0{}".format(AllKeys.last_seed_number + 6))
        AllKeys.last_seed_number += 6 + 1


class AuthConnTestReputation(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

        args = ['-mineridreputation_m=1', '-mineridreputation_n=10', '-disablesafemode=1', '-mindebugrejectionfee=0', '-paytxfee=0.00003']
        self.extra_args = [args + ['-mineridgeneratorurl=http://127.0.0.1:9002', '-mineridgeneratoralias=testMiner0'],
                           args + ['-mineridgeneratorurl=http://127.0.0.1:9003', '-mineridgeneratoralias=testMiner1']]

        self.miner_names = ["name1", "name2"]

    def setup_chain(self):
        #super().setup_chain()
        # Append rpcauth to bitcoin.conf before initialization

        rpcuser0 = "rpcuser=rpcuser0"
        rpcpassword0 = "rpcpassword=rpcpassword0"
        rpcuser1 = "rpcuser=rpcuser1"
        rpcpassword1 = "rpcpassword=rpcpassword1"

        user0 = "user=user0"
        password0 = "password=password0"
        user1 = "user=user1"
        password1 = "password=password1"

        rpcport0 = "rpcport=" + str(rpc_port(0))
        rpcport1 = "rpcport=" + str(rpc_port(1))

        def make_datadir (n):
            datadir = os.path.join(self.options.tmpdir, "node" + str(n))
            if not os.path.isdir(datadir):
                os.makedirs(datadir)
            return datadir

        with open(os.path.join(make_datadir(0), "bitcoin.conf"), 'a', encoding='utf8') as f:
            f.write(rpcuser0 + "\n")
            f.write(user0 + "\n")
            f.write(rpcpassword0 + "\n")
            f.write(password0 + "\n")
            f.write(rpcport0 + "\n")
            f.write("regtest=1\n")
            f.write("debug=1\n")
            f.write("port=" + str(p2p_port(0)) + "\n")
            f.write("shrinkdebugfile=0\n")

        with open(os.path.join(make_datadir(1), "bitcoin.conf"), 'a', encoding='utf8') as f:
            f.write(rpcuser1 + "\n")
            f.write(user1 + "\n")
            f.write(rpcpassword1 + "\n")
            f.write(password1 + "\n")
            f.write(rpcport1 + "\n")
            f.write("regtest=1\n")
            f.write("debug=1\n")
            f.write("port=" + str(p2p_port(1)) + "\n")
            f.write("shrinkdebugfile=0\n")

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

        with open(datapath + '/.minerinfotxsigningkey.dat', 'a') as f:
            f.write(fundingKeyJson)
        with open(datapath + '/minerinfotxfunding.dat', 'a') as f:
            f.write(fundingSeedJson)

    def one_test(self, allKeys, http_conns, aliases, nodenum):

        # create the minerinfodoc transaction and ensure it is in the mempool
        # create a dummy transaction to compare behaviour
        node = self.nodes[nodenum]
        height = node.getblockcount() + 1

        def signWithService(message):
            hashToSign = sha256(message)
            hex = bytes_to_hex_str(hashToSign)
            http_conns[nodenum].request('GET', "/minerid/{}/pksign/{}/".format(aliases[nodenum], hex))
            response = http_conns[nodenum].getresponse()
            signature = response.read()
            signature = json.loads(signature)

            return signature["signature"]

        minerinfotx_parameters = {
            'height': height,
            'name': self.miner_names[nodenum],
            'publicIP': '127.0.0.1',
            'publicPort': str(rpc_port(nodenum)),
            'minerKeys': signWithService,
            'revocationKeys':  None,
            'prev_minerKeys': None,
            'prev_revocationKeys': None,
            'pubCompromisedMinerKeyHex': None
        }

        http_conns[nodenum].request('GET', "/opreturn/{}/{}/".format(aliases[nodenum],  height))
        response = http_conns[nodenum].getresponse()
        scriptPubKey = response.read()
        scriptPubKey = scriptPubKey.decode('ascii')

        txid = node.createminerinfotx(scriptPubKey)
        wait_until (lambda: txid in node.getrawmempool())

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

    def isAuthenticated (self, nodenum):
        peerinfo = self.nodes[nodenum].getpeerinfo()
        if len(peerinfo) > 1:
            if (peerinfo[1]["authconn"]):
                return True
        return False

    def run_test(self):

        homedir = os.path.expanduser("~")

        # make this test only if minerid generator is installed on this machine
        if not os.path.isdir(homedir + '/minerid-reference0'):
            return
        if not os.path.isdir(homedir + '/minerid-reference1'):
            return

        allKeys0 = AllKeys()
        allKeys1 = AllKeys()

        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)

        os.system("rm -rf ~/.minerid-client/testMiner0")
        os.system("rm ~/.keystore/testMiner0_*.key")
        os.system("rm ~/.revocationkeystore/testMiner0_*.key")

        os.system("rm -rf ~/.minerid-client/testMiner1")
        os.system("rm ~/.keystore/testMiner1_*.key")
        os.system("rm ~/.revocationkeystore/testMiner1_*.key")

        os.system("sed -i 's/.rpcPort.:[ ]*[0-9]*/\"rpcPort\": {}/'  ~/minerid-reference0/config/default.json".format(rpc_port(0)))
        os.system("sed -i 's/.rpcPort.:[ ]*[0-9]*/\"rpcPort\": {}/'  ~/minerid-reference1/config/default.json".format(rpc_port(1)))

        os.system("cd  ~/minerid-reference0; npm run cli -- generateminerid --name testMiner0")
        os.system("cd  ~/minerid-reference1; npm run cli -- generateminerid --name testMiner1")

        # here we write a bad ip address to purpusefully fail the ip address check during authentication
        with open(homedir + "/.minerid-client/testMiner0/minerIdOptionalData", "w+") as w:
            w.write("""\
            {
                "extensions": {
                    "PublicIP":"127.0.0.2",
                    "PublicPort": 8888
                }
            }
            """)

        with open(homedir + "/.minerid-client/testMiner1/minerIdOptionalData", "w+") as w:
            w.write("""\
            {
                "extensions": {
                    "PublicIP":"127.0.0.2",
                    "PublicPort": 8888
                }
            }
            """)

        os.system("cd  ~/minerid-reference0; npm start&")
        os.system("cd  ~/minerid-reference1; npm start&")
        sleep(1) # give the server some time to start

        http_conns = []
        http_conns.append(httplib.HTTPConnection("127.0.0.1:9002"))
        http_conns.append(httplib.HTTPConnection("127.0.0.1:9003"))
        aliases = ["testMiner0", "testMiner1"]

        # store funding keys and funding txid
        block0, coinbase_tx0 = self.make_block_with_coinbase(self.nodes[0])
        self.nodes[0].submitblock(ToHex(block0))
        wait_until(lambda: block0.hash == self.nodes[0].getbestblockhash(), timeout=10, check_interval=1)
        self.nodes[0].generate(1)
        self.sync_all()

        block1, coinbase_tx1 = self.make_block_with_coinbase(self.nodes[1])
        self.nodes[1].submitblock(ToHex(block1))
        wait_until(lambda: block1.hash == self.nodes[0].getbestblockhash(), timeout=10, check_interval=1)
        self.nodes[1].generate(101)
        self.sync_all()

        fundingSeedTx0 = self.create_funding_seed(self.nodes[0], keys=allKeys0.fundingKeys,
                                                  coinbase_tx=coinbase_tx0)
        self.store_funding_info(nodenum=0, keys=allKeys0.fundingKeys, txId=fundingSeedTx0.hash, index=0)
        self.nodes[0].generate(1)
        self.sync_all()

        fundingSeedTx1 = self.create_funding_seed(self.nodes[1], keys=allKeys1.fundingKeys,
                                                  coinbase_tx=coinbase_tx1)
        self.store_funding_info(nodenum=1, keys=allKeys1.fundingKeys, txId=fundingSeedTx1.hash, index=0)
        self.nodes[1].generate(1)
        self.sync_all()

        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)
        assert (not self.isAuthenticated(0))
        assert (not self.isAuthenticated(1))
        # m = 0 and n = 0 for node0
        # m = 0 and n = 0 for node1
        # following function increments block height by 2
        self.one_test(allKeys0, http_conns, aliases, nodenum=0)
        # m = 1, and n = 2 for node0
        # m = 0, and n = 2 for node1
        assert (not self.isAuthenticated(0))
        assert (not self.isAuthenticated(1))  # node0 reputation now good but need to reconnect

        # building reputation of node 1
        disconnect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 1)
        assert (not self.isAuthenticated(0))
        # this must fail because nodes advertise mismatching ip-addresses
        assert (not self.isAuthenticated(1))


if __name__ == '__main__':
    AuthConnTestReputation().main()
