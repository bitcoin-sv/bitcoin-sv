#!/usr/bin/env python3
# Copyright (C) 2018-2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.util import *
from test_framework.key import CECKey
from test_framework.script import *
import time

'''
Test the behaviour of the txn propagation after a new block.
'''


class TxnPropagationAfterBlock(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.tip = None
        self.block_time = None
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.extra_args = [['-broadcastdelay=50000', '-txnpropagationfreq=50000']] * self.num_nodes

    def run_test(self):
        self.test.run()

    def get_tests(self):
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))
        block = self.chain.next_block
        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 140, 100)

        yield test

        # Create blocks with multiple txns in
        block1 = self.chain.next_block(1, spend=out[0:20])
        block2 = self.chain.next_block(2, spend=out[20:40])

        # Check frequency propagator runs has been correctly set to very slow (we will poke as required)
        assert_equal(self.nodes[0].getnetworkinfo()['txnpropagationfreq'], 50000)

        # Get half of the txns from each block into the peers inventory queue
        for t in range(1, 11):
            self.nodes[0].sendrawtransaction(bytes_to_hex_str(block1.vtx[t].serialize()), True)
            self.nodes[0].sendrawtransaction(bytes_to_hex_str(block2.vtx[t].serialize()), True)
        self.nodes[0].settxnpropagationfreq(50000)
        wait_until(lambda: self.nodes[0].getpeerinfo()[0]['txninvsize'] == 20)

        # Feed in the other half of txns to just the txn propagator queue
        for t in range(11, 21):
            self.nodes[0].sendrawtransaction(bytes_to_hex_str(block1.vtx[t].serialize()), True)
            self.nodes[0].sendrawtransaction(bytes_to_hex_str(block2.vtx[t].serialize()), True)
        assert_equal(self.nodes[0].getnetworkinfo()['txnpropagationqlen'], 20)
        assert_equal(self.nodes[0].getmempoolinfo()['size'], 40)

        # Mine the first new block
        yield TestInstance([[block1, True]])

        # Check the txns from the mined block have gone from the propagator queue and the nodes queue
        assert_equal(self.nodes[0].getnetworkinfo()['txnpropagationqlen'], 10)
        assert_equal(self.nodes[0].getpeerinfo()[0]['txninvsize'], 10)
        assert_equal(self.nodes[0].getmempoolinfo()['size'], 20)


if __name__ == '__main__':
    TxnPropagationAfterBlock().main()
