#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from time import sleep

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, OP_RETURN, OP_FALSE


#context manager extension for the NodeConn.
class SafeNodeConnection(NodeConn):

    def __init__(self, dstaddr='127.0.0.1', dstport=None, rpc=None, callback=None, net="regtest", services=NODE_NETWORK, send_version=True):
        assert rpc is not None, "Rpc must be specified"
        super(SafeNodeConnection, self).__init__(dstaddr= dstaddr,
                                                 dstport=(dstport if dstport is not None else p2p_port(0)),
                                                 rpc=rpc,
                                                 callback=(callback if callback is not None else NodeConnCB()),
                                                 net=net,
                                                 services=services,
                                                 send_version=send_version)

    @property
    def callback(self):
        return self.cb

    def __enter__(self):
        self.cb.add_connection(self)
        self.thread = NetworkThread()
        self.thread.start()
        self.cb.wait_for_verack()
        return self

    def __exit__(self, type, value, traceback):
        self.disconnect_node()
        self.thread.join()
        return False


# Test the functionality -datacarriersize works as expected. It should accept both OP_RETURN and OP_FALSE, OP_RETURN
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
        self.start_node(0, ['-datacarriersize=%d' % self.dataCarrierSize, '-acceptnonstdtxn=false'])

    def calc_overhead_bytes(self,script_op_codes):
        # Script consists of ((1 byte per op_code) + OP_PUSHDATA2(1 byte) + size (2 bytes) + data)
        return len(script_op_codes) + 3;

    def run_test_parametrized(self, script_op_codes):

        with SafeNodeConnection(rpc=self.nodes[0]) as connection:

            rejected_txs = []

            def on_reject(conn, msg):
                rejected_txs.append(msg)

            connection.callback.on_reject = on_reject

            # Create and send transaction with data carrier size equal to 500B.
            out_value = 10000
            ftx = CTransaction()

            script_ftx = CScript(script_op_codes + [b"a" * (self.dataCarrierSize - self.calc_overhead_bytes(script_op_codes))])
            ftx.vout.append(CTxOut(out_value, script_ftx))
            ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex']
            ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
            ftx = FromHex(CTransaction(), ftxHex)
            ftx.rehash()
            connection.send_message(msg_tx(ftx))

            # Create and send transaction with data carrier size equal to 501B.
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(ftx.sha256, 0), b''))
            script_tx = CScript(script_op_codes + [b"a" * (self.dataCarrierSize + 1 - self.calc_overhead_bytes(script_op_codes))])
            tx.vout.append(CTxOut(out_value -1000, script_tx))
            tx.rehash()
            connection.send_message(msg_tx(tx))

            # Wait for rejection.
            connection.callback.wait_for_reject()

            # Only second transaction should be rejected.
            assert_equal(len(rejected_txs), 1)
            assert_equal(rejected_txs[0].data, tx.sha256)
            assert_equal(rejected_txs[0].reason, b'scriptpubkey')


    def run_test(self):
        self.nodes[0].generate(101)
        self.run_test_parametrized([OP_RETURN])
        self.run_test_parametrized([OP_FALSE, OP_RETURN])

if __name__ == '__main__':
    DataCarrierSizeTest().main()

