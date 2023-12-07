#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
This test checks the behaviour of P2SH before and after genesis.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
from test_framework.key import CECKey
from test_framework.script import *

from test_framework.util import assert_raises_message


# In this test we are checking behavior of the Wallet when trying to spend pre and post genesis P2SH script
# 1. Importing different P2SH(P2PKH) scripts in wallets on nodes 1 and 2
# 2. Putting tx that funds P2SH on node1 to the block and mine it
# 3. Moving after genesis
# 4. Putting tx that funds P2SH on node2 to the block and mine it
# 5. Check balance on node1 and node2
# 6. Try to spend funds from node1 and node2

class P2SH(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.genesisactivationheight = 150
        self.extra_args = [['-acceptnonstdtxn=0', '-banscore=1000000',
                            f'-genesisactivationheight={self.genesisactivationheight}']] * 3

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block

        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node2 = self.nodes[2]

        self.chain.set_genesis_hash(int(node1.getbestblockhash(), 16))

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(0,100):
            block(i, coinbase_pubkey=self.coinbase_pubkey)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # create two addresses on the node0
        address1 = node0.getnewaddress()
        scriptPubKey1 = node0.validateaddress(address1)["scriptPubKey"]
        address2 = node0.getnewaddress()
        scriptPubKey2 = node0.validateaddress(address2)["scriptPubKey"]

        # import P2SH(P2PKH) on node1 and node2
        # have to do in this way because it seems that we can't create P2PKH address and later add P2SH(P2PKH) to the same private key
        node1.importaddress(scriptPubKey1, "x", True, True) # importing script, not key
        node1.importprivkey(node0.dumpprivkey(address1))
        node2.importaddress(scriptPubKey2, "x", True, True) # importing script, not key
        node2.importprivkey(node0.dumpprivkey(address2))

        out = [self.chain.get_spendable_output() for _ in range(50)]

        # Create a p2sh transactions
        def new_P2SH_tx(scriptPubKey):
            output = out.pop(0)
            redeem_script = CScript(hex_str_to_bytes(scriptPubKey))
            redeem_script_hash = hash160(redeem_script)
            p2sh_script = CScript([OP_HASH160, redeem_script_hash, OP_EQUAL])
            return create_and_sign_transaction(spend_tx=output.tx, n=output.n,
                                               value=output.tx.vout[0].nValue-100,
                                               private_key=self.coinbase_key,
                                               script=p2sh_script)

        # Add the transactions to the block
        assert node0.getblockcount() < self.genesisactivationheight, "We must be before genesis"
        block(100)
        new_tx1 = new_P2SH_tx(scriptPubKey1)
        self.chain.update_block(100, [new_tx1]) # sending funds to P2SH address BEFORE genesis
        yield self.accepted()

        current_height = node1.getblockcount()

        for i in range(self.genesisactivationheight - current_height):
            block(101+i, coinbase_pubkey=self.coinbase_pubkey)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        assert node0.getblockcount() >= self.genesisactivationheight, "We must be after genesis"

        block(150)
        new_tx2 = new_P2SH_tx(scriptPubKey2)
        self.chain.update_block(150, [new_tx2]) # sending funds to P2SH address AFTER genesis
        yield self.rejected(RejectResult(16, b'bad-txns-vout-p2sh'))

        self.chain.set_tip(149)

        balance1 = node1.getbalance("*", 1, False)
        assert balance1 * COIN == new_tx1.vout[0].nValue, "Wallet has registered pre genesis transaction."
        balance2 = node2.getbalance("*", 1, False)
        assert balance2 * COIN == 0, "No funds in wallet as transaction is not accepted."

        # Pre genesis P2SH transaction can be spent through wallet
        node1.sendtoaddress(node0.getnewaddress(), balance1 - 1)

        balance1_new = node1.getbalance("*", 1, False)
        assert balance1 > balance1_new, "Pre genesis P2SH is spent."


if __name__ == '__main__':
    P2SH().main()
