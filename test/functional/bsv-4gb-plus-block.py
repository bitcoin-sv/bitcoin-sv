#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test the P2P behavior of connections to nodes for the extended > 4GB block messages.

Launch 3 bitcoind nodes.
    node0 will be fed large trnsactions and will mine a >4GB block.
    node1 will fetch the large block from node0 via a basic block message.
    node2 will fetch the large block from node0 via a cmpctblock message.

Create 2 connections to node0; 1 with the new protocol version 70016
                               1 with the old protocol version 70015

Mine a small block on the node.
    Verify that a standard format block message is received over both connections

Mine a >4GB block on the node.
    Put some large transactions in the nodes mempool.
    Mine a block containing those transactions.
    Verify that an extended format block message is received over the new connection.
    Verify that no block message is sent over the old connection.
    Verify that all real bitcoind nodes (version 70016) can sync to the > 4GB block.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import MY_VERSION, NodeConnCB, CTxOut, msg_tx, msg_block
from test_framework.util import wait_until, logger, check_for_log_msg, connect_nodes, disconnect_nodes_bi, sync_blocks
from test_framework.script import CScript, OP_TRUE, OP_FALSE, OP_RETURN, OP_DROP, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.cdefs import ONE_MEGABYTE, ONE_GIGABYTE
from test_framework.key import CECKey
from test_framework.blocktools import create_block, create_coinbase, create_tx

from operator import itemgetter


class MyConnCB(NodeConnCB):

    def __init__(self):
        super().__init__()
        self.block_count = 0

    def on_block(self, conn, message):
        super().on_block(conn, message)

        # Validate block to make sure it's arrived correctly
        assert message.block.is_valid()
        # Record block arrival
        self.block_count += 1

    def on_inv(self, conn, message):
        # Don't request txns; they're large and unnecessary for this test
        request = False
        for i in message.inv:
            if i.type != 1:
                request = True
                break
        if request:
            super().on_inv(conn, message)


class BigBlockTests(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.locking_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])

        self.nodeArgs = ['-genesisactivationheight=1',
                         '-blockmaxsize={}'.format(ONE_GIGABYTE * 5),
                         '-maxmempool=10000',
                         '-maxnonstdtxvalidationduration=100000',
                         '-maxtxnvalidatorasynctasksrunduration=100001',
                         '-blockdownloadtimeoutbasepercent=300']

        self.extra_args = [self.nodeArgs] * self.num_nodes

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args, timewait=int(1200 * self.options.timeoutfactor))
        self.start_nodes()

    # Create and send block with coinbase
    def make_coinbase(self, conn):
        tip = conn.rpc.getblock(conn.rpc.getbestblockhash())

        coinbase_tx = create_coinbase(tip["height"] + 1, self.coinbase_pubkey)
        coinbase_tx.rehash()

        block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
        block.solve()

        conn.send_message(msg_block(block))
        wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, timeout=int(30 * self.options.timeoutfactor))

        return coinbase_tx

    def sign_tx(self, tx, spendtx, n):
        scriptPubKey = bytearray(spendtx.vout[n].scriptPubKey)
        sighash = SignatureHashForkId(spendtx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spendtx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript([self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    # Generate some large transactions and put them in the mempool
    def create_and_send_transactions(self, conn, spendtx, num_of_transactions, money_to_spend=5000000000):
        for i in range(0, num_of_transactions):
            money_to_spend = money_to_spend - 500000000  # Large fee required for big txns
            tx = create_tx(spendtx, 0, money_to_spend, script=CScript([OP_DROP, OP_TRUE]))
            tx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN, bytearray([0x00] * (ONE_MEGABYTE * 880))])))
            self.sign_tx(tx, spendtx, 0)
            tx.rehash()

            conn.send_message(msg_tx(tx))
            wait_until(lambda: tx.hash in conn.rpc.getrawmempool(), timeout=int(360 * self.options.timeoutfactor))
            logger.info("Submitted txn {} of {}".format(i+1, num_of_transactions))
            assert conn.rpc.getmempoolinfo()['size'] == i+1

            spendtx = tx

    def run_test(self):
        # Get out of IBD
        self.nodes[0].generate(1)
        self.sync_all()

        # Stop node so we can restart it with our connections
        self.stop_node(0)

        # Disconnect node1 and node2 for now
        disconnect_nodes_bi(self.nodes, 1, 2)

        connArgs = [{"versionNum":MY_VERSION}, {"versionNum":70015}]
        with self.run_node_with_connections("Test old and new protocol versions", 0, self.nodeArgs, number_of_connections=2,
                                            connArgs=connArgs, cb_class=MyConnCB) as (newVerConn,oldVerConn):
            assert newVerConn.connected
            assert oldVerConn.connected

            # Generate small block, verify we get it over both connections
            self.nodes[0].generate(1)
            wait_until(lambda: newVerConn.cb.block_count == 1, timeout=int(30 * self.options.timeoutfactor))
            wait_until(lambda: oldVerConn.cb.block_count == 1, timeout=int(30 * self.options.timeoutfactor))

            # Get us a spendable output
            coinbase_tx = self.make_coinbase(newVerConn)
            self.nodes[0].generate(100)

            # Put some large txns into the nodes mempool until it exceeds 4GB in size
            self.create_and_send_transactions(newVerConn, coinbase_tx, 5)

            # Reconnect node0 and node2 and sync their blocks. Node2 will end up receiving the
            # large block via compact blocks
            connect_nodes(self.nodes, 0, 2)
            sync_blocks(itemgetter(0,2)(self.nodes))

            # Mine a >4GB block, verify we only get it over the new connection
            old_block_count = newVerConn.cb.block_count
            logger.info("Mining a big block")
            self.nodes[0].generate(1)
            assert(self.nodes[0].getmempoolinfo()['size'] == 0)
            logger.info("Waiting for block to arrive at test")
            wait_until(lambda: newVerConn.cb.block_count == old_block_count+1, timeout=int(1200 * self.options.timeoutfactor))

            # Look for log message saying we won't send to old peer
            wait_until(lambda: check_for_log_msg(self, "cannot be sent because it exceeds max P2P message limit", "/node0"))

            # Verify node2 gets the big block via a (not very) compact block
            wait_until(lambda: self.nodes[0].getbestblockhash() == self.nodes[2].getbestblockhash())
            peerinfo = self.nodes[2].getpeerinfo()
            assert(peerinfo[0]['bytesrecv_per_msg']['cmpctblock'] > 0)
            assert(peerinfo[0]['bytesrecv_per_msg']['blocktxn'] > 0)

            # Reconnect node0 to node1
            logger.info("Syncing bitcoind nodes to big block")
            connect_nodes(self.nodes, 0, 1)
            self.sync_all(timeout=int(1200 * self.options.timeoutfactor))

            # Verify node1 also got the big block
            assert(self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())


if __name__ == '__main__':
    BigBlockTests().main()
