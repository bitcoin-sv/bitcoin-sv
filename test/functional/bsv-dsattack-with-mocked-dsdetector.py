#!/usr/bin/env python3
# Copyright (c) 2021  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

import time
import json
import threading
from io import BytesIO


from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, connect_nodes, sync_blocks, disconnect_nodes_bi, p2p_port
from test_framework.key import CECKey
from test_framework.blocktools import create_block, create_coinbase
from test_framework.script import hash160, CScript, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL , SIGHASH_FORKID
from test_framework.mininode import CTransaction, CTxOut, CTxIn, COutPoint, ToHex, NetworkThread, NodeConn, NodeConnCB, msg_dsdetected, BlockDetails, CBlockHeader, DSMerkleProof, MerkleProofNode, FromHex, CBlock
from test_framework.authproxy import JSONRPCException

### BEGIN MOCKING CLASSES
#
# There are mocking classes for 3 objects
# - MockDsdetector (which is replaced with a docker immage in the integration test)
# - Exchange (an exchange that takes action after being notified of suspicieous events)
# - User (people/wallets sending fraudulent transactions)

from http.server import BaseHTTPRequestHandler, HTTPServer
import http.client as httplib
from functools import partial


class MockDsdetector():
    def __init__(self, testRig, node):
        self.peer = NodeConnCB()
        self.peer.add_connection(NodeConn('127.0.0.1', p2p_port(0), node, self.peer))
        NetworkThread().start()
        self.peer.wait_for_verack()
        testRig.dsdetector = self
        self.message = None

    def SendDsNotification(self):
        if self.message:
            self.peer.send_and_ping(self.message)

    def CheckForDoubleSpends (self, nodes):
        spent_inputs = []
        seen_transactions = []
        ds_counter = 0
        for node in nodes:
            for height in range(node.getblockcount() + 1):
                blockhash = node.getblockhash(height)
                block = node.getblock(blockhash, 2)
                blockHex = node.getblock(blockhash, False)
                for txraw in block['tx'][1:]:  # exclude coinbase
                    # skip the identical transactions in the two chains, they are no double spends
                    if txraw['txid'] in seen_transactions:
                        continue
                    else:
                        seen_transactions.append(txraw['txid'])
                    for i in txraw['vin']:
                        utxoA = (i['txid'], i['vout'])
                        blockA = FromHex(CBlock(), blockHex)
                        txA = FromHex(CTransaction(), txraw['hex'])
                        foundB = [j for j in spent_inputs if j['utxo'] == utxoA]
                        if foundB:
                            ds_counter += 1
                            foundB = foundB[0]
                            blockB = foundB['block']
                            txB = foundB['tx']
                            txA.rehash()

                            txB.rehash()

                            blockA.vtx[0].rehash()
                            blockB.vtx[0].rehash()
                            sha256_A = blockA.vtx[0].sha256
                            sha256_B = blockB.vtx[0].sha256

                            dsdMessage = msg_dsdetected(blocksDetails=[
                                BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(sha256_A)])),
                                BlockDetails([CBlockHeader(blockB)], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(sha256_B)]))])
                            self.message = dsdMessage

                            dsdBytes = dsdMessage.serialize()
                            dsdMessageDeserialized = msg_dsdetected()
                            dsdMessageDeserialized.deserialize(BytesIO(dsdBytes))
                            assert_equal(str(dsdMessage), str(dsdMessageDeserialized))

                            break
                        else:
                            spent_inputs.append({'txid':txraw['txid'], 'tx':txA, 'utxo':utxoA, 'block':blockA})

        return ds_counter


class Exchange():
    def __init__(self, testRig, node):
        testRig.exchange = self
        self.node = node
        self.invalidblockhash = None

    def CatchNotification_InvalidateIfRequired(self, node, bad_block_hint):
        asJson = self.get_JSON_notification()
        # In reality the exchange will choose the block to invalidate via human interaction.
        # Here give a hint via the second parameter.
        # We merly assert that the chosen block can be found in the notficication
        for b in asJson['blocks']:
            hash = b['divergentBlockHash']
            if hash == bad_block_hint:
                node.invalidateblock(bad_block_hint)
                node.ignoresafemodeforblock(bad_block_hint)
                return True
        return False

    def start_webhook_server(self):
        self.server = HTTPServer(('localhost', 8888), partial(WebHookService))
        self.serverThread = threading.Thread(target=self.server.serve_forever)
        self.serverThread.deamon = True
        self.serverThread.start()
        self.conn = httplib.HTTPConnection("localhost:8888")

    def stop_webhook_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.serverThread.join()

    def get_JSON_notification(self):
        # Query webhook server for the JSON notification
        self.conn.request('GET', "/dsdetected/webhook/query")
        response = self.conn.getresponse()
        # We either got it or not
        if (response.status != 200):
            return None
        return json.loads(response.read())


