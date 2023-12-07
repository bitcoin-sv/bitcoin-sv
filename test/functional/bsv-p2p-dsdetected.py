#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from functools import partial
from http.server import BaseHTTPRequestHandler, HTTPServer
from io import BytesIO
from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import make_block, create_tx, send_by_headers, wait_for_tip
from test_framework.mininode import NodeConnCB, NodeConn, NetworkThread, CTxOut, COIN, msg_dsdetected, CBlockHeader, BlockDetails, DSMerkleProof, MerkleProofNode, FromHex, CBlock
from test_framework.util import p2p_port, assert_equal
from test_framework.script import CScript, OP_TRUE
import http.client as httplib
import json
import os
import random
import threading


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


class fake_msg_dsdetected():
    command = b"dsdetected"

    def serialize(self):
        r = os.urandom(random.randint(100, 1000))
        return r


class DSDetectedTests(BitcoinTestFramework):

    def __del__(self):
        self.stop_webhook_server()

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # Set webhook url with IP, port and endpoint
        self.extra_args = [["-dsdetectedwebhookurl=http://127.0.0.1:8888/dsdetected/webhook", "-banscore=100000","-safemodemaxforkdistance=288"]]

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

    # Method will create a random number of branches containing random number of simple blocks (with a coinbase transaction).
    # The last block/tip in each branch will contain an additional transaction. These transactions will spend the same output (randomly selected from txsWithUtxos).
    # First two branches will fork at commonBlock which is regarded as the common block for all branches.
    # Other branches will fork at randomly selected block in one of the previously created branch.
    # This will create a random block tree, for example:
    #           commonBlock
    #               *
    #          /branch1  \branch2
    #         *           *
    #        /             \
    #       *               *
    #      / \branch3        \
    #     *   *               *
    #    /            branch5/ \
    #   *                   *   *
    #  / \branch4          /
    # *   *               *
    #      \
    #       *
    #        \
    #         *
    # Resulted array of branches can then be used to generate a dsdetected P2P message.
    # maxNumberOfBranches must not be less that 2.
    # maxNumberOfBlocksPerBranch must not be less that 1.
    # commonBlock should be a CBlock() instance.
    # txsWithUtxos should be an array of transactions [CTransaction()]. Each transaction should have spendable outputs.
    def createRandomBlockTree(self, maxNumberOfBranches, maxNumberOfBlocksPerBranch, commonBlock, txsWithUtxos):

        # Select a random transaction which output we will double-spend
        spendTransaction = txsWithUtxos[random.randrange(len(txsWithUtxos))]
        spendOutput = random.randrange(len(spendTransaction.vout))

        # Random number of branches (at least 2)
        nBranches = random.randint(2, maxNumberOfBranches)
        branches = []
        # Each branch will spend the same output, but for diversity each will spend a different amount (fraction of an output value)
        valueFraction = 1.0/(nBranches+1.0)
        valueFactor = 1.0
        last_block_time = commonBlock.nTime
        for _ in range(nBranches):
            branch = []
            if len(branches) < 2:
                # First two branches will start from the commonBlock
                previousBlock = commonBlock
            else:
                # Other branches will be forked in random branches at random blocks
                randomBranch = branches[random.randrange(len(branches))]
                # Make sure we don't "fork" at last block, making one single branch instead of an actual fork
                if len(randomBranch) == 1:
                    previousBlock = commonBlock
                else:
                    # Add all blocks from first block until previous block in forked branch to the new branch
                    # dsdetected message requires a list of block headers from double-spending block up to the common block
                    branch = randomBranch[:random.randrange(1, len(randomBranch))]
                    previousBlock = branch[-1]
            for _ in range(random.randint(1, maxNumberOfBlocksPerBranch)):
                # To make unique blocks we need to set the last_block_time
                previousBlock, last_block_time = make_block(None, parent_block=previousBlock, last_block_time=last_block_time)
                branch.append(previousBlock)
            # Last block should contain a double-spend transaction but we intentionally spend a different amount for each to make transactions unique
            valueFactor = valueFactor - valueFraction
            dsTx = create_tx(spendTransaction, spendOutput, int(valueFactor*spendTransaction.vout[spendOutput].nValue))
            branch[-1].vtx.append(dsTx)
            branch[-1].hashMerkleRoot = branch[-1].calc_merkle_root()
            branch[-1].solve()
            branches.append(branch)
        return branches

    def createDsDetectedMessageFromBlockTree(self, branches):
        blocksDetails = []
        for blocks in branches:
            # Last block in every branch contains two transactions: a coinbase and a double-spend transaction.
            merkleProof = DSMerkleProof(1, blocks[-1].vtx[1], blocks[-1].hashMerkleRoot, [MerkleProofNode(blocks[-1].vtx[0].sha256)])
            # Block headers should be ordered from tip to the first block
            blockHeaders = [CBlockHeader(block) for block in reversed(blocks)]
            # Each branch is a block detail with its block headers and merkle proof for double-spend transaction
            blocksDetails.append(BlockDetails(blockHeaders, merkleProof))
        return msg_dsdetected(blocksDetails=blocksDetails)

    def run_test(self):

        # Turn on a webhook server
        self.start_webhook_server()

        # Create a P2P connection
        node = self.nodes[0]
        peer = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), node, peer)
        peer.add_connection(connection)
        NetworkThread().start()
        peer.wait_for_verack()

        # Create an initial block with a coinbase we will split into multiple utxos
        initialBlock, _ = make_block(connection)
        coinbaseTx = initialBlock.vtx[0]

        send_by_headers(connection, [initialBlock], do_send_blocks=True)
        wait_for_tip(connection, initialBlock.hash)

        node.generate(101)
        block101hex = node.getblock(node.getbestblockhash(), False)
        block101dict = node.getblock(node.getbestblockhash(), 2)
        block101 = FromHex(CBlock(), block101hex)
        block101.height = block101dict['height']
        block101.rehash()

        # Create a block with a transaction spending coinbaseTx of a previous block and making multiple outputs for future transactions to spend
        utxoBlock, _ = make_block(connection, parent_block=block101)
        utxoTx = create_tx(coinbaseTx, 0, 1*COIN)

        # Create additional 48 outputs (we let 1 COIN as fee)
        for _ in range(48):
            utxoTx.vout.append(CTxOut(1*COIN, CScript([OP_TRUE])))
        # Add to block
        utxoTx.rehash()

        utxoBlock.vtx.append(utxoTx)
        utxoBlock.hashMerkleRoot = utxoBlock.calc_merkle_root()
        utxoBlock.solve()

        send_by_headers(connection, [utxoBlock], do_send_blocks=True)
        wait_for_tip(connection, utxoBlock.hash)

        # Make sure serialization/deserialization works as expected
        # Create dsdetected message. The content is not important here.
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(utxoBlock), CBlockHeader(initialBlock)], DSMerkleProof(1, utxoTx, utxoBlock.hashMerkleRoot, [MerkleProofNode(utxoBlock.vtx[0].sha256)]))])
        dsdBytes = dsdMessage.serialize()
        dsdMessageDeserialized = msg_dsdetected()
        dsdMessageDeserialized.deserialize(BytesIO(dsdBytes))
        assert_equal(str(dsdMessage), str(dsdMessageDeserialized))

        # Send a message containing random bytes. Webhook should not receive the notification.
        peer.send_and_ping(fake_msg_dsdetected())
        assert_equal(self.get_JSON_notification(), None)

        # Create two blocks with transactions spending the same utxo
        blockA, _ = make_block(connection, parent_block=utxoBlock)
        blockB, _ = make_block(connection, parent_block=utxoBlock)
        blockF, _ = make_block(connection, parent_block=utxoBlock)
        txA = create_tx(utxoBlock.vtx[1], 0, int(0.8*COIN))
        txB = create_tx(utxoBlock.vtx[1], 0, int(0.9*COIN))
        txF = create_tx(utxoBlock.vtx[1], 0, int(0.7*COIN))
        txA.rehash()
        txB.rehash()
        txF.rehash()
        blockA.vtx.append(txA)
        blockB.vtx.append(txB)
        blockF.vtx.append(txF)
        blockA.hashMerkleRoot = blockA.calc_merkle_root()
        blockB.hashMerkleRoot = blockB.calc_merkle_root()
        blockF.hashMerkleRoot = blockF.calc_merkle_root()
        blockA.calc_sha256()
        blockB.calc_sha256()
        blockF.calc_sha256()
        blockA.solve()
        blockB.solve()
        blockF.solve()

        start_banscore = node.getpeerinfo()[0]['banscore']

        # Webhook should not receive the notification if we send dsdetected message with only one block detail.
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with two block details and one is containing no headers.
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message where last headers in block details do not have a common previous block hash.
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(utxoBlock)], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message where block details does not have headers in proper order.
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(utxoBlock), CBlockHeader(blockB)], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with the empty merkle proof.
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof())])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with the wrong index in the merkle proof (merkle root validation should fail)
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(0, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with the wrong transaction in the merkle proof (merkle root validation should fail)
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(1, txA, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with the wrong merkle root (merkle root validation should fail)
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(1, txB, blockA.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with the wrong merkle proof (merkle root validation should fail)
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockA.hashMerkleRoot)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with the merkle proof having an additional unexpected node (merkle root validation should fail)
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256), MerkleProofNode(blockA.hashMerkleRoot)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with the valid proof, but transaction is a coinbase transaction
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA,           blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(0, blockB.vtx[0], blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[1].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if we send dsdetected message with transactions that are not double spending
        # Create a block similar as before, but with a transaction spending a different utxo
        blockC, _ = make_block(connection, parent_block=utxoBlock)
        txC = create_tx(utxoBlock.vtx[1], 1, int(0.7*COIN))
        blockC.vtx.append(txC)
        blockC.hashMerkleRoot = blockC.calc_merkle_root()
        blockC.solve()
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockC)], DSMerkleProof(1, txC, blockC.hashMerkleRoot, [MerkleProofNode(blockC.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if the two double spending transactions are actually the same transaction (having same txid)
        # Create a block similar as before, but with a transaction spending a different utxo
        blockD, _ = make_block(connection, parent_block=utxoBlock)
        blockD.vtx.append(txA)
        blockD.hashMerkleRoot = blockD.calc_merkle_root()
        blockD.solve()
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockD)], DSMerkleProof(1, txA, blockD.hashMerkleRoot, [MerkleProofNode(blockD.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Webhook should not receive the notification if header cannot pow
        # note hat pow is so easy in regtest that nonce can often be hence we have to select the nonce carefully
        blockE, _ = make_block(connection, parent_block=utxoBlock)
        blockE.vtx.append(txB)
        blockE.hashMerkleRoot = blockE.calc_merkle_root()
        nonce = blockE.nNonce
        while True:
            blockE.solve()
            if blockE.nNonce > nonce:
                blockE.nNonce = nonce
                break
            nonce += 1
            blockE.nNonce = nonce

        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockE)], DSMerkleProof(1, txB, blockE.hashMerkleRoot, [MerkleProofNode(blockE.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        end_banscore = node.getpeerinfo()[0]['banscore']
        assert ((end_banscore - start_banscore) / 10 == 13)  # because we have 13 negative tests so far

        # Finally, webhook should receive the notification if we send a proper dsdetected message
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        json_notification = self.get_JSON_notification()
        # remove diverentBlockHash so we can compare with the ds-message
        assert(json_notification != None)
        for e in json_notification['blocks']:
            del e['divergentBlockHash']
        assert_equal(str(dsdMessage), str(msg_dsdetected(json_notification=json_notification)))

        # Repeat previous test but change the order of the BlockDetails, the node should identify this as a duplicate
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockB)], DSMerkleProof(1, txB, blockB.hashMerkleRoot, [MerkleProofNode(blockB.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # repeat previous test but generate many blocks in the node to age the notificatoin message.
        # very old notification messages shall be ignored. We use the same thresholds as safe mode.
        # We will hardcode this threshold for now until branch we depend on is merged
        node.generate (289)
        dsdMessage = msg_dsdetected(blocksDetails=[
            BlockDetails([CBlockHeader(blockA)], DSMerkleProof(1, txA, blockA.hashMerkleRoot, [MerkleProofNode(blockA.vtx[0].sha256)])),
            BlockDetails([CBlockHeader(blockF)], DSMerkleProof(1, txF, blockF.hashMerkleRoot, [MerkleProofNode(blockF.vtx[0].sha256)]))])
        peer.send_and_ping(dsdMessage)
        assert_equal(self.get_JSON_notification(), None)

        # Create number of random valid block trees and send dsdetected P2P message for each
        maxNumberOfBranches = 10
        maxNumberOfBlocksPerBranch = 30
        for _ in range(10):
            blockTree = self.createRandomBlockTree(maxNumberOfBranches, maxNumberOfBlocksPerBranch, utxoBlock, [utxoBlock.vtx[1]])
            dsdMessage = self.createDsDetectedMessageFromBlockTree(blockTree)
            peer.send_and_ping(dsdMessage)
            # Notification should be received as generated dsdetected message is valid
            json_notification = self.get_JSON_notification()
            # remove diverentBlockHash so we can compare with the ds-message
            assert (json_notification != None)
            for e in json_notification['blocks']:
                del e['divergentBlockHash']
            assert_equal(str(dsdMessage), str(msg_dsdetected(json_notification=json_notification)))

        self.stop_webhook_server()


if __name__ == '__main__':
    DSDetectedTests().main()
