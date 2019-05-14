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

    # Sign a transaction, using the key we know about.
    # This signs input 0 in tx, which is assumed to be spending output n in spend_tx
    def sign_tx(self, tx, spend_tx, n):
        scriptPubKey = bytearray(spend_tx.vout[n].scriptPubKey)
        if (scriptPubKey[0] == OP_TRUE):  # an anyone-can-spend
            tx.vin[0].scriptSig = CScript()
            return
        sighash = SignatureHashForkId(
            spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript(
            [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    # Create a new block with some number of valid spending txns
    def next_block(self, number, spend=None, additional_coinbase_value=0, script=CScript([OP_TRUE])):
        if self.chain.tip == None:
            base_block_hash = self.chain._genesis_hash
            block_time = int(time.time()) + 1
        else:
            base_block_hash = self.chain.tip.sha256
            block_time = self.chain.tip.nTime + 1
        # First create the coinbase
        height = self.chain.block_heights[base_block_hash] + 1
        coinbase = create_coinbase(height, self.coinbase_pubkey)
        coinbase.vout[0].nValue += additional_coinbase_value
        coinbase.rehash()
        if spend == None:
            block = create_block(base_block_hash, coinbase, block_time)
        else:
            # All but one satoshi for each txn to fees
            for s in spend:
                coinbase.vout[0].nValue += s.tx.vout[s.n].nValue - 1 
                coinbase.rehash()
            block = create_block(base_block_hash, coinbase, block_time)
            # Add as many txns as required
            for s in spend:
                # Spend 1 satoshi
                tx = create_transaction(s.tx, s.n, b"", 1, script)
                self.sign_tx(tx, s.tx, s.n)
                self.chain.add_transactions_to_block(block, [tx])
                block.hashMerkleRoot = block.calc_merkle_root()
        # Do PoW, which is very inexpensive on regnet
        block.solve()
        self.chain.tip = block
        self.chain.block_heights[block.sha256] = height
        assert number not in self.chain.blocks
        self.chain.blocks[number] = block
        return block

    def get_tests(self):
        node = self.nodes[0]
        self.chain.set_genesis_hash( int(node.getbestblockhash(), 16) )

        # Create a new block
        self.next_block(0)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(140):
            self.next_block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # Collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(100):
            out.append(self.chain.get_spendable_output())

        # Create blocks with multiple txns in
        block1 = self.next_block(1, spend=out[0:20])
        block2 = self.next_block(2, spend=out[20:40])

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
