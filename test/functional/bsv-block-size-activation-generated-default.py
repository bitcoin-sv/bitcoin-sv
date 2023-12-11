#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test that the new default generated (mined) block size works correctly without the use
of the blockmaxsize parameter.

In short; if the user doesn't override things via the blockmaxsize
parameter then after the NEW_BLOCKSIZE_ACTIVATION_TIME date the default generated block
size should increase to DEFAULT_MAX_BLOCK_SIZE_*_AFTER
"""
import math
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import *
from test_framework.mininode import *
from test_framework.script import CScript, OP_TRUE, OP_RETURN
from test_framework.blocktools import *

from test_framework.cdefs import (ONE_MEGABYTE, MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS, REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME,
                                  REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE, REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)

DEFAULT_ACTIVATION_TIME = REGTEST_NEW_BLOCKSIZE_ACTIVATION_TIME # can be orverriden on command line
DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE = REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE
DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER = REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER


class BSVGeneratedBlockSizeActivation(BitcoinTestFramework):

    def add_options(self, parser):
        super().add_options(parser)
        parser.add_option("--blocksizeactivationtime", dest="blocksizeactivationtime", type='int')

    def set_test_params(self):
        self.data_carrier_size = 500000
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-whitelist=127.0.0.1', "-datacarriersize=%d" % self.data_carrier_size,
                            '-jbathrottlethreshold=100']]

    def setup_nodes(self):
        # Append -blocksizeactivationtime only if explicitly specified
        # We can not do this in set_test_params, because self.options has yet initialized there
        self.activation_time = DEFAULT_ACTIVATION_TIME
        if (self.options.blocksizeactivationtime is not None):
            self.activation_time = self.options.blocksizeactivationtime
            self.extra_args[0].append("-blocksizeactivationtime=%d" % self.options.blocksizeactivationtime)

        self.add_nodes(self.num_nodes, self.extra_args)
        # increase rpc_timeout to avoid getting timeout on generate for block that is hard to validate
        self.nodes[0].rpc_timeout = 300
        self.start_nodes()

    # Create an empty block with given block time. Used to move median past time around
    def mine_empty_block(self, nTime):
        node = self.nodes[0]
        hashPrev = int(node.getbestblockhash(),16)

        coinbase = create_coinbase(node.getblockcount() + 1)
        block = create_block(hashPrev, coinbase, nTime)
        block.solve()
        ret = node.submitblock(ToHex(block))
        assert(ret is None)

    # Ensure funding and returns given number of spend anyone transactions without submitting them
    def make_transactions(self, num_transactions, add_op_return_size = 0): # TODO: Consolidate with bsv-broadcast_delay.py

        # Sanity check - estimate size  of funding transaction and check that it is not too big
        assert(200 + num_transactions * 20 < MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS)

        node = self.nodes[0]
        # Generate and age some blocks to have enough spendable coins
        node.generate(101)

        # Create funding transactions that will provide funds for other transactions
        out_value = 1000000
        ftx = CTransaction()
        for i in range(num_transactions):
            ftx.vout.append(CTxOut(out_value, CScript([OP_TRUE]))) # anyone can spend

        # Fund the transaction
        ftxHex = node.fundrawtransaction(ToHex(ftx),{'changePosition' : len(ftx.vout)})['hex']
        ftxHex = node.signrawtransaction(ftxHex)['hex']
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()

        node.sendrawtransaction(ftxHex)

        node.generate(1) # ensure that mempool is empty to avoid 'too-long-mempool-chain' errors in next test

        # Create transactions that depends on funding transactions that has just been submitted
        txs = []
        for i in range(num_transactions):
            fee = 200 + add_op_return_size
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(ftx.sha256, i), b''))
            tx.vout.append(CTxOut(out_value - fee, CScript([OP_TRUE])))

            # Add big output if requested
            if (add_op_return_size > 0):
                tx.vout.append(CTxOut(int(0), CScript([OP_RETURN,  b"a" * add_op_return_size])))

            tx.rehash()
            txs.append(tx)

        return txs

    def run_test(self):

        if DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE == REGTEST_DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER:
            self.log.info("Default generated block size is equal before and after activation. Nothing to test")
            return # do not use SkipTest(), since this fails build on Jenkins

        node = self.nodes[0]

        # set bitcoind time to be before activation time
        node.setmocktime(self.activation_time - 3600)

        activation_time = self.activation_time
        self.log.info("Using activation time %d " % activation_time)

        # Generate cca 10 more transactions that needed to avoid problems with rounding
        nrTransactions = math.ceil(
            (DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE
                + DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER
                + 10 * (200 + self.data_carrier_size))
            / (200 + self.data_carrier_size))

        self.log.info("Required number of transactions: %d " % nrTransactions)
        txs = self.make_transactions(nrTransactions, self.data_carrier_size)

        # Bring mock time close to the activation time, so that node will not reject block with time-too-new
        node.setmocktime(activation_time-1)

        # Create and submit 6 empty block with [activation_time-1 .. activation_time+4] to bring MPT to activation_time-1
        for i in range(6):
            self.mine_empty_block(activation_time-1 + i)

        mpt = node.getblockheader(node.getbestblockhash())['mediantime']
        assert_equal(mpt, activation_time-1)

        # Send all of the transactions:
        for tx in txs:
            node.sendrawtransaction(ToHex(tx))

        mempool_size_before = node.getmempoolinfo()['bytes']

        # Mempool should now contain enough transactions for two blocks
        assert(mempool_size_before > DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE + DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)

        # Mine the next block with unique block time. MPT before mining the block
        # will be activation_time - 1, so the old rules should apply
        # Block size should be near DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE

        node.setmocktime(activation_time + 5)
        block1Hash = node.generate(1)[0]
        block1Hex = node.getblock(block1Hash, False)
        block1Size = len(block1Hex) /2 # Hex encoded

        # Check if block was mined with the correct time (mpt == activation_time)
        block1Header = node.getblockheader(block1Hash)
        assert(block1Header['mediantime'] == activation_time)
        assert(block1Header['time'] == activation_time + 5)

        # Mining should not consume too much of data
        assert(block1Size < DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE)
        # Mining should consume almost whole block size
        assert(block1Size > DEFAULT_MAX_GENERATED_BLOCK_SIZE_BEFORE - 2 * self.data_carrier_size)

        # Mine another block with unique time. MPT before mining this block
        # should be activation_time. New rules should be activated for this block
        node.setmocktime(activation_time + 6)
        block2Hash = node.generate(1)[0]
        block2hex = node.getblock(block2Hash, False)
        block2size = len(block2hex) /2 # Hex encoded

        # Check if block was mined at the correct time (mpt = activation_time + 1)
        block2Header = node.getblockheader(block2Hash)
        assert(block2Header['mediantime'] == activation_time +1)
        assert(block2Header['time'] == activation_time + 6)

        # Mining should not consume too much of data
        assert(block2size < DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER)
        # Mining should consume almost whole block size
        assert(block2size > DEFAULT_MAX_GENERATED_BLOCK_SIZE_AFTER - 2 * self.data_carrier_size)


if __name__ == '__main__':
    BSVGeneratedBlockSizeActivation().main()
