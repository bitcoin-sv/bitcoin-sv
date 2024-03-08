#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""Test the ZMQ API topics: hashblock2, hashtx2, rawblock2, rawtx2

Scenario:
1. Create nodes 0 and 1 (N0, N1). Only N0 has ZMQ notifications enabled for topics hashblock2, hashtx2, rawblock2, rawtx2.
2. Generate new block on N1. N0 should receive notifications for this block and its coinbase transaction.
3. Send transaction TX1 to N1. N0 should receive notification for this transaction.
4. Generate another block on N1 which consist of coinbase transaction and previously created transaction TX1.
   N0 should receive notification for only coinbase transaction and for new block. TX1 notification should not be duplicated.
5. Shut down N1.
6. Create 2 blocks on N0, each containing one coinbase and one normal transaction.
   Receive notifications for these transactions and blocks.
7. Shut down N0 and restart N1.
8. Create 3 blocks on N1, each containing one coinbase and one normal transaction.
9. Restart N1.
10. N0 should disconnect its 2 blocks and receive notifications for discarded transactions.
11. N0 should receive 18 notifications: 6 per each new block (2 notifications (hash+raw) for coinbase tx, sent tx and block)

"""
import os
import struct

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import (assert_equal,
                                 bytes_to_hex_str,
                                 hash256,
                                 connect_nodes_bi,
                                 check_zmq_test_requirements, zmq_port, wait_until)


class ZMQNewTopicsTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def setup_nodes(self):
        # Check that bitcoin has been built with ZMQ enabled and we have python zmq package installed.
        check_zmq_test_requirements(self.options.configfile,
                                    SkipTest("bitcoind has not been built with zmq enabled."))
        # import zmq when we know we have the requirements for test with zmq.
        import zmq

        self.zmqContext = zmq.Context()
        self.zmqSubSocket = self.zmqContext.socket(zmq.SUB)
        self.zmqSubSocket.set(zmq.RCVTIMEO, 60000)
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashblock2")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashtx2")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"rawblock2")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"rawtx2")
        address = f"tcp://127.0.0.1:{zmq_port(0)}"
        self.zmqSubSocket.connect(address)
        self.extra_args = [['-zmqpubhashblock2=%s' % address, '-zmqpubrawblock2=%s' % address,
                            '-zmqpubhashtx2=%s' % address, '-zmqpubrawtx2=%s' % address], []]
        self.add_nodes(self.num_nodes, self.extra_args)
        self.start_nodes()
        connect_nodes_bi(self.nodes, 0, 1)

    def run_test(self):
        try:
            self.test_activenotifications()
            self._zmq_test()
        finally:
            # Destroy the zmq context
            self.log.debug("Destroying zmq context")
            self.zmqContext.destroy(linger=None)

    def check_blocknotification(self, blockhash, seq):
        self.log.info("Wait for block")
        msg = self.zmqSubSocket.recv_multipart()
        topic = msg[0]
        assert_equal(topic, b"hashblock2")
        body = msg[1]
        msgSequence = struct.unpack('<I', msg[-1])[-1]
        assert_equal(msgSequence, seq)
        blkhash = bytes_to_hex_str(body)
        # blockhash from generate must be equal to the hash received over zmq
        assert_equal(blockhash, blkhash)

        msg = self.zmqSubSocket.recv_multipart()
        topic = msg[0]
        assert_equal(topic, b"rawblock2")
        body = msg[1]
        msgSequence = struct.unpack('<I', msg[-1])[-1]
        assert_equal(msgSequence, seq)

        # Check the hash of the rawblock's header matches generate
        assert_equal(blockhash, bytes_to_hex_str(hash256(body[:80])))

    def check_txnotification(self, seq):
        self.log.info("Wait for tx")
        msg = self.zmqSubSocket.recv_multipart()
        topic = msg[0]
        assert_equal(topic, b"hashtx2")
        txhash = msg[1]
        msgSequence = struct.unpack('<I', msg[-1])[-1]
        assert_equal(msgSequence, seq)

        msg = self.zmqSubSocket.recv_multipart()
        topic = msg[0]
        assert_equal(topic, b"rawtx2")
        body = msg[1]
        msgSequence = struct.unpack('<I', msg[-1])[-1]
        assert_equal(msgSequence, seq)

        # Check that the rawtx hashes to the hashtx
        assert_equal(hash256(body), txhash)
        return bytes_to_hex_str(txhash)

    def test_activenotifications(self):
        active_notifications = self.nodes[0].activezmqnotifications()
        address = f"tcp://127.0.0.1:{zmq_port(0)}"

        assert_equal({'notification': 'pubhashblock2',
                      'address': address} in active_notifications, True)
        assert_equal({'notification': 'pubhashtx2',
                      'address': address} in active_notifications, True)
        assert_equal({'notification': 'pubrawblock2',
                      'address': address} in active_notifications, True)
        assert_equal({'notification': 'pubrawtx2',
                      'address': address} in active_notifications, True)

    def restart_node(self, nodeNumber, connectingNodeNumber):
        import zmq
        address = f"tcp://127.0.0.1:{zmq_port(0)}"
        self.log.info("Restarting node%d and connecting zmq socket.", nodeNumber)
        self.start_node(nodeNumber, extra_args=self.extra_args[nodeNumber])

        self.zmqContext = zmq.Context()
        self.zmqSubSocket = self.zmqContext.socket(zmq.SUB)
        self.zmqSubSocket.set(zmq.RCVTIMEO, 6000)
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashblock2")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashtx2")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"rawblock2")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"rawtx2")
        self.zmqSubSocket.connect(address)

        connect_nodes_bi(self.nodes, nodeNumber, connectingNodeNumber)
        self.test_activenotifications()

    def _zmq_test(self):
        genhashes = self.nodes[1].generate(1)
        currentTxSeq = 0
        currentBlockSeq = 0

        self.log.info("Checking basic notifications for transaction and block.")
        self.check_txnotification(currentTxSeq)
        self.check_blocknotification(genhashes[0], currentBlockSeq)

        self.log.info("Checking notifications for peers transactions and duplicate notifications for block.")
        # Send a transaction TX1 to ourselves. Transaction should be submitted to hashtx2 and rawtx2 topics.
        txhash_sent = self.nodes[1].sendtoaddress(self.nodes[1].getnewaddress(), 1.0)

        currentTxSeq += 1
        txhash = self.check_txnotification(currentTxSeq)
        assert txhash_sent == txhash

        # Transaction TX1 is in this new block. Notification for TX1 should not be duplicated.
        blockhashes = self.nodes[1].generate(1)

        currentTxSeq += 1
        txhash = self.check_txnotification(currentTxSeq)
        # We only receive notification for coinbase tx
        assert txhash_sent != txhash

        currentBlockSeq += 1
        self.check_blocknotification(blockhashes[0], currentBlockSeq)
        self.log.info("Did not receive duplicate notification for already known transaction in new block.")

        ### Scenario 2: Reorg ###
        self.log.info("Checking reorg scenario.")
        self.stop_node(1)

        txhashes_sent0 = []
        blockhashes0 = []
        coinbasehashes0 = []

        # Add 2 blocks, each containing one coinbase and one normal transaction on node0 (node1 is shut down)
        for i in range(2):
            txhashes_sent0.append(self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1.0))

            currentTxSeq += 1
            txhash = self.check_txnotification(currentTxSeq)
            assert txhashes_sent0[-1] == txhash

            blockhashes0.append(self.nodes[0].generate(1)[0])

            currentTxSeq += 1
            coinbasehashes0.append(self.check_txnotification(currentTxSeq))
            assert txhashes_sent0[-1] != coinbasehashes0[i]

            currentBlockSeq += 1
            self.check_blocknotification(blockhashes0[i], currentBlockSeq)

        self.log.info("Shutting down node0 and restarting node1.")
        self.stop_node(0)
        self.zmqSubSocket.close()

        self.start_node(1)

        txhashes_sent1 = []
        blockhashes1 = []
        coinbasehashes1 = []

        # Add 3 blocks, each containing one coinbase and one normal transaction on node1 (node0 is shut down)
        numberOfBlocks = 3
        for i in range(numberOfBlocks):
            txhashes_sent1.append(self.nodes[1].sendtoaddress(self.nodes[1].getnewaddress(), 1.0))
            blockhashes1.append(self.nodes[1].generate(1)[0])
            coinbasehashes1.append(self.nodes[1].getblock(self.nodes[1].getbestblockhash())['tx'][0])

        self.restart_node(nodeNumber=0, connectingNodeNumber=1)
        blockhashes_notified = set()
        txhashes_notified = set()

        self.log.info("Checking that all three blocks from the longer chain are published on re-org.")
        # Checking that all three blocks from the longer chain are published along with all the transactions
        # 2 notifications (hash+raw) for coinbase tx, sent tx and block -- 6 per added block +
        # 2 notifications (hash+raw) per tx from the disconnected block
        for i in range(numberOfBlocks * 6 + 2*len(txhashes_sent0)):
            msg = self.zmqSubSocket.recv_multipart()
            topic = msg[0]
            body = msg[1]

            if topic == b"hashblock2":
                blockhashes_notified.add(bytes_to_hex_str(body))
            if topic == b"hashtx2":
                txhashes_notified.add(bytes_to_hex_str(body))

        assert blockhashes_notified.difference(blockhashes1) == set()
        self.log.info("All blocks from the new longest chain were published.")
        assert txhashes_notified.difference(set(txhashes_sent1).union(set(coinbasehashes1)).union(txhashes_sent0)) == set()
        self.log.info("All transactions from the new longest chain were published.")
        assert set(txhashes_sent0).issubset(txhashes_notified)
        self.log.info("All transactions from the disconnected blocks were published when added to mempool.")
        assert txhashes_sent0 == self.nodes[0].getrawmempool()
        self.log.info("Transactions from disconnected blocks are back in mempool.")

        self.log.info("Generate a new block that will contain transactions from disconnected blocks.")
        blockhash = self.nodes[0].generate(1)[0]
        txes_added_again = set()
        for i in range(4):
            msg = self.zmqSubSocket.recv_multipart()
            topic = msg[0]
            body = msg[1]
            if topic == b"hashtx2":
                # only notify for coinbase tx
                assert bytes_to_hex_str(body) not in txhashes_sent0
            if topic == b"hashblock2":
                assert bytes_to_hex_str(body) == blockhash
        assert len(self.nodes[0].getrawmempool()) == 0
        self.log.info("Transactions from disconnected blocks were added to a new block - not notified again.")


if __name__ == '__main__':
    ZMQNewTopicsTest().main()
