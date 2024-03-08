#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check that transactions that contain op codes in unlock scripts are accepted
before AND rejected after genesis activation with acceptnonstdtxn=1 parameter
set - test net only.

In this test (opposed to bsv-genesis-pushonly.py), transactions are sent individually via P2P protocol.
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_TRUE, OP_ADD, OP_DROP
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal
from test_framework.comptool import TestManager, TestInstance
from test_framework.mininode import msg_tx


class BSVGenesisActivationTransactions(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 103
        self.extra_args = [['-whitelist=127.0.0.1', '-acceptnonstdtxn=1', '-genesisactivationheight=%d' % self.genesisactivationheight]]

    def run_test(self):
        self.test.run()

    def assert_accepted_transaction(self, out):
        transaction_op_add = create_transaction(out.tx, out.n, CScript([1, 1, OP_ADD, OP_DROP]), 100000, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(transaction_op_add))
        self.check_mempool(self.test.connections[0].rpc, [transaction_op_add])

    def assert_rejected_transaction(self, out):
        def on_reject(conn, msg):
            assert_equal(msg.reason, b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

        transaction_op_add = create_transaction(out.tx, out.n, CScript([1, 1, OP_ADD, OP_DROP]), 100000, CScript([OP_TRUE]))
        self.test.connections[0].cb.on_reject = on_reject
        self.test.connections[0].send_message(msg_tx(transaction_op_add))
        self.test.connections[0].cb.wait_for_reject()

    def get_tests(self):

        # shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 100, 100)

        yield test

        # tip is on height 101
        assert_equal(node.getblock(node.getbestblockhash())['height'], 101)
        self.assert_accepted_transaction(out[0])

        self.nodes[0].generate(1)

        # tip is on height 102
        assert_equal(node.getblock(node.getbestblockhash())['height'], 102)
        self.assert_rejected_transaction(out[1])

        self.nodes[0].generate(1)

        # tip is on height 103
        assert_equal(node.getblock(node.getbestblockhash())['height'], 103)

        self.assert_rejected_transaction(out[2])


if __name__ == '__main__':
    BSVGenesisActivationTransactions().main()
