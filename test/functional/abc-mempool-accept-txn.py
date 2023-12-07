#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
This test checks acceptance of transactions by the mempool
It is derived from the much more complex p2p-fullblocktest.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import (assert_raises_rpc_error, assert_equal)
from test_framework.comptool import TestManager, TestInstance
from test_framework.blocktools import *
import time
from test_framework.key import CECKey
from test_framework.script import *
import struct
from test_framework.cdefs import MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS

# Error for too many sigops in one TX
TXNS_TOO_MANY_SIGOPS_ERROR = b'bad-txns-too-many-sigops'
RPC_TXNS_TOO_MANY_SIGOPS_ERROR = "64: " + \
    TXNS_TOO_MANY_SIGOPS_ERROR.decode("utf-8")


class FullBlockTest(ComparisonTestFramework):

    # Can either run this test as 1 node with expected answers, or two and compare them.
    # Change the "outcome" variable from each TestInstance object to only do
    # the comparison.

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.block_heights = {}
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.tip = None
        self.blocks = {}
        self.extra_args = [['-minrelaytxfee=0']]

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option(
            "--runbarelyexpensive", dest="runbarelyexpensive", default=True)

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block
        update_block = self.chain.update_block
        save_spendable_output = self.chain.save_spendable_output
        get_spendable_output = self.chain.get_spendable_output
        accepted = self.accepted

        # shorthand for variables
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))
        # Create a new block
        block(0)
        yield accepted()

        test, out, _ = prepare_init_chain(self.chain, 99, 33)

        yield test

        # P2SH
        # Build the redeem script, hash it, use hash to create the p2sh script
        redeem_script = CScript([self.coinbase_pubkey] + [
                                OP_2DUP, OP_CHECKSIGVERIFY] * 5 + [OP_CHECKSIG])
        redeem_script_hash = hash160(redeem_script)
        p2sh_script = CScript([OP_HASH160, redeem_script_hash, OP_EQUAL])

        # Creates a new transaction using a p2sh transaction as input
        def spend_p2sh_tx(p2sh_tx_to_spend, output_script=CScript([OP_TRUE])):
            # Create the transaction
            spent_p2sh_tx = CTransaction()
            spent_p2sh_tx.vin.append(
                CTxIn(COutPoint(p2sh_tx_to_spend.sha256, 0), b''))
            spent_p2sh_tx.vout.append(CTxOut(1, output_script))
            # Sign the transaction using the redeem script
            sighash = SignatureHashForkId(
                redeem_script, spent_p2sh_tx, 0, SIGHASH_ALL | SIGHASH_FORKID, p2sh_tx_to_spend.vout[0].nValue)
            sig = self.coinbase_key.sign(
                sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
            spent_p2sh_tx.vin[0].scriptSig = CScript([sig, redeem_script])
            spent_p2sh_tx.rehash()
            return spent_p2sh_tx

        # P2SH tests
        # Create a p2sh transaction
        p2sh_tx = create_and_sign_transaction(
            out[0].tx, out[0].n, 1, p2sh_script, self.coinbase_key)

        # Add the transaction to the block
        block(1)
        update_block(1, [p2sh_tx])
        yield accepted()

        # Sigops p2sh limit for the mempool test
        p2sh_sigops_limit_mempool = MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS - \
            redeem_script.GetSigOpCount(True)
        # Too many sigops in one p2sh script
        too_many_p2sh_sigops_mempool = CScript(
            [OP_CHECKSIG] * (p2sh_sigops_limit_mempool + 1))

        # A transaction with this output script can't get into the mempool
        assert_raises_rpc_error(-26, RPC_TXNS_TOO_MANY_SIGOPS_ERROR, node.sendrawtransaction,
                                ToHex(spend_p2sh_tx(p2sh_tx, too_many_p2sh_sigops_mempool)))

        # The transaction is rejected, so the mempool should still be empty
        assert_equal(set(node.getrawmempool()), set())

        # Max sigops in one p2sh txn
        max_p2sh_sigops_mempool = CScript(
            [OP_CHECKSIG] * (p2sh_sigops_limit_mempool))

        # A transaction with this output script can get into the mempool
        max_p2sh_sigops_txn = spend_p2sh_tx(p2sh_tx, max_p2sh_sigops_mempool)
        max_p2sh_sigops_txn_id = node.sendrawtransaction(
            ToHex(max_p2sh_sigops_txn))
        assert_equal(set(node.getrawmempool()), {max_p2sh_sigops_txn_id})

        # Mine the transaction
        block(2, spend=out[1])
        update_block(2, [max_p2sh_sigops_txn])
        yield accepted()

        # The transaction has been mined, it's not in the mempool anymore
        assert_equal(set(node.getrawmempool()), set())


if __name__ == '__main__':
    FullBlockTest().main()
