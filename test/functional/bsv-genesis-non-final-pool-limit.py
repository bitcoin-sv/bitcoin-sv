#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test applying limit to the non-final pool size.
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_TRUE
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal, wait_until
from test_framework.comptool import TestInstance, RejectResult, DiscardResult
import time


class BSVGenesis_NonFinalPoolLimit(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 600
        self.extra_args = [['-debug', '-peerbloomfilters=1', '-genesisactivationheight=%d' % self.genesisactivationheight,
                            '-maxmempoolnonfinal=0', '-rejectmempoolrequest=0']] * self.num_nodes

    def run_test(self):
        self.test.run()

    def create_transaction(self, prevtx, n, sig, value, scriptPubKey):
        tx = create_transaction(prevtx, n, sig, value, scriptPubKey)
        tx.nVersion = 2
        tx.rehash()
        return tx

    def create_locked_transaction(self, prevtx, n, sig, value, scriptPubKey, lockTime, sequence):
        tx = self.create_transaction(prevtx, n, sig, value, scriptPubKey)
        tx.nLockTime = lockTime
        tx.vin[0].nSequence = sequence
        tx.rehash()
        return tx

    def make_sequence(self, n):
        # Bits 31 and 22 must be cleared to enable nSequence relative lock time meaning pre-genesis
        n = n - self.nodes[0].getblockcount()
        sequence = n & 0x0000FFFF
        return sequence

    def get_tests(self):
        # Shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 600, 200)

        yield test

        # Create block with some transactions for us to spend
        block(1, spend=out[0])
        spend_tx1 = self.create_transaction(out[1].tx, out[1].n, CScript(), 100000, CScript([OP_TRUE]))
        self.chain.update_block(1, [spend_tx1])
        yield self.accepted()

        # Create non-final txn
        nLockTime = int(time.time()) + 1000
        tx1 = self.create_locked_transaction(spend_tx1, 0, CScript(), 1000, CScript([OP_TRUE]), nLockTime, 0x00000003)
        # The transactions should be (silently) rejected because the pool is full.
        yield TestInstance([[tx1, DiscardResult()]])
        assert(tx1.hash not in self.nodes[0].getrawnonfinalmempool())


if __name__ == '__main__':
    BSVGenesis_NonFinalPoolLimit().main()
