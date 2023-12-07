#!/usr/bin/env python3
# Copyright (c) 2019  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from time import sleep

from test_framework.blocktools import create_block, create_coinbase
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, NetworkThread, CInv
from test_framework.script import CScript, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG, \
    OP_CODESEPARATOR, OP_TRUE, OP_VERIFY, OP_CHECKSIGVERIFY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until


def make_coinbase(connection):
    "Create and send block with coinbase, returns conbase (tx, key) tuple"
    tip = connection.rpc.getblock(connection.rpc.getbestblockhash())

    coinbase_key = CECKey()
    coinbase_key.set_secretbytes(b"horsebattery")
    coinbase_tx = create_coinbase(tip["height"] + 1, coinbase_key.get_pubkey())
    coinbase_tx.rehash()

    block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
    block.solve()

    connection.send_message(msg_block(block))
    wait_until(lambda: connection.rpc.getbestblockhash() == block.hash, timeout=10)

    return coinbase_tx, coinbase_key


def make_separator_tx(tx_to_spend, key_for_tx_to_spend, n_sgnings):
    """create Transaction with scriptPubKey in form of:
    <pk1> OP_CHECKSIGVERIFY OP_CODESEPARATOR <pk2> OP_CHECKSIGVERIFY OP_CODESEPARATOR ... <pk N_signings> OP_CHECKSIG
    """
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, 0), b"", 0xffffffff))

    keys = []
    script_list = []
    for i in range(n_sgnings - 1):
        k = CECKey()
        k.set_secretbytes(b"x" * (i+1))
        keys.append(k)
        script_list.extend([k.get_pubkey(), OP_CHECKSIGVERIFY, OP_CODESEPARATOR])

    k = CECKey()
    k.set_secretbytes(b"x" * n_sgnings)
    keys.append(k)
    script_list.extend([k.get_pubkey(), OP_CHECKSIG])

    amount = tx_to_spend.vout[0].nValue - 2000
    tx.vout.append(CTxOut(amount, CScript(script_list)))

    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[0].nValue)
    tx.vin[0].scriptSig = CScript([key_for_tx_to_spend.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    tx.rehash()
    return tx, keys


def spend_separator_tx(tx_sep_tx, keys_for_sep_tx):
    """spends Transaction with scriptPubKey in form of:
    <pk1> OP_CHECKSIGVERIFY OP_CODESEPARATOR <pk2> OP_CHECKSIGVERIFY OP_CODESEPARATOR ... <pk N_signings> OP_CHECKSIG
    """

    tx = CTransaction()

    tx.vin.append(CTxIn(COutPoint(tx_sep_tx.sha256, 0), b"", 0xffffffff))

    k = CECKey()
    k.set_secretbytes(b"horsebattery")

    amount = tx_sep_tx.vout[0].nValue - 2000

    script_lists = [[]]

    for item in list(tx_sep_tx.vout[0].scriptPubKey):
        for l in script_lists:
            l.append(item)
        if item == OP_CODESEPARATOR:
            script_lists.append([])

    tx.vout.append(CTxOut(amount, CScript([k.get_pubkey(), OP_CHECKSIG])))

    flags = bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))

    sign_list = []
    for sc, key in zip(script_lists, keys_for_sep_tx):
        sighash = SignatureHashForkId(CScript(sc), tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_sep_tx.vout[0].nValue)
        sign_list.append(key.sign(sighash) + flags)

    tx.vin[0].scriptSig = CScript(reversed(sign_list))
    tx.rehash()
    return tx, k


# Tests creation and spending transactions with scriptPubKey in form:
# <pk1> OP_CHECKSIGVERIFY OP_CODESEPARATOR <pk2> OP_CHECKSIGVERIFY OP_CODESEPARATOR ... <pk N_signings> OP_CHECKSIG

class InvalidTx(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        with self.run_node_with_connections("Scenario 1", 0, ['-acceptnonstdtxn=1'], number_of_connections=1) as (conn,):
            coinbase_tx, coinbase_key = make_coinbase(conn)
            conn.rpc.generate(100)

            sep_tx, sep_keys = make_separator_tx(coinbase_tx, coinbase_key, 5)

            conn.send_message(msg_tx(sep_tx))
            sleep(1)
            conn.rpc.generate(10)

            tx, _ = spend_separator_tx(sep_tx, sep_keys)
            conn.send_message(msg_tx(tx))

            wait_until(lambda: len(conn.rpc.getrawmempool()) == 1, timeout=5)


if __name__ == '__main__':
    InvalidTx().main()
