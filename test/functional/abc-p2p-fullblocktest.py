#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
This test checks simple acceptance of bigger blocks via p2p.
It is derived from the much more complex p2p-fullblocktest.
The intention is that small tests can be derived from this one, or
this one can be extended, to cover the checks done for bigger blocks
(e.g. sigops limits).
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
import time
from test_framework.key import CECKey
from test_framework.script import *
from test_framework.cdefs import (ONE_MEGABYTE)

class FullBlockTest(ComparisonTestFramework):

    # Can either run this test as 1 node with expected answers, or two and compare them.
    # Change the "outcome" variable from each TestInstance object to only do
    # the comparison.

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.excessive_block_size = 100 * ONE_MEGABYTE
        self.extra_args = [['-whitelist=127.0.0.1',
                            "-excessiveblocksize=%d"
                            % self.excessive_block_size]]

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option(
            "--runbarelyexpensive", dest="runbarelyexpensive", default=True)

    def run_test(self):
        self.nodes[0].setexcessiveblock(self.excessive_block_size)
        self.test.run()

    def get_tests(self):
        node = self.nodes[0]
        self.chain.set_genesis_hash( int(node.getbestblockhash(), 16) )

        # shorthand for functions
        block = self.chain.next_block

        # Create a new block
        block(0)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(99):
            block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(100):
            out.append(self.chain.get_spendable_output())

        # Let's build some blocks and test them.
        for i in range(16):
            n = i + 1
            block(n, spend=out[i], block_size=n * ONE_MEGABYTE // 2)
            yield self.accepted()

        # block of maximal size
        block(17, spend=out[16], block_size=self.excessive_block_size)
        yield self.accepted()

        # Oversized blocks will cause us to be disconnected
        assert(not self.test.test_nodes[0].closed)
        block(18, spend=out[17], block_size=self.excessive_block_size + 1)
        self.test.connections[0].send_message(msg_block((self.chain.tip)))
        self.test.wait_for_disconnections()
        assert(self.test.test_nodes[0].closed)

        # Rewind bad block and remake connection to node
        self.chain.set_tip(17)
        self.restart_network()
        self.test.wait_for_verack()

        # Accept many sigops
        lots_of_checksigs = CScript(
            [OP_CHECKSIG] * MAX_BLOCK_SIGOPS_PER_MB)
        block(19, spend=out[17], script=lots_of_checksigs,
              block_size=ONE_MEGABYTE)
        yield self.accepted()

        block(20, spend=out[18], script=lots_of_checksigs,
              block_size=ONE_MEGABYTE, extra_sigops=1)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))

        # Rewind bad block
        self.chain.set_tip(19)

        # Accept 40k sigops per block > 1MB and <= 2MB
        block(21, spend=out[18], script=lots_of_checksigs,
              extra_sigops=MAX_BLOCK_SIGOPS_PER_MB, block_size=ONE_MEGABYTE + 1)
        yield self.accepted()

        # Accept 40k sigops per block > 1MB and <= 2MB
        block(22, spend=out[19], script=lots_of_checksigs,
              extra_sigops=MAX_BLOCK_SIGOPS_PER_MB, block_size=2 * ONE_MEGABYTE)
        yield self.accepted()

        # Reject more than 40k sigops per block > 1MB and <= 2MB.
        block(23, spend=out[20], script=lots_of_checksigs,
              extra_sigops=MAX_BLOCK_SIGOPS_PER_MB + 1, block_size=ONE_MEGABYTE + 1)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))

        # Rewind bad block
        self.chain.set_tip(22)

        # Reject more than 40k sigops per block > 1MB and <= 2MB.
        block(24, spend=out[20], script=lots_of_checksigs,
              extra_sigops=MAX_BLOCK_SIGOPS_PER_MB + 1, block_size=2 * ONE_MEGABYTE)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))

        # Rewind bad block
        self.chain.set_tip(22)

        # Accept 60k sigops per block > 2MB and <= 3MB
        block(25, spend=out[20], script=lots_of_checksigs, extra_sigops=2 *
              MAX_BLOCK_SIGOPS_PER_MB, block_size=2 * ONE_MEGABYTE + 1)
        yield self.accepted()

        # Accept 60k sigops per block > 2MB and <= 3MB
        block(26, spend=out[21], script=lots_of_checksigs,
              extra_sigops=2 * MAX_BLOCK_SIGOPS_PER_MB, block_size=3 * ONE_MEGABYTE)
        yield self.accepted()

        # Reject more than 40k sigops per block > 1MB and <= 2MB.
        block(27, spend=out[22], script=lots_of_checksigs, extra_sigops=2 *
              MAX_BLOCK_SIGOPS_PER_MB + 1, block_size=2 * ONE_MEGABYTE + 1)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))

        # Rewind bad block
        self.chain.set_tip(26)

        # Reject more than 40k sigops per block > 1MB and <= 2MB.
        block(28, spend=out[22], script=lots_of_checksigs, extra_sigops=2 *
              MAX_BLOCK_SIGOPS_PER_MB + 1, block_size=3 * ONE_MEGABYTE)
        yield self.rejected(RejectResult(16, b'bad-blk-sigops'))

        # Rewind bad block
        self.chain.set_tip(26)

        # Too many sigops in one txn
        too_many_tx_checksigs = CScript(
            [OP_CHECKSIG] * (MAX_BLOCK_SIGOPS_PER_MB + 1))
        block(
            29, spend=out[22], script=too_many_tx_checksigs, block_size=ONE_MEGABYTE + 1)
        yield self.rejected(RejectResult(16, b'bad-txn-sigops'))

        # Rewind bad block
        self.chain.set_tip(26)

        # Generate a key pair to test P2SH sigops count
        private_key = CECKey()
        private_key.set_secretbytes(b"fatstacks")
        public_key = private_key.get_pubkey()

        # P2SH
        # Build the redeem script, hash it, use hash to create the p2sh script
        redeem_script = CScript(
            [public_key] + [OP_2DUP, OP_CHECKSIGVERIFY] * 5 + [OP_CHECKSIG])
        redeem_script_hash = hash160(redeem_script)
        p2sh_script = CScript([OP_HASH160, redeem_script_hash, OP_EQUAL])

        # Create a p2sh transaction
        p2sh_tx = self.chain.create_tx_with_script(out[22], 1, p2sh_script)

        # Add the transaction to the block
        block(30)
        self.chain.update_block(30, [p2sh_tx])
        yield self.accepted()

        # Creates a new transaction using the p2sh transaction included in the
        # last block
        def spend_p2sh_tx(output_script=CScript([OP_TRUE])):
            # Create the transaction
            spent_p2sh_tx = CTransaction()
            spent_p2sh_tx.vin.append(CTxIn(COutPoint(p2sh_tx.sha256, 0), b''))
            spent_p2sh_tx.vout.append(CTxOut(1, output_script))
            # Sign the transaction using the redeem script
            sighash = SignatureHashForkId(
                redeem_script, spent_p2sh_tx, 0, SIGHASH_ALL | SIGHASH_FORKID, p2sh_tx.vout[0].nValue)
            sig = private_key.sign(sighash) + \
                bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
            spent_p2sh_tx.vin[0].scriptSig = CScript([sig, redeem_script])
            spent_p2sh_tx.rehash()
            return spent_p2sh_tx

        # Sigops p2sh limit
        p2sh_sigops_limit = MAX_BLOCK_SIGOPS_PER_MB - \
            redeem_script.GetSigOpCount(True)
        # Too many sigops in one p2sh txn
        too_many_p2sh_sigops = CScript([OP_CHECKSIG] * (p2sh_sigops_limit + 1))
        block(31, spend=out[23], block_size=ONE_MEGABYTE + 1)
        self.chain.update_block(31, [spend_p2sh_tx(too_many_p2sh_sigops)])
        yield self.rejected(RejectResult(16, b'bad-txn-sigops'))

        # Rewind bad block
        self.chain.set_tip(30)

        # Max sigops in one p2sh txn
        max_p2sh_sigops = CScript([OP_CHECKSIG] * (p2sh_sigops_limit))
        block(32, spend=out[23], block_size=ONE_MEGABYTE + 1)
        self.chain.update_block(32, [spend_p2sh_tx(max_p2sh_sigops)])
        yield self.accepted()

        # Submit a very large block via RPC
        large_block = block(
            33, spend=out[24], block_size=self.excessive_block_size)
        node.submitblock(ToHex(large_block))


if __name__ == '__main__':
    FullBlockTest().main()
