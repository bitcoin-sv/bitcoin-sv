#!/usr/bin/env python3
# Copyright (c) 2019-2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test some ZMQ notifications/messages when transaction get removed from mempool.

Body of ZMQ message is in json format: {txid: hexstring, reason: string,
                                        collidedWith: {txid: hexstring, size: integer, hex: hexstring},
                                        blockhash: hexstring}
The fields collidedWith and blockhash are only present when reason for removal is collision-in-block-tx

To see if zmq notifiers works, we only check for particular
reasons when transactions gets removed from mempool.

Some reasons we can get from -zmqpubremovedfrommempoolblock notifier are:
 - included-in-block (when tx gets included in block)
 - reorg (when reorg is happening and tx tries to spend immature coin)
Some reason we can get from -zmqpubdiscardedfrommempool notifier is:
 - collision-in-block-tx (when we have two tx1, tx2 using same input on two different nodes,
                         and then tx2 gets mined in block, when the block is propagated
                         the other tx1 will be removed from mempool.)

Test case 1:
    - send two transactions tx1, tx2 to node0 and mine a block
    - receive included in block notification
Test case 2:
    - invalidate blocks inputs that tx1, tx2 use become immature
    - receive removed for reorg notification
Test case 3:
    - disconnect nodes n0, n1
    - create tx1 on n0, tx2 on n1 that uses same input.
    - mine a block on n1
    - connect nodes n0, n1
    - receive conflict in block tx notification
Test case 4:
    - disconnect nodes n0, n1
    - mine few blocks on n1
    - create tx2 on n1 and mine a block
    - create tx1 on n0 that uses same input as tx2 and mine a block
    - connect nodes n0, n1
    - receive conflict in block tx notification
