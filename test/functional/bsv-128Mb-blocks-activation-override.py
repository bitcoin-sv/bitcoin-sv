#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2018 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test that the new default 128Mb blocks activates correctly in conjunction
with the excessiveblocksize parameter.

In short; after the Nov 15th HF date the default max block size should become
128Mb, unless the user has explictly set the max block size via the
excessiveblocksize parameter, in which case we should continue to honor
that value.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.util import assert_equal
from test_framework.cdefs import (ONE_MEGABYTE, MAX_TX_SIGOPS_COUNT)

import os
import shutil
from collections import deque

# Far into the future
MAGNETIC_START_TIME = 2000000000

class PreviousSpendableOutput():

    def __init__(self, tx=CTransaction(), n=-1):
        self.tx = tx
        self.n = n  # the output we're spending

# Test the HF activation of 128Mb blocks
class BSV128MbActivation(ComparisonTestFramework):

    # Can either run this test as 1 node with expected answers, or two and compare them.
    # Change the "outcome" variable from each TestInstance object to only do
    # the comparison.

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.block_heights = {}
        self.tip = None
        self.blocks = {}
        self.excessive_block_size = 64 * ONE_MEGABYTE
        self.extra_args = [['-whitelist=127.0.0.1',
                            "-magneticactivationtime=%d" % MAGNETIC_START_TIME,
                            "-excessiveblocksize=%d" % self.excessive_block_size]]

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option(
            "--runbarelyexpensive", dest="runbarelyexpensive", default=True)

    def run_test(self):
        self.nodes[0].setmocktime(MAGNETIC_START_TIME)
        self.test.run()

    def add_transactions_to_block(self, block, tx_list):
        [tx.rehash() for tx in tx_list]
        block.vtx.extend(tx_list)

    # this is a little handier to use than the version in blocktools.py
    def create_tx(self, spend, value, script=CScript([OP_TRUE])):
        tx = create_transaction(spend.tx, spend.n, b"", value, script)
        return tx

    def next_block(self, number, spend=None, script=CScript([OP_TRUE]), block_size=0, extra_sigops=0):
        if self.tip == None:
            base_block_hash = self.genesis_hash
            block_time = int(time.time()) + 1
        else:
            base_block_hash = self.tip.sha256
            block_time = self.tip.nTime + 1
        # First create the coinbase
        height = self.block_heights[base_block_hash] + 1
        coinbase = create_coinbase(height)
        coinbase.rehash()
        if spend == None:
            # We need to have something to spend to fill the block.
            assert_equal(block_size, 0)
            block = create_block(base_block_hash, coinbase, block_time)
        else:
            # all but one satoshi to fees
            coinbase.vout[0].nValue += spend.tx.vout[spend.n].nValue - 1
            coinbase.rehash()
            block = create_block(base_block_hash, coinbase, block_time)

            # Make sure we have plenty engough to spend going forward.
            spendable_outputs = deque([spend])

            def get_base_transaction():
                # Create the new transaction
                tx = CTransaction()
                # Spend from one of the spendable outputs
                spend = spendable_outputs.popleft()
                tx.vin.append(CTxIn(COutPoint(spend.tx.sha256, spend.n)))
                # Add spendable outputs
                for i in range(4):
                    tx.vout.append(CTxOut(0, CScript([OP_TRUE])))
                    spendable_outputs.append(PreviousSpendableOutput(tx, i))
                return tx

            tx = get_base_transaction()

            # Make it the same format as transaction added for padding and save the size.
            # It's missing the padding output, so we add a constant to account for it.
            tx.rehash()
            base_tx_size = len(tx.serialize()) + 18

            # If a specific script is required, add it.
            if script != None:
                tx.vout.append(CTxOut(1, script))

            # Put some random data into the first transaction of the chain to randomize ids.
            tx.vout.append(
                CTxOut(0, CScript([random.randint(0, 256), OP_RETURN])))

            # Add the transaction to the block
            self.add_transactions_to_block(block, [tx])

            # If we have a block size requirement, just fill
            # the block until we get there
            current_block_size = len(block.serialize())
            while current_block_size < block_size:
                # We will add a new transaction. That means the size of
                # the field enumerating how many transaction go in the block
                # may change.
                current_block_size -= len(ser_compact_size(len(block.vtx)))
                current_block_size += len(ser_compact_size(len(block.vtx) + 1))

                # Create the new transaction
                tx = get_base_transaction()

                # Add padding to fill the block.
                script_length = block_size - current_block_size - base_tx_size
                if script_length > 510000:
                    if script_length < 1000000:
                        # Make sure we don't find ourselves in a position where we
                        # need to generate a transaction smaller than what we expected.
                        script_length = script_length // 2
                    else:
                        script_length = 500000
                tx_sigops = min(extra_sigops, script_length,
                                MAX_TX_SIGOPS_COUNT)
                extra_sigops -= tx_sigops
                script_pad_len = script_length - tx_sigops
                script_output = CScript(
                    [b'\x00' * script_pad_len] + [OP_CHECKSIG] * tx_sigops)
                tx.vout.append(CTxOut(0, script_output))

                # Add the tx to the list of transactions to be included
                # in the block.
                self.add_transactions_to_block(block, [tx])
                current_block_size += len(tx.serialize())

            # Now that we added a bunch of transaction, we need to recompute
            # the merkle root.
            block.hashMerkleRoot = block.calc_merkle_root()

        # Check that the block size is what's expected
        if block_size > 0:
            assert_equal(len(block.serialize()), block_size)

        # Do PoW, which is cheap on regnet
        block.solve()
        self.tip = block
        self.block_heights[block.sha256] = height
        assert number not in self.blocks
        self.blocks[number] = block
        return block


    def get_tests(self):

        # save the current tip so it can be spent by a later block
        def save_spendable_output():
            spendable_outputs.append(self.tip)

        # get an output that we previously marked as spendable
        def get_spendable_output():
            return PreviousSpendableOutput(spendable_outputs.pop(0).vtx[0], 0)

        # returns a test case that asserts that the current tip was accepted
        def accepted():
            return TestInstance([[self.tip, True]])

        # returns a test case that asserts that the current tip was rejected
        def rejected(reject=None):
            if reject is None:
                return TestInstance([[self.tip, False]])
            else:
                return TestInstance([[self.tip, reject]])

        # move the tip back to a previous block
        def tip(number):
            self.tip = self.blocks[number]

        # adds transactions to the block and updates state
        def update_block(block_number, new_transactions):
            block = self.blocks[block_number]
            self.add_transactions_to_block(block, new_transactions)
            old_sha256 = block.sha256
            block.hashMerkleRoot = block.calc_merkle_root()
            block.solve()
            # Update the internal state just like in next_block
            self.tip = block
            if block.sha256 != old_sha256:
                self.block_heights[
                    block.sha256] = self.block_heights[old_sha256]
                del self.block_heights[old_sha256]
            self.blocks[block_number] = block
            return block

        # Test no excessiveblocksize parameter specified
        node = self.nodes[0]
        self.genesis_hash = int(node.getbestblockhash(), 16)
        self.block_heights[self.genesis_hash] = 0
        spendable_outputs = []

        # shorthand for functions
        block = self.next_block

        # Create a new block
        block(0)
        save_spendable_output()
        yield accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(99):
            block(5000 + i)
            test.blocks_and_transactions.append([self.tip, True])
            save_spendable_output()
        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(100):
            out.append(get_spendable_output())

        # Let's build some blocks and test them.
        for i in range(15):
            n = i + 1
            block(n, spend=out[i], block_size=n * ONE_MEGABYTE)
            yield accepted()

        # Start moving MTP forward
        bfork = block(5555, out[15], block_size=32 * ONE_MEGABYTE)
        bfork.nTime = MAGNETIC_START_TIME - 1
        update_block(5555, [])
        yield accepted()

        # Get to one block of the Nov 15, 2018 HF activation
        for i in range(5):
            block(5100 + i)
            test.blocks_and_transactions.append([self.tip, True])
        yield test

        # Check that the MTP is just before the configured fork point.
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     MAGNETIC_START_TIME - 1)

        # Before we acivate the Nov 15, 2018 HF, 64MB is the limit.
        # Oversized blocks will cause us to be disconnected
        assert(not self.test.test_nodes[0].closed)
        block(4444, spend=out[16], block_size=self.excessive_block_size + 1)
        self.test.connections[0].send_message(msg_block((self.tip)))
        self.test.wait_for_disconnections()
        assert(self.test.test_nodes[0].closed)

        # Rewind bad block and remake connection to node
        tip(5104)
        self.test.clear_all_connections()
        self.test.add_all_connections(self.nodes)
        NetworkThread().start()
        self.test.wait_for_verack()

        # Activate the Nov 15, 2018 HF
        block(5556)
        yield accepted()

        # Now MTP is exactly the fork time. Bigger blocks are now accepted.
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     MAGNETIC_START_TIME)

        # Block of maximal size (excessiveblocksize)
        block(17, spend=out[18], block_size=self.excessive_block_size)
        yield accepted()

        # Oversized blocks will cause us to be disconnected
        assert(not self.test.test_nodes[0].closed)
        block(18, spend=out[19], block_size=self.excessive_block_size + 1)
        self.test.connections[0].send_message(msg_block((self.tip)))
        self.test.wait_for_disconnections()
        assert(self.test.test_nodes[0].closed)

        # Rewind bad block and remake connection to node
        tip(17)
        self.test.clear_all_connections()
        self.test.add_all_connections(self.nodes)
        NetworkThread().start()
        self.test.wait_for_verack()

        # Check we can still mine a good size block
        block(5557, spend=out[20], block_size=self.excessive_block_size)
        yield accepted()


if __name__ == '__main__':
    BSV128MbActivation().main()
