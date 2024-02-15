#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check that SCRIPT_VERIFY_SIGPUSHONLY become mandatory after genesis activation.
Before genesis, blocks with transactions that contain op codes in unlock scripts are accepted.
After genesis, they are rejected.

In this test (opposed to bsv-genesis-pushonly-transactions.py), blocks are sent as whole via P2P protocol.
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_TRUE, OP_ADD
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal, hashToHex
from test_framework.comptool import TestManager, TestInstance, RejectResult


class BSVGenesisActivation(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 104
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

        test, out, _ = prepare_init_chain(self.chain, 101, 100)

        yield test

        assert_equal(node.getblock(node.getbestblockhash())['height'], 102)

        # create block with height 103, where sig_pushonly is not yet mandatory
        block(1, spend=out[0])
        transaction_op_add_accepted = create_transaction(out[1].tx, out[1].n, CScript([1, 1, OP_ADD]), 100000, CScript([OP_TRUE]))
        blk_accepted = self.chain.update_block(1, [transaction_op_add_accepted])
        yield self.accepted()
        assert_equal(node.getblock(node.getbestblockhash())['height'], 103)

        # create block with height 104, where sig_pushonly is mandatory
        block(2, spend=out[2])
        transaction_op_add_rejected = create_transaction(out[3].tx, out[3].n, CScript([1, 1, OP_ADD]), 100000, CScript([OP_TRUE]))
        self.chain.update_block(2, [transaction_op_add_rejected])
        yield self.rejected(RejectResult(16, b'blk-bad-inputs'))
        assert_equal(node.getblock(node.getbestblockhash())['height'], 103)

        assert_equal(node.getbestblockhash(), blk_accepted.hash)

        # invalidate block with height 103
        node.invalidateblock(hashToHex(blk_accepted.sha256))

        # tip is now on height 102
        assert_equal(node.getblock(node.getbestblockhash())['height'], 102)

        # transaction_op_add_accepted should not be in mempool (individual transactions are always checked against pushonly)
        assert(transaction_op_add_accepted.hash not in set(node.getrawmempool()))


if __name__ == '__main__':
    BSVGenesisActivation().main()
