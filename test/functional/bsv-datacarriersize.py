#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from time import sleep

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.mininode import *
from test_framework.script import CScript, OP_RETURN, OP_FALSE

# Test the functionality -datacarriersize works as expected. It should accept both OP_RETURN and OP_FALSE, OP_RETURN
# 1. Set -datacarriersize to 500B.
# 2. Send transaction with script that contains 500B. It should be accepted.
# 3. Send transaction with script that contains 501B. It should be rejected.

class DataCarrierSizeTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.num_peers = 1
        self.dataCarrierSize = 500;
        
    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)
        self.start_node(0)
        
    def calc_overhead_bytes(self,script_op_codes):
        # Script consists of ((1 byte per op_code) + OP_PUSHDATA2(1 byte) + size (2 bytes) + data)
        return len(script_op_codes) + 3;

    def run_test_parametrized(self, script_op_codes, description):

        # dataCarrierSize parameter is used for checking the size of the whole script (CScript).
        with self.run_node_with_connections(description, 0, ['-datacarriersize=%d' % self.dataCarrierSize, '-acceptnonstdtxn=false'], self.num_peers) as connections:

            connection = connections[0]
            rejected_txs = []

            def on_reject(conn, msg):
                rejected_txs.append(msg)

            connection.cb.on_reject = on_reject

            # Create and send transaction with data carrier size equal to 500B.
            out_value = 10000
            ftx = CTransaction()

            script_ftx = CScript(script_op_codes + [b"a" * (self.dataCarrierSize - self.calc_overhead_bytes(script_op_codes))])
            ftx.vout.append(CTxOut(out_value, script_ftx))
            ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex']
            ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
            ftx = FromHex(CTransaction(), ftxHex)
            ftx.rehash()
            connection.cb.send_message(msg_tx(ftx))

            # Create and send transaction with data carrier size equal to 501B.
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(ftx.sha256, 0), b''))
            script_tx = CScript(script_op_codes + [b"a" * (self.dataCarrierSize + 1 - self.calc_overhead_bytes(script_op_codes))])
            tx.vout.append(CTxOut(out_value -1000, script_tx))
            tx.rehash()
            connection.cb.send_message(msg_tx(tx))

            # Wait for rejection.
            connection.cb.wait_for_reject()

            # Only second transaction should be rejected.
            assert_equal(len(rejected_txs), 1)
            assert_equal(rejected_txs[0].data, tx.sha256)
            assert_equal(rejected_txs[0].reason, b'scriptpubkey')


    def run_test(self):
        self.nodes[0].generate(101)
        self.stop_node(0)

        self.run_test_parametrized([OP_RETURN], "script with OP_RETURN op code")
        self.run_test_parametrized([OP_FALSE, OP_RETURN], "script with  OP_FALSE, OP_RETURN op code")

if __name__ == '__main__':
    DataCarrierSizeTest().main()

