#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing rpc call sendrawtransactions to return doublespend transactions
and transactions they collided with.

The "invalid" transactions in case of doublespend now have the additional field "collidedWith".
The format of this field is:
collidedWith: [{txid: hexstring,
               size: integer,
               hex: hexstring},
              ...
             ]

Test steps:
1. create tx with spendable output and send it.
2. create tx1 that uses spendable output and send it.
3. create doublespend trransactions tx2, tx3 that use same input as tx1.
4. send tx2, tx3, and receive that transactions tx2, tx3 collided with transaction tx1
"""
from test_framework.script import CTransaction, CScript, OP_TRUE, CTxOut
from test_framework.test_framework import BitcoinTestFramework, ToHex, FromHex
from test_framework.util import assert_equal, wait_until
from test_framework.mininode import CTxIn, COutPoint


class BSVsendrawtransactionsCollided(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        self.nodes[0].generate(101)

        unspent = self.nodes[0].listunspent()[0]

        # create tx with spendable output
        tx_spendable_output = CTransaction()
        tx_outs = [CTxOut(4500000000, CScript([OP_TRUE]))]
        tx_spendable_output.vout = tx_outs
        tx_spendable_output.vin = [CTxIn(COutPoint(int(unspent["txid"], 16), 0))]

        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx_spendable_output))['hex']
        self.nodes[0].sendrawtransaction(tx_hex, True)
        tx_spendable_output = FromHex(CTransaction(), tx_hex)
        tx_spendable_output.rehash()

        wait_until(lambda: len(self.nodes[0].getrawmempool()) == 1)

        tx1 = CTransaction()
        tx_outs = [CTxOut(4300000000, CScript([OP_TRUE]))]
        tx1.vout = tx_outs
        tx1.vin = [CTxIn(COutPoint(int(tx_spendable_output.hash, 16), 0))]

        tx1_hex = self.nodes[0].signrawtransaction(ToHex(tx1))['hex']
        self.nodes[0].sendrawtransaction(tx1_hex, True)
        tx1 = FromHex(CTransaction(), tx1_hex)
        tx1.rehash()

        wait_until(lambda: len(self.nodes[0].getrawmempool()) == 2)

        tx2_doublespend = CTransaction()
        tx_outs = [CTxOut(4400000000, CScript([OP_TRUE]))]
        tx2_doublespend.vout = tx_outs
        tx2_doublespend.vin = [CTxIn(COutPoint(int(tx_spendable_output.hash, 16), 0))]

        tx2_hex_doublespend = self.nodes[0].signrawtransaction(ToHex(tx2_doublespend))['hex']
        tx2_doublespend = FromHex(CTransaction(), tx2_hex_doublespend)
        tx2_doublespend.rehash()

        tx3_doublespend = CTransaction()
        tx_outs = [CTxOut(4200000000, CScript([OP_TRUE]))]
        tx3_doublespend.vout = tx_outs
        tx3_doublespend.vin = [CTxIn(COutPoint(int(tx_spendable_output.hash, 16), 0))]

        tx3_hex_doublespend = self.nodes[0].signrawtransaction(ToHex(tx3_doublespend))['hex']
        tx3_doublespend = FromHex(CTransaction(), tx3_hex_doublespend)
        tx3_doublespend.rehash()

        response = self.nodes[0].sendrawtransactions([{"hex": tx2_hex_doublespend, "allowhighfees": True},
                                                      {"hex": tx3_hex_doublespend, "allowhighfees": True}])

        collidedtx_hashes = [tx2_doublespend.hash, tx3_doublespend.hash]
        assert_equal(response["invalid"][0]["txid"] in collidedtx_hashes, True)
        assert_equal(response["invalid"][0]["collidedWith"][0]["txid"] == tx1.hash, True)
        assert_equal(response["invalid"][0]["collidedWith"][0]["hex"] == tx1_hex, True)
        assert_equal(response["invalid"][0]["collidedWith"][0]["size"], len(tx1_hex)//2)

        assert_equal(response["invalid"][1]["txid"] in collidedtx_hashes, True)
        assert_equal(response["invalid"][1]["collidedWith"][0]["txid"] == tx1.hash, True)
        assert_equal(response["invalid"][1]["collidedWith"][0]["hex"] == tx1_hex, True)
        assert_equal(response["invalid"][1]["collidedWith"][0]["size"], len(tx1_hex) // 2)


if __name__ == '__main__':
    BSVsendrawtransactionsCollided().main()
