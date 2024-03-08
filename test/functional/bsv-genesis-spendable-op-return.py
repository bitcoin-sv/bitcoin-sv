#!/usr/bin/env python3
# Copyright (c) 2019  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
import json
import urllib
from time import sleep

from rest import http_get_call
from test_framework.blocktools import create_block, create_coinbase
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, NetworkThread, CInv, \
    ToHex
from test_framework.script import CScript, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG, \
    OP_CODESEPARATOR, OP_TRUE, OP_VERIFY, OP_CHECKSIGVERIFY, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, bytes_to_hex_str

OP_TRUE_OP_RETURN_SCRIPT = CScript([OP_TRUE, OP_RETURN, b"xxx"])


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


def spend_tx_to_data(tx_to_spend, key_for_tx_to_spend):
    "Create and send block with coinbase, returns conbase (tx, key) tuple"
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, 0), b"", 0xffffffff))

    amount = tx_to_spend.vout[0].nValue - 2000
    tx.vout.append(CTxOut(amount, OP_TRUE_OP_RETURN_SCRIPT))

    sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[0].nValue)
    tx.vin[0].scriptSig = CScript([key_for_tx_to_spend.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    tx.rehash()
    return tx


# This test tries to check if OP_TRUE OP_RETURN tx is in mempook
class SpendingOpReturnTx(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        with self.run_node_with_connections("Scenario 1", 0, ['-acceptnonstdtxn=1', '-genesisactivationheight=10'], number_of_connections=1) as (conn,):
            coinbase_tx, coinbase_key = make_coinbase(conn)
            conn.rpc.generate(100)

            tx_data = spend_tx_to_data(coinbase_tx, coinbase_key)
            conn.send_message(msg_tx(tx_data))

            conn.cb.sync_with_ping()

            url = urllib.parse.urlparse(self.nodes[0].url)
            json_mempool = json.loads(http_get_call(url.hostname, url.port, f'/rest/mempool/contents.json'))
            json_tx = json.loads(http_get_call(url.hostname, url.port, f'/rest/getutxos/checkmempool/{tx_data.hash}-0.json'))

            assert len(json_mempool) == 1, f"Only one tx should be in mempool. Found {len(json_mempool)}"
            assert tx_data.hash in json_mempool, "Our tx should be in mempool"
            assert json_tx['utxos'][0]['scriptPubKey']['hex'] == bytes_to_hex_str(OP_TRUE_OP_RETURN_SCRIPT)


if __name__ == '__main__':
    SpendingOpReturnTx().main()
