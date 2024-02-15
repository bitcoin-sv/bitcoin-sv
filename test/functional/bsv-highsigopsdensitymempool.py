#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import assert_equal
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import create_transaction, CScript, msg_tx, prepare_init_chain
from test_framework.script import OP_CHECKMULTISIG, OP_TRUE


# We create 100 high and 10 low sigops density transactions and make sure that low density transactions are mined too.
class MempoolHighSigopsDensity(ComparisonTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 50
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight]]

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block

        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 300, 300)

        yield test

        # send 100 transactions with high sigops density
        txsMultisigs = []
        twoGB = 2147483647
        for i in range(100):
            txMultisig = create_transaction(out[i].tx, out[i].n, b'', 100000, CScript([twoGB, OP_CHECKMULTISIG]))
            self.test.connections[0].send_message(msg_tx(txMultisig))
            txsMultisigs.append(txMultisig)
        # check that transactions are in mempool
        self.check_mempool(self.test.connections[0].rpc, txsMultisigs)

        # send 10 transactions with normal sigops density
        txsBasics = []
        for j in range(10):
            txBasic = create_transaction(out[i+j+1].tx, out[i+j+1].n, b'', 100000, CScript([2, OP_CHECKMULTISIG]))
            self.test.connections[0].send_message(msg_tx(txBasic))
            txsBasics.append(txBasic)
        # check that transactions are in mempool
        self.check_mempool(self.test.connections[0].rpc, txsBasics)

        mempool = node.getrawmempool()
        for tx in txsMultisigs:
            assert_equal(True, tx.hash in mempool)
        for tx in txsBasics:
            assert_equal(True, tx.hash in mempool)

        node.generate(1)
        blockTxs = node.getblock(node.getbestblockhash())['tx']
        for tx in txsBasics:
            assert_equal(True, tx.hash in blockTxs)


if __name__ == '__main__':
    MempoolHighSigopsDensity().main()
