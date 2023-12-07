#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Test makes sure that -maxcoinsviewcachesize prevents transactions that spend
# inputs with too large input transactions size from being accepted.

# maxcoinsviewcachesize is set to 6000
# transaction spends two transactions of ~5000 size so that is greater than
# 6000 and thus the transaction is not accepted to mempool

from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_TRUE, OP_RETURN, CTransaction, CTxOut
from test_framework.blocktools import assert_equal, CTxIn, COutPoint, prepare_init_chain
from test_framework.comptool import TestInstance, TestNode, RejectResult


class BSVTxMaxCoinsCacheSizePolicyLimit(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 105
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight,
                            '-maxstdtxvalidationduration=100000', '-maxtxnvalidatorasynctasksrunduration=111000',
                            '-maxnonstdtxvalidationduration=110000', '-maxcoinsviewcachesize=6000'
                            ]] * self.num_nodes

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block

        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        test, out, _ = prepare_init_chain(self.chain, 105, 105, block_0=False)

        yield test

        assert_equal(node.getblock(node.getbestblockhash())['height'], 105)

        block(1)

        redeem_script = CScript([OP_TRUE, OP_RETURN, b"a" * 5000])

        spend_tx1 = CTransaction()
        spend_tx1.vin.append(CTxIn(COutPoint(out[2].tx.sha256, out[2].n), CScript(), 0xffffffff))
        spend_tx1.vout.append(CTxOut(500, redeem_script))
        spend_tx1.vout.append(CTxOut(500, redeem_script))
        spend_tx1.calc_sha256()
        self.log.info(spend_tx1.hash)

        self.chain.update_block(1, [spend_tx1])
        yield self.accepted()

        tx1 = CTransaction()
        tx1.vout = [CTxOut(499, CScript([OP_TRUE]))]
        tx1.vin.append(CTxIn(COutPoint(spend_tx1.sha256, 0), CScript(), 0xfffffff))
        tx1.vin.append(CTxIn(COutPoint(spend_tx1.sha256, 1), CScript(), 0xfffffff))
        tx1.calc_sha256()
        self.log.info(tx1.hash)
        yield TestInstance([[tx1, RejectResult(16, b'bad-txns-inputs-too-large')]])


if __name__ == '__main__':
    BSVTxMaxCoinsCacheSizePolicyLimit().main()
