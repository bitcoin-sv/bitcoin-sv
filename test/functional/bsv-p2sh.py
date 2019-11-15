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

# a little handier version of create_transaction
def create_tx(spend_tx, n, value, script=CScript([OP_TRUE])):
    tx = create_transaction(spend_tx, n, b"", value, script)
    return tx

# sign a transaction, using the key we know about
# this signs input 0 in tx, which is assumed to be spending output n in
# spend_tx
def sign_tx(tx, spend_tx, n, coinbase_key):
    scriptPubKey = bytearray(spend_tx.vout[n].scriptPubKey)
    if (scriptPubKey[0] == OP_TRUE):  # an anyone-can-spend
        tx.vin[0].scriptSig = CScript()
        return
    sighash = SignatureHashForkId(
        spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
    tx.vin[0].scriptSig = CScript(
        [coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

def create_and_sign_transaction(spend_tx, n, value, coinbase_key, script=CScript([OP_TRUE])):
    tx = create_tx(spend_tx, n, value, script)
    sign_tx(tx, spend_tx, n, coinbase_key)
    tx.rehash()
    return tx

SPEND_OUTPUT = CScript([OP_FALSE,OP_RETURN]) # Output script used by spend transactions. Could be anything that is standard, but OP_FALSE OP_RETURN is the easiest to create.

class P2SH(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.genesisactivationheight = 203
         # Build the redeem script, hash it, use hash to create the p2sh script
        self.redeem_script = CScript([self.coinbase_pubkey, OP_2DUP, OP_CHECKSIGVERIFY, OP_CHECKSIG])
        self.p2sh_script = CScript([OP_HASH160, hash160(self.redeem_script), OP_EQUAL])


    def setup_network(self):
        self.extra_args = [['-norelaypriority', '-acceptnonstdtxn=0', f'-genesisactivationheight={self.genesisactivationheight}']]
        self.add_nodes(self.num_nodes, self.extra_args)
        self.start_nodes()
        self.init_network()

    def run_test(self):
        self.test.run()


    # Creates a new transaction using a p2sh transaction as input
    def spend_p2sh_tx(self, p2sh_tx_to_spend, output_script=SPEND_OUTPUT, privateKey=None):
        privateKey = privateKey or self.coinbase_key
        # Create the transaction
        spent_p2sh_tx = CTransaction()
        spent_p2sh_tx.vin.append(
            CTxIn(COutPoint(p2sh_tx_to_spend.sha256, 0), b''))
        spent_p2sh_tx.vout.append(CTxOut(p2sh_tx_to_spend.vout[0].nValue-100, output_script))
        # Sign the transaction using the redeem script
        sighash = SignatureHashForkId(
            self.redeem_script, spent_p2sh_tx, 0, SIGHASH_ALL | SIGHASH_FORKID, p2sh_tx_to_spend.vout[0].nValue)
        sig = privateKey.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
        spent_p2sh_tx.vin[0].scriptSig = CScript([sig, self.redeem_script])
        spent_p2sh_tx.rehash()
        return spent_p2sh_tx

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash( int(node.getbestblockhash(), 16) )

        # Create and mature coinbase txs
        test = TestInstance(sync_every_block=False)
        for i in range(200):
            block(i, coinbase_pubkey=self.coinbase_pubkey)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        coinbase_utxos = [self.chain.get_spendable_output() for _ in range(50)]

        # Create a p2sh transactions that spends coinbase tx
        def new_P2SH_tx():
            output = coinbase_utxos.pop(0)
            return create_and_sign_transaction(spend_tx=output.tx, n=output.n,
                                               value=output.tx.vout[0].nValue-100,
                                               coinbase_key=self.coinbase_key,
                                               script=self.p2sh_script)

        # Add the transactions to the block
        block(200)
        p2sh_txs = [new_P2SH_tx() for _ in range(4)]
        self.chain.update_block(200, p2sh_txs)
        yield self.accepted()



        coinbase_to_p2sh_tx = new_P2SH_tx()

        # rpc tests
        node.signrawtransaction(ToHex(coinbase_to_p2sh_tx)) # check if we can sign this tx (sign is unused)
        coinbase_to_p2sh_tx_id = node.sendrawtransaction(ToHex(coinbase_to_p2sh_tx)) # sending using rpc

        # Create new private key that will fail with the redeem script
        wrongPrivateKey = CECKey()
        wrongPrivateKey.set_secretbytes(b"wrongkeysecret")
        wrongkey_txn = self.spend_p2sh_tx(p2sh_txs[0], privateKey=wrongPrivateKey)
        # A transaction with this output script can't get into the mempool
        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed",
            node.sendrawtransaction, ToHex(wrongkey_txn))

        # A transaction with this output script can get into the mempool
        correctkey_tx = self.spend_p2sh_tx(p2sh_txs[1])
        correctkey_tx_id = node.sendrawtransaction(ToHex(correctkey_tx))
        assert_equal(set(node.getrawmempool()), {correctkey_tx_id, coinbase_to_p2sh_tx_id})

        block(201)
        self.chain.update_block(201, [correctkey_tx, coinbase_to_p2sh_tx])
        yield self.accepted()

        assert node.getblockcount() == self.genesisactivationheight - 1
        # This block will be at genesis height
        # P2SH transactions are still accepted if they are in the block
        block(202)
        p2sh_tx_after_genesis1 = new_P2SH_tx()
        p2sh_tx_after_genesis2 = new_P2SH_tx()
        p2sh_tx_after_genesis3 = new_P2SH_tx()
        self.chain.update_block(202, [p2sh_tx_after_genesis1, p2sh_tx_after_genesis2, p2sh_tx_after_genesis3])
        yield self.accepted()

        # we are at gensis height
        assert node.getblockcount() == self.genesisactivationheight

        # P2SH transactions are not treated as standard any more and they cant enter the mempool
        assert_raises_rpc_error(-26, "scriptpubkey",
                                node.sendrawtransaction, ToHex(new_P2SH_tx()))

        # Create new private key that would fail with the old redeem script, the same behavior as before genesis
        wrongPrivateKey = CECKey()
        wrongPrivateKey.set_secretbytes(b"wrongkeysecret")
        wrongkey_txn = self.spend_p2sh_tx(p2sh_txs[2], privateKey=wrongPrivateKey)
        # A transaction with this output script can't get into the mempool
        assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed",
            node.sendrawtransaction, ToHex(wrongkey_txn))

        # We can spend old P2SH transactions
        correctkey_tx = self.spend_p2sh_tx(p2sh_txs[3])
        sign_result = node.signrawtransaction(ToHex(correctkey_tx))
        assert sign_result['complete'], "Should be able to sign"
        correctkey_tx_id = node.sendrawtransaction(ToHex(correctkey_tx))
        assert_equal(set(node.getrawmempool()), {correctkey_tx_id})

        # But we can not spend P2SH that was created after genesis, nonstandard
        tx_spends_p2sh_after_genesis = self.spend_p2sh_tx(p2sh_tx_after_genesis1)
        sign_result = node.signrawtransaction(ToHex(tx_spends_p2sh_after_genesis))
        assert sign_result['complete'] == False, "Should NOT be able to sign"
        assert_raises_rpc_error(-26, "bad-txns-nonstandard-inputs",
                                node.sendrawtransaction, ToHex(tx_spends_p2sh_after_genesis))

        # Tx that spends P2SH afer genesis will be correct if it is in the block already
        block(203)
        tx_spends_p2sh_after_genesis2 = self.spend_p2sh_tx(p2sh_tx_after_genesis2)
        self.chain.update_block(203, [tx_spends_p2sh_after_genesis2])
        yield self.accepted()

        # The tx that spends post genesis P2SH with wrong sign is valid beacause the hash of the script matches and script itself is not evaluated
        block(204)
        tx_spends_p2sh_after_genesis_wrong_key = self.spend_p2sh_tx(p2sh_tx_after_genesis3, privateKey=wrongPrivateKey)
        self.chain.update_block(204, [tx_spends_p2sh_after_genesis_wrong_key])
        yield self.accepted()

        tx1_raw = node.getrawtransaction(p2sh_txs[0].hash, True)
        assert tx1_raw["vout"][0]["scriptPubKey"]["type"] == "scripthash"
        tx2_raw = node.getrawtransaction(p2sh_tx_after_genesis1.hash, True)
        assert tx2_raw["vout"][0]["scriptPubKey"]["type"] == "nonstandard"


if __name__ == '__main__':
    P2SH().main()
