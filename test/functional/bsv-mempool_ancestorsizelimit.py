#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, OP_TRUE, OP_RETURN

# Test if -limitancestorsize and -limitdescendantsize settings work as expected.
# Ancestors:
# Sum of transactions's ancestors' sizes in a mempool should not exceed limitancestorsize.
# A chain of transactions is sent to bitcoind in that order: fundingTransaction -> tx1 -> tx2 -> tx3 ...
# Descendants:
# Sum of transactions's descendants' sizes in a mempool should not exceed limitdescendantsize.
# A funding transaction with 10 outputs is sent to bitcoind, after that children with the same parent are sent.

class MempoolSizeLimitTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.limitancestorsize = 1000
        self.limitdescendantsize = 2000
        # limitancestorsize and limitdescendantsize are passed in kilobytes
        self.extra_args = [["-limitancestorsize=%d" % (self.limitancestorsize / 1000), "-limitdescendantsize=%d" % (self.limitdescendantsize / 1000)]]

    # Build and submit a transaction that spends parent_txid:vout
    # Return amount sent
    def chain_transaction(self, node, parent_txid, vout, value, num_outputs, connection):
        fee = 300
        send_value = (value - fee) / num_outputs

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(parent_txid, vout), b''))
        for i in range(num_outputs):
            tx.vout.append(CTxOut(int(send_value), CScript([OP_TRUE])))
        # Append some data, so that transactions are larger
        tx.vout.append(CTxOut(int(0), CScript([OP_RETURN,  b"a" * 200])))
        tx.rehash()
        connection.send_message(msg_tx(tx))
   
        txSize = len(tx.serialize())
        return (tx, send_value, txSize)

    def run_test(self):
        # 0. Prepare initial blocks.
        self.nodes[0].generate(101)

        rejected_txs = []
        def on_reject(conn, msg):
            rejected_txs.append(msg)

        connection = NodeConnCB()
        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], connection))
        connection.add_connection(connections[0])
        connection.on_reject = on_reject
        NetworkThread().start()
        connection.wait_for_verack()

        ######################################
        # 1. Test ancestor chain size limit.
        # First create funding transaction that pays to output that does not require signatures.
        out_value = 10000
        ftx = CTransaction()
        ftx.vout.append(CTxOut(out_value, CScript([OP_TRUE])))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex'] 
        ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()
        connection.send_message(msg_tx(ftx))
        ancestorsSize = len(ftxHex)/2
        txid = ftx.sha256
        value = out_value

        # For loop will take less than MAX_ANCESOTS iterations.
        # That way we make sure that exception is not caused by ancestor limit count.
        MAX_ANCESTORS = 25
        for i in range (0, MAX_ANCESTORS):
            (tx, sent_value, txSize) = self.chain_transaction(self.nodes[0], txid, 0, value, 1, connection)
            txid = tx.sha256
            value = sent_value
            ancestorsSize = ancestorsSize + txSize

            # Check if next sent transaction will cause too many ancestors in a mempool.
            # The same exception is thrown either if the count of ancestors is too large or the size of all ancestors is too large.
            # Here, the size is the problem (count is less then limitancestorcount, which is 25 by default).
            if (ancestorsSize + txSize > self.limitancestorsize):
                (tx, sent_value, txSize) = self.chain_transaction(self.nodes[0], txid, 0, value, 1, connection)
                connection.wait_for_reject()
                break

        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].data, tx.sha256)
        assert_equal(rejected_txs[0].reason, b'too-long-mempool-chain')

        ######################################
        # 2. Test descendant chain size limit.
        send_value = 10000
   
        ftx = CTransaction()
        for i in range(10):
            ftx.vout.append(CTxOut(send_value, CScript([OP_TRUE])))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex'] 
        ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()
        descendantsSize = len(ftxHex)/2
        connection.send_message(msg_tx(ftx))

        utxos = []
        for i in range(10):
            utxos.append({'txid': ftx.sha256, 'vout': i, 'amount': send_value})

        # For loop will take less than MAX_DESCENDANTS iterations.
        # That way we make sure that exception is not caused by descendant limit count.
        MAX_DESCENDANTS = 25
        # Keep txids from the produced chain.
        chain_of_descendant_txns = []
        for i in range(MAX_DESCENDANTS):
            utxo = utxos.pop(0)
            (tx, sent_value, txSize) = self.chain_transaction(self.nodes[0], utxo['txid'], utxo['vout'], utxo['amount'], 1, connection)
            descendantsSize = descendantsSize + txSize
            chain_of_descendant_txns.append(tx.sha256)

            # Check if next sent transaction will cause too many descendants in a mempool.
            # The same exception is thrown either if the count of descendants is too large or the size of all descendants is too large.
            # Here, the size is the problem (count is less then limitdescendantscount, which is 25 by default).
            if (descendantsSize + txSize > self.limitdescendantsize):
                utxo = utxos.pop(0)
                connection.message_count.clear()
                (tx, sent_value, txSize) = self.chain_transaction(self.nodes[0], utxo['txid'], utxo['vout'], utxo['amount'], 1, connection)
                chain_of_descendant_txns.append(tx.sha256)
                connection.wait_for_reject()
                break

        assert_equal(len(rejected_txs), 2)
        # Evaluation order of siblings is random
        assert(rejected_txs[1].data in chain_of_descendant_txns)
        assert_equal(rejected_txs[1].reason, b'too-long-mempool-chain')

if __name__ == '__main__':
    MempoolSizeLimitTest().main()
