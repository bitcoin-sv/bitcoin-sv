#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, OP_RETURN

# Test the functionality -datacarriersize works as expected.
# 1. Set -datacarriersize to 500B.
# 2. Send transaction with script that contains 500B. It should be accepted.
# 3. Send transaction with script that contains 501B. It should be rejected.

class DataCarrierSizeTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        
    def setup_network(self):
        self.add_nodes(self.num_nodes)
        self.dataCarrierSize = 500;
        # dataCarrierSize is used for checking the size of the whole script (CScript).
        # Script consists of (OP_RETURN (1 byte) + OP_PUSHDATA2(1 byte) + size (2 bytes) + data)
        self.overheadBytes = 4
        self.start_node(0, ['-datacarriersize=%d' % self.dataCarrierSize, '-acceptnonstdtxn=false'])

    def run_test(self):

        self.nodes[0].generate(101)

        connection1 = NodeConnCB()
        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], connection1))
        connection1.add_connection(connections[0])

        rejected_txs = []
        def on_reject(conn, msg):
            rejected_txs.append(msg)
        connection1.on_reject = on_reject
        NetworkThread().start()
        connection1.wait_for_verack()

        # Create and send transaction with data carrier size equal to 500B.
        out_value = 10000
        ftx = CTransaction()

        ftx.vout.append(CTxOut(out_value, CScript([OP_RETURN,  b"a" * (self.dataCarrierSize - self.overheadBytes)])))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex'] 
        ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']         
        ftx = FromHex(CTransaction(), ftxHex)        
        ftx.rehash()        
        connection1.send_message(msg_tx(ftx))

        # Create and send transaction with data carrier size equal to 501B.
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(ftx.sha256, 0), b''))
        tx.vout.append(CTxOut(out_value -1000, CScript([OP_RETURN,   b"a" * (self.dataCarrierSize + 1 - self.overheadBytes)])))
        tx.rehash()
        connection1.send_message(msg_tx(tx))

        # Wait for rejection. 
        connection1.wait_for_reject()

        # Only second transaction should be rejected.
        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].data, tx.sha256)
        assert_equal(rejected_txs[0].reason, b'scriptpubkey')

if __name__ == '__main__':
    DataCarrierSizeTest().main()

