#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework, sync_blocks
from test_framework.script import CScript, OP_TRUE
from test_framework.mininode import CTransaction, ToHex, CTxIn, CTxOut, COutPoint, FromHex
from test_framework.blocktools import create_block, create_coinbase
import glob

'''
'''
class CreateMinerInfoTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

        #args = ['-maxmempool=4000', '-mindebugrejectionfee=0','-paytxfee=0.00003','-txindex=1']
        args = ['-excessiveblocksize=15MB', '-maxmempool=1000', '-mindebugrejectionfee=0','-paytxfee=0.00003','-txindex=1']

        self.extra_args = [args]

    def make_block_with_coinbase(self):
        node = self.nodes[0]
        tip = node.getblock(node.getbestblockhash())
        coinbase_tx = create_coinbase(tip["height"] + 1)
        block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
        block.solve()
        self.nodes[0].submitblock(ToHex(block))
        coinbase_tx.rehash()
        return coinbase_tx

    def split_inputs(self, txin, index, n):
        if n == 0:
            return 0
        node = self.nodes[0]
        amount = txin.vout[index].nValue - 1
        if amount < 9:
            return n

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(txin.sha256, index), b"", 0xffffffff))

        scriptPubKey = CScript([OP_TRUE])
        amount = int(amount / 2)
        tx.vout.append(CTxOut(amount, scriptPubKey))
        tx.vout.append(CTxOut(amount, scriptPubKey))

        tx.rehash()
        #print ("txid:", tx.hash, " n:", n, " amount:", amount)
        txid = node.sendrawtransaction(ToHex(tx), False, True)
        n -= 1
        n = self.split_inputs(tx, 0, n)
        n = self.split_inputs(tx, 1, n)
        return n

    def make_many_transactions(self, n):
        node = self.nodes[0]

        coinbase_tx = self.make_block_with_coinbase()
        self.nodes[0].generate(101)
        #coinbase_tx = FromHex(CTransaction(), coinbase_tx['hex'])
        nn = self.split_inputs(coinbase_tx, 0, n)
        assert(nn == 0)

    def create_merkleproof(self, doOverflowMemory):
        if doOverflowMemory:
            self.make_many_transactions (1024 * 64)
        else:
            self.make_many_transactions (1024 * 32)

        test_txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.0001)
        self.nodes[0].generate(1)
        bhash = self.nodes[0].getbestblockhash()
        proof = self.nodes[0].getmerkleproof2(bhash, test_txid)

    def find_overflow_message_in_logs(self):
        for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
            if f"Merkle Tree of size" in line and f"will not be written to keep disk size hard limit" in line:
                return True
        return False

    def run_test(self):

        args = ['-mindebugrejectionfee=0','-paytxfee=0.00003','-txindex=1'] 
        self.stop_nodes()
        self.start_nodes(self.extra_args)

        self.create_merkleproof (False)
        assert (not self.find_overflow_message_in_logs())
        self.create_merkleproof (True)
        assert (self.find_overflow_message_in_logs())

if __name__ == '__main__':
    CreateMinerInfoTest().main()