"""

import json

from test_framework.script import CTransaction, CScript, OP_TRUE, CTxOut
from test_framework.test_framework import BitcoinTestFramework, SkipTest, ToHex, FromHex
from test_framework.util import (assert_equal, check_zmq_test_requirements,
                                 disconnect_nodes_bi, connect_nodes_bi, sync_blocks,
                                 zmq_port)
from test_framework.mininode import CTxIn, COutPoint


class ZMQRemovedFromMempool(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_nodes(self):

        # Check that bitcoin has been built with ZMQ enabled and we have python zmq package installed.
        check_zmq_test_requirements(self.options.configfile,
                                    SkipTest("bitcoind has not been built with zmq enabled."))
        # import zmq when we know we have the requirements for test with zmq.
        import zmq

        self.zmqContext = zmq.Context()
        self.zmqSubSocket = self.zmqContext.socket(zmq.SUB)
        self.zmqSubSocket.set(zmq.RCVTIMEO, 60000)
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"discardedfrommempool")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"removedfrommempoolblock")
        address = f"tcp://127.0.0.1:{zmq_port(0)}"
        self.zmqSubSocket.connect(address)
        self.extra_args = [["-zmqpubdiscardedfrommempool=%s" % address,
                            "-zmqpubremovedfrommempoolblock=%s" % address,
                            "-disablesafemode=1"],
                           ["-disablesafemode=1"]]
        self.add_nodes(self.num_nodes, self.extra_args)
        self.start_nodes()

    def run_test(self):
        try:
            self._zmq_test()
        finally:
            # Destroy the zmq context
            self.log.debug("Destroying zmq context")
            self.zmqContext.destroy(linger=None)

    def _zmq_test(self):
        block_hashes = self.nodes[0].generate(101)

        """Test case 1"""
        tx_hash1 = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1.0)
        tx_hash2 = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1.0)

        block_hash1 = self.nodes[0].generate(1)[0]
        # sync blocks so we are synchronized later in test
        sync_blocks(self.nodes)

        # receive notifications for txs to be included in block
        msg1 = self.zmqSubSocket.recv_multipart()
        assert_equal(msg1[0], b"removedfrommempoolblock")
        msg1_body = json.loads(msg1[1])
        assert_equal(msg1_body["reason"], "included-in-block")
        msg2 = self.zmqSubSocket.recv_multipart()
        assert_equal(msg2[0], b"removedfrommempoolblock")
        msg2_body = json.loads(msg2[1])
        assert_equal(msg2_body["reason"], "included-in-block")

        removed_tx = [msg1_body["txid"], msg2_body["txid"]]
        assert_equal(tx_hash1 in removed_tx and tx_hash2 in removed_tx, True)

        """Test case 2"""
        # bring txs back to mempool
        self.nodes[0].invalidateblock(block_hash1)
        # invalidate again so the coins that txs uses are immature
        self.nodes[0].invalidateblock(block_hashes[len(block_hashes) - 2])

        # receive notifications for txs about reorg mempool removal reason
        msg1 = self.zmqSubSocket.recv_multipart()
        assert_equal(msg1[0], b"removedfrommempoolblock")
        msg1_body = json.loads(msg1[1])
        assert_equal(msg1_body["reason"], "reorg")
        msg2 = self.zmqSubSocket.recv_multipart()
        assert_equal(msg2[0], b"removedfrommempoolblock")
        msg2_body = json.loads(msg2[1])
        assert_equal(msg2_body["reason"], "reorg")

        removed_tx = [msg1_body["txid"], msg2_body["txid"]]
        assert_equal(tx_hash1 in removed_tx and tx_hash2 in removed_tx, True)

        """Test case 3"""
        # bring both nodes on same height
        self.nodes[1].invalidateblock(block_hashes[len(block_hashes)-2])
        self.nodes[0].generate(4)
        sync_blocks(self.nodes)
        unspent = self.nodes[0].listunspent()[0]

        # create tx with spendable output for both nodes to use
        tx_spendable_output = CTransaction()
        tx_outs = [CTxOut(4500000000, CScript([OP_TRUE]))]
        tx_spendable_output.vout = tx_outs
        tx_spendable_output.vin = [CTxIn(COutPoint(int(unspent["txid"], 16), 0))]

        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx_spendable_output))['hex']
        self.nodes[0].sendrawtransaction(tx_hex, True)
        tx_spendable_output = FromHex(CTransaction(), tx_hex)
        tx_spendable_output.rehash()

        self.nodes[0].generate(1)
        # ignore included in block message
        _ = self.zmqSubSocket.recv_multipart()
        sync_blocks(self.nodes)

        # disconnect nodes and create transaction tx2 on node1 and mine a block
        # then create tx1 on node0 that use same output as tx2.
        disconnect_nodes_bi(self.nodes, 0, 1)

        tx2 = CTransaction()
        tx_outs = [CTxOut(4400000000, CScript([OP_TRUE]))]
        tx2.vout = tx_outs
        tx2.vin = [CTxIn(COutPoint(int(tx_spendable_output.hash, 16), 0))]

        tx_hex = self.nodes[1].signrawtransaction(ToHex(tx2))['hex']
        tx2_size = len(tx_hex)/2
        tx2 = FromHex(CTransaction(), tx_hex)
        tx2.rehash()
        self.nodes[1].sendrawtransaction(tx_hex, True)
        blockhash = self.nodes[1].generate(1)[0]

        tx1 = CTransaction()
        tx_outs = [CTxOut(4300000000, CScript([OP_TRUE]))]
        tx1.vout = tx_outs
        tx1.vin = [CTxIn(COutPoint(int(tx_spendable_output.hash, 16), 0))]

        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx1))['hex']
        tx1 = FromHex(CTransaction(), tx_hex)
        tx1.rehash()
        self.nodes[0].sendrawtransaction(tx_hex, True)

        # connect nodes again and sync blocks, we now expect to get conflict for tx1
        # because tx2 that uses same output as tx1 is already in block.
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes)

        msg = self.zmqSubSocket.recv_multipart()
        assert_equal(msg[0], b"discardedfrommempool")
        body = json.loads(msg[1])
        assert_equal(body["reason"], "collision-in-block-tx")
        assert_equal(body["txid"], tx1.hash)
        assert_equal(body["collidedWith"]["txid"], tx2.hash)
        assert_equal(body["collidedWith"]["size"], tx2_size)
        assert_equal(body["blockhash"], blockhash)

        """Test case 4"""
        # create tx with spendable output for both nodes to use
        unspent = self.nodes[0].listunspent()[0]
        tx_spendable_output = CTransaction()
        tx_outs = [CTxOut(4500000000, CScript([OP_TRUE]))]
        tx_spendable_output.vout = tx_outs
        tx_spendable_output.vin = [CTxIn(COutPoint(int(unspent["txid"], 16), 0))]

        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx_spendable_output))['hex']
        self.nodes[0].sendrawtransaction(tx_hex, True)
        tx_spendable_output = FromHex(CTransaction(), tx_hex)
        tx_spendable_output.rehash()

        self.nodes[0].generate(5)
        # ignore included in block message
        _ = self.zmqSubSocket.recv_multipart()
        sync_blocks(self.nodes)

        # disconnect nodes; mine few blocks on n1; create transaction tx2 on node1 and mine a block
        # then create tx1 on node0 that use same output as tx2.
        disconnect_nodes_bi(self.nodes, 0, 1)

        self.nodes[1].generate(5)
        tx2 = CTransaction()
        tx_outs = [CTxOut(4400000000, CScript([OP_TRUE]))]
        tx2.vout = tx_outs
        tx2.vin = [CTxIn(COutPoint(int(tx_spendable_output.hash, 16), 0))]

        tx_hex = self.nodes[1].signrawtransaction(ToHex(tx2))['hex']
        tx2_size = len(tx_hex)/2
        tx2 = FromHex(CTransaction(), tx_hex)
        tx2.rehash()
        self.nodes[1].sendrawtransaction(tx_hex, True)
        blockhash_tx2 = self.nodes[1].generate(1)[0]

        tx1 = CTransaction()
        tx_outs = [CTxOut(4300000000, CScript([OP_TRUE]))]
        tx1.vout = tx_outs
        tx1.vin = [CTxIn(COutPoint(int(tx_spendable_output.hash, 16), 0))]

        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx1))['hex']
        tx1 = FromHex(CTransaction(), tx_hex)
        tx1.rehash()
        self.nodes[0].sendrawtransaction(tx_hex, True)

        self.nodes[0].generate(1)
        # ignore included in block message
        _ = self.zmqSubSocket.recv_multipart()

        # connect nodes again to cause reorg to n1 chain, we now expect to
        # get conflict for tx1, because tx2 that uses same input as tx1 is already
        # in block on longer chain.
        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes)

        msg = self.zmqSubSocket.recv_multipart()
        assert_equal(msg[0], b"discardedfrommempool")
        body = json.loads(msg[1])
        assert_equal(body["reason"], "collision-in-block-tx")
        assert_equal(body["txid"], tx1.hash)
        assert_equal(body["collidedWith"]["txid"], tx2.hash)
        assert_equal(body["collidedWith"]["size"], tx2_size)
        assert_equal(body["blockhash"], blockhash_tx2)


if __name__ == '__main__':
    ZMQRemovedFromMempool().main()