# A dummy webhook service that accepts POST request and stores the JSON string sent
# This JSON string can then be retrieved through GET request for evaluation.
class WebHookService(BaseHTTPRequestHandler):
    # On every POST/GET new WebHookService instance is created. To be able to share the last JSON received between instances, static variable is used.
    lastReceivedJSON = None

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def do_POST(self):
        # Check we get it from the proper path and with proper content
        assert_equal(self.path, "/dsdetected/webhook")
        assert_equal(self.headers['Content-Type'], "application/json")
        # Read JSON string and store it
        WebHookService.lastReceivedJSON = self.rfile.read(int(self.headers['Content-Length']))
        self.send_response(200)
        self.end_headers()

    def do_GET(self):
        # This dummy service receives from this path only
        assert_equal(self.path, "/dsdetected/webhook/query")
        if (WebHookService.lastReceivedJSON == None):
            self.send_response(400, "No JSON received")
            self.end_headers()
            return
        # Forward the last JSON string
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(WebHookService.lastReceivedJSON)
        # Reset it to None to be able to test if sending dsdetected message will create a webhook notification or not
        WebHookService.lastReceivedJSON = None

    # To avoid filling up the stderr on every successful request
    def log_request(self, code):
        return


class User:

    def __init__(self, secret_bytes):
        self.key = CECKey()
        self.key.set_secretbytes(secret_bytes)
        self.pubkey = self.key.get_pubkey()

    def spend_to_pkh (self, node, spend_tx, n, amount, to_pubkey):
        value = int(amount)
        scriptPubKey = CScript([OP_DUP, OP_HASH160, hash160(to_pubkey), OP_EQUALVERIFY, OP_CHECKSIG])

        tx = CTransaction()
        assert (n < len(spend_tx.vout))
        tx.vin.append(CTxIn(COutPoint(spend_tx.sha256, n), b"", 0xffffffff))
        tx.vout.append(CTxOut(value, scriptPubKey))
        tx.calc_sha256()

        self.__sign_tx(tx, spend_tx, n)
        tx.rehash()
        node.sendrawtransaction(ToHex(tx), False, True)

        if False: # if we want to get the tx as json formatted output for debugging
            tx_json = node.decoderawtransaction(ToHex(tx))
            for output in tx_json['vout']:
                output['value'] = float(output['value'])
            text = json.dumps(tx_json, indent=4)
            print("ds transaction:", text)

        return tx

    def __sign_tx(self, sign_tx, spend_tx, n):
        sighash = SignatureHashForkId(spend_tx.vout[n].scriptPubKey, sign_tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        sign_tx.vin[0].scriptSig = CScript([self.key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID])), self.pubkey])

#
### END MOCKING CLASSES


class CompetingChainsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ["-txindex=1", "-disablesafemode=0", "-debug=1", "-dsdetectedwebhookurl=http://127.0.0.1:8888/dsdetected/webhook", "-banscore=100000"],
            ["-txindex=1", "-disablesafemode=0", "-debug=1"],
        ]
        self.nbDoubleSpends = 1  # needs to be one for the mocked ds-detector
        self.lenChain0 = 8  # more than SAFE_MODE_MAX_VALID_FORK_LENGTH 7
        self.lenChain1 = 18  # less than SAFE_MODE_MAX_VALID_FORK_DISTANCE (72)
        self.FORK_ROOT_HEIGHT = 200

    def setup_network(self):
        self.setup_nodes()

    def make_coinbase(self, conn_rpc):
        tip = conn_rpc.getblock(conn_rpc.getbestblockhash())
        coinbase_tx = create_coinbase(tip["height"] + 1)
        block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
        block.solve()
        conn_rpc.submitblock(ToHex(block))
        return coinbase_tx

    def send_funds_to_attacker (self, node, attacker, coinbase_tx):
        funding_amount = int(coinbase_tx.vout[0].nValue / self.nbDoubleSpends)
        funding_tx = CTransaction()

        funding_tx.vin.append(CTxIn(COutPoint(coinbase_tx.sha256, 0), b"", 0xffffffff))

        scriptPubKey = CScript([OP_DUP, OP_HASH160, hash160(attacker.pubkey), OP_EQUALVERIFY, OP_CHECKSIG])
        for i in range(self.nbDoubleSpends):
            funding_tx.vout.append(CTxOut(funding_amount, scriptPubKey))

        funding_tx.rehash()
        funding_txid = node.sendrawtransaction(ToHex(funding_tx), False, True)
        assert_equal(node.getrawmempool(), [funding_txid])
        return funding_tx

    def run_test(self):

        # Test 1:
        # 1. fund an attacker for the test on node0
        # 2. progress to block height 200
        # 3. sync all nodes
        # 4. disconnect the two nodes forking at block height 200
        # 5. spend attackers fund in node0 and double spend them in node1
        # 6. Assert that the two chains actually contain the attackers double spends

        attacker = User(b"horsebattery")
        friend0_of_attacker = User(b"fatstack")
        friend1_of_attacker = User(b"fatheap")
        node0 = self.nodes[0]  # victim node
        node1 = self.nodes[1]  # node under control of attacker

        # The dsdetector is conneget_JSON_notificationcted to node- to which it sends ds messages
        dsdetector = MockDsdetector(self, node0)
        # The exchange is connected to a node0 in which it can invalidate chains
        # Additionally the Exchange listens to webhook messages on port 8888.
        # node0 will send webhook messages because node0 is connectged to the dsdetector
        exchange = Exchange(self, node0)
        exchange.start_webhook_server()

        self.log.info("fund attacker. We fund him at height 200 -2")
        self.log.info("just for debugging convenience. We plan to fork at height 200")
        coinbase_tx = self.make_coinbase(node0)
        node0.generate(self.FORK_ROOT_HEIGHT - 2)
        assert (node0.getblockcount() == self.FORK_ROOT_HEIGHT - 1)

        self.log.info("fund attacker")
        funding_tx = self.send_funds_to_attacker (node0, attacker, coinbase_tx)
        node0.generate(1)
        assert (node0.getblockcount() == self.FORK_ROOT_HEIGHT + 0)

        self.log.info("sync nodes. All nodes have the same chain and funding transactions after syncing")
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes)
        disconnect_nodes_bi(self.nodes, 0, 1)

        # fork from here
        assert (node0.getblockcount() == node1.getblockcount())

        self.log.info("spends attackers funds in node0")
        for i in range(self.nbDoubleSpends):
            attacker.spend_to_pkh(node0, funding_tx, i, funding_tx.vout[i].nValue, friend0_of_attacker.pubkey)
        node0.generate(1)
        assert (node0.getblockcount() == self.FORK_ROOT_HEIGHT + 1)

        self.log.info("double spend attacker funds in node1")
        for i in range(self.nbDoubleSpends):
            attacker.spend_to_pkh(node1, funding_tx, i, funding_tx.vout[i].nValue, friend1_of_attacker.pubkey)

        node1.generate(1)
        first_bad_block = node1.getbestblockhash()

        assert (node1.getblockcount() == self.FORK_ROOT_HEIGHT + 1)

        self.log.info("check that funds have been double spent to different addresses")
        assert(dsdetector.CheckForDoubleSpends(self.nodes[0:2]) == self.nbDoubleSpends)

        # Test 2.
        # 1. Progress the two competing chains in node0 and node1 to different lengths (configurable).
        #    node1 shall hold the longer chain and is the one controlled by the attacker.
        #    The two nodes are not connected to each other directly or indirectly and at this point
        #    contain the doulbe spends we have prapared.
        # 2. connect the nodes and sync them to force a reorg
        # 3. Assert that all double spends disappeared - which nontheless means the attack succeeded.
        self.log.info("Mine lenChain0 blocks on node0")

        node0.generate(self.lenChain0 - 1)
        assert(node0.getblockcount() == self.FORK_ROOT_HEIGHT + self.lenChain0)

        self.log.info("Mine competing lenChain1 blocks on node1")
        node1.generate(self.lenChain1 - 1)
        assert(node1.getblockcount() == self.FORK_ROOT_HEIGHT + self.lenChain1)

        self.log.info("Connect nodes to force a reorg")
        # note that reorg may not happen with next safe mode specification and
        # and safe mode may be entered
        connect_nodes(self.nodes, 1, 0)
        sync_blocks(self.nodes[0:2])
        if self.lenChain1 > self.lenChain0:
            assert(node0.getblockcount() == self.FORK_ROOT_HEIGHT + self.lenChain1)
        else:
            assert(node1.getblockcount() == self.FORK_ROOT_HEIGHT + self.lenChain0)

        self.log.info("check that both nodes have the same chains")
        lastblock0 = node0.getbestblockhash()
        lastblock1 = node1.getbestblockhash()
        assert(lastblock0 == lastblock1)

        self.log.info("check that double spends have been removed")
        assert (dsdetector.CheckForDoubleSpends(self.nodes[0:2]) == 0)

        # Test 3: Assert that safemode has been reached
        try:
            node0.rpc.getbalance()
            assert False, "Should not come to here, should raise exception in line above."
        except JSONRPCException as e:
            assert e.error["message"] == "Safe mode: Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues. A large valid fork has been detected."

        # Test 4: Assert that safemode is exited if the offending chain is invalidated
        # We know a ds notification has been sent hence the exchange will react to this
        # notification by invalidating the bad chain which also make sthe node exit safemode.
        dsdetector.SendDsNotification()
        while not exchange.CatchNotification_InvalidateIfRequired(node0, first_bad_block):
            time.sleep(1)
        balance = node0.rpc.getbalance()
        assert (balance != None)

        exchange.stop_webhook_server()


if __name__ == '__main__':
    CompetingChainsTest().main()
