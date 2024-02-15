#!/usr/bin/env python3
# Copyright (c) 2019  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from time import sleep
import socket

from test_framework.blocktools import create_block, create_coinbase
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block
from test_framework.script import CScript, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_mempool_equals

_lan_ip = None


def get_lan_ip():
    global _lan_ip
    if _lan_ip: return _lan_ip
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # doesn't even have to be reachable
        s.connect(('10.255.255.255', 1))
        _lan_ip = s.getsockname()[0]
    except:
        _lan_ip = '127.0.0.1'
    finally:
        s.close()
    return _lan_ip


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


def create_parent_tx(tx_to_spend, key_for_tx_to_spend, n_outputs, invalidity=None):
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, 0), b"", 0xffffffff))

    keys = []
    if invalidity == "low_fee":
        amount_per_output = tx_to_spend.vout[0].nValue // n_outputs - 10
    else:
        amount_per_output = tx_to_spend.vout[0].nValue // n_outputs - 2000

    for i in range(n_outputs):
        k = CECKey()
        keys.append(k)
        k.set_secretbytes(b"x" * (i+1))
        tx.vout.append(CTxOut(amount_per_output, CScript([k.get_pubkey(), OP_CHECKSIG])))

    if invalidity == "bad_signature":
        sighash = b"\xff" * 32
    else:
        sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, tx_to_spend.vout[0].nValue)

    tx.vin[0].scriptSig = CScript([key_for_tx_to_spend.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    tx.rehash()
    return tx, keys


def create_children_txs(parent_tx1, keys1, parent_tx2, keys2, invalidity=None):
    ret = []
    for n, (txout1, key1, txout2, key2) in enumerate(zip(parent_tx1.vout, keys1, parent_tx2.vout, keys2)):
        amount1 = txout1.nValue if invalidity == "low_fee" else int(0.99 * txout1.nValue)
        amount2 = txout2.nValue if invalidity == "low_fee" else int(0.99 * txout2.nValue)

        amount = amount1 + amount2

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(parent_tx1.sha256, n), b"", 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(parent_tx2.sha256, n), b"", 0xffffffff))

        k = CECKey()
        k.set_secretbytes(b"x" * (n+1))
        tx.vout.append(CTxOut(amount, CScript([k.get_pubkey(), OP_CHECKSIG])))
        tx.calc_sha256()

        sighash1 = SignatureHashForkId(parent_tx1.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, parent_tx1.vout[n].nValue)
        tx.vin[0].scriptSig = CScript([key1.sign(sighash1) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

        if invalidity == "bad_signature":
            sighash2 = b"\xff" * 32
        else:
            sighash2 = SignatureHashForkId(parent_tx2.vout[n].scriptPubKey, tx, 1, SIGHASH_ALL | SIGHASH_FORKID,
                                           parent_tx2.vout[n].nValue)

        tx.vin[1].scriptSig = CScript([key2.sign(sighash2) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

        tx.rehash()
        ret.append(tx)

    return ret


# Tests sending invalid transactions in four scenarios:
# 1. Sending low-fee transaction from non-whitelisted peer -> tx rejected, not relayed
# 2. Sending low-fee transaction from whitelisted peer -> tx rejected, relayed
# 3. Sending bad signature transaction to peer with high banscore -> tx rejected, not banned
# 4. Sending bad signature transaction to peer with low banscore -> tx rejected, banned

# Then tests sending orphan transaction and validating them when parent is sent in four scenarios:
# 1. Sending valid orphans, before two valid parents -> everything accepted to mempool
# 2. Sending low-fee orphans, before two valid parents -> parents accepted, orphans rejected, not banned
# 3. Sending bad sgnature orphans, before two valid parents -> parents accepted, banned
# 4. Sending valid orphans, before one valid and one invalid parent -> parents accepted, orphans not accepted, not banned
class InvalidTx(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def prepare_invalid_tx(self, conn_sending, conn_receiving, invalidity):

        coinbase_tx, coinbase_key = make_coinbase(conn_sending)
        conn_sending.rpc.generate(100)

        rejected_txs = []

        def on_reject(conn, msg):
            rejected_txs.append(msg)
        conn_sending.cb.on_reject = on_reject

        relayed_txs = []

        def on_inv(conn, msg):
            for i in msg.inv:
                if i.type == 1:
                    relayed_txs.append(i.hash)

        conn_receiving.cb.on_inv = on_inv

        tx, keys = create_parent_tx(coinbase_tx, coinbase_key, n_outputs=1, invalidity=invalidity)

        return tx, rejected_txs, relayed_txs

    def prepare_parents_and_children(self, conn, parent_invalidity, children_invalidity):

        rejected_txs = []

        def on_reject(conn, msg):
            rejected_txs.append(msg)

        conn.cb.on_reject = on_reject

        coinbase_tx1, coinbase_key1 = make_coinbase(conn)
        coinbase_tx2, coinbase_key2 = make_coinbase(conn)
        conn.rpc.generate(100)

        parent_tx1, keys1 = create_parent_tx(coinbase_tx1, coinbase_key1, n_outputs=20)
        parent_tx2, keys2 = create_parent_tx(coinbase_tx2, coinbase_key2, n_outputs=20, invalidity=parent_invalidity)

        orphans = create_children_txs(parent_tx1, keys1, parent_tx2, keys2, invalidity=children_invalidity)

        return parent_tx1, parent_tx2, orphans, rejected_txs

    def check_rejected(self, rejected_txs, should_be_rejected_tx_set):
        wait_until(lambda: {tx.data for tx in rejected_txs} == {o.sha256 for o in should_be_rejected_tx_set}, timeout=20)

    def check_relayed(self, relayed_txs, should_be_relayed_tx_set):
        wait_until(lambda: set(relayed_txs) == {o.sha256 for o in should_be_relayed_tx_set}, timeout=20)

    def run_invalid_tx_scenarios(self):

        with self.run_node_with_connections("Scenario 1: Low fee, non-whitelisted peer", 0, ['-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=2, ip=get_lan_ip()) as (sending_conn, receiving_conn):

            low_fee_tx, rejected_txs, relayed_txs = self.prepare_invalid_tx(sending_conn, receiving_conn, "low_fee")

            sending_conn.send_message(msg_tx(low_fee_tx))

            self.check_rejected(rejected_txs, [low_fee_tx])
            sleep(1) #give time to relay
            assert len(relayed_txs) == 0, "No transaction should be relayed."

        with self.run_node_with_connections("Scenario 2: Low fee, whitelisted peer", 0, [f'-whitelist={get_lan_ip()}','-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=2, ip=get_lan_ip()) as (sending_conn, receiving_conn):

            low_fee_tx, rejected_txs, relayed_txs = self.prepare_invalid_tx(sending_conn, receiving_conn, "low_fee")

            sending_conn.send_message(msg_tx(low_fee_tx))

            self.check_rejected(rejected_txs, [low_fee_tx]) # tx is rejected, but
            self.check_relayed(relayed_txs, [low_fee_tx])   # it is relayed as we are whitelisted

        with self.run_node_with_connections("Scenario 3: Bad signature, big banscore", 0, ['-banscore=10000000','-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=2, ip=get_lan_ip()) as (sending_conn, receiving_conn):
            tx, rejected_txs, relayed_txs = self.prepare_invalid_tx(sending_conn, receiving_conn, "bad_signature")

            sending_conn.send_message(msg_tx(tx))

            self.check_rejected(rejected_txs, [tx])
            sleep(1)
            assert len(relayed_txs) == 0, "No tx should be relayed."
            assert sending_conn.connected, "Should not be banned."

        with self.run_node_with_connections("Scenario 4: Bad signature, small banscore", 0, ['-banscore=1','-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=2, ip=get_lan_ip()) as (sending_conn, receiving_conn):
            tx, rejected_txs, relayed_txs = self.prepare_invalid_tx(sending_conn, receiving_conn, "bad_signature")

            sending_conn.send_message(msg_tx(tx))

            self.check_rejected(rejected_txs, [tx])

            # we will be banned
            sending_conn.cb.wait_for_disconnect(timeout=20)
            assert len(sending_conn.rpc.listbanned()) == 1  # and banned
            sending_conn.rpc.clearbanned()
            assert len(relayed_txs) == 0, "No tx should be relayed."

    def run_invalid_orphans_scenarios(self):

        with self.run_node_with_connections("Scenario 1: Valid orphans", 0, ['-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=1, ip=get_lan_ip()) as (conn,):

            parent_tx1, parent_tx2, orphans, rejected_txs = self.prepare_parents_and_children(conn,
                                                                                              parent_invalidity=None,
                                                                                              children_invalidity=None)

            #sending orphans
            for tx in orphans:
                conn.send_message(msg_tx(tx))

            assert conn.rpc.getmempoolinfo()["size"] == 0, "No transactions should be in the mempool!"
            assert len(rejected_txs) == 0, "No transactions should be rejected!"

            # sending parents
            conn.send_message(msg_tx(parent_tx1))
            conn.send_message(msg_tx(parent_tx2))

            # all transactions should be accepted to mempool
            check_mempool_equals(conn.rpc, orphans + [parent_tx1, parent_tx2])
            assert len(rejected_txs) == 0, "No transactions should be rejected!"

        with self.run_node_with_connections("Scenario 2: low fee orphans", 0, ['-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=1, ip=get_lan_ip()) as (conn,):

            parent_tx1, parent_tx2, orphans, rejected_txs = self.prepare_parents_and_children(conn,
                                                                                              parent_invalidity=None,
                                                                                              children_invalidity="low_fee")

            # sending orphans
            for tx in orphans:
                conn.send_message(msg_tx(tx))

            sleep(1)
            assert conn.rpc.getmempoolinfo()["size"] == 0, "No transactions should be in the mempool!"
            assert len(rejected_txs) == 0, "No transactions should be rejected yet!"

            # sending first parent
            conn.send_message(msg_tx(parent_tx1))
            check_mempool_equals(conn.rpc, [parent_tx1])
            assert len(rejected_txs) == 0, "No transactions should be rejected yet!"

            # sending second parent
            conn.send_message(msg_tx(parent_tx2))
            check_mempool_equals(conn.rpc, [parent_tx1, parent_tx2])

            # check that all orphans are rejected, but we are not banned
            self.check_rejected(rejected_txs, orphans)
            assert conn.connected, "We should still be connected (not banned)."

        with self.run_node_with_connections("Scenario 3: invalid signature orphans", 0, ['-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=1, ip=get_lan_ip()) as (conn,):

            parent_tx1, parent_tx2, orphans, rejected_txs = self.prepare_parents_and_children(conn,
                                                                                              parent_invalidity=None,
                                                                                              children_invalidity="bad_signature")

            # sending orphans
            for tx in orphans:
                conn.send_message(msg_tx(tx))

            sleep(1)
            assert conn.rpc.getmempoolinfo()["size"] == 0, "No transactions should be in the mempool!"
            assert len(rejected_txs) == 0, "No transactions should be rejected yet!"

            conn.send_message(msg_tx(parent_tx1))
            check_mempool_equals(conn.rpc, [parent_tx1])  # Only parent_tx1 should be in the mempool
            assert len(rejected_txs) == 0, "No transactions should be rejected yet!"

            conn.send_message(msg_tx(parent_tx2))
            check_mempool_equals(conn.rpc, [parent_tx1, parent_tx2]) # Only parent_tx1 and parent_tx2 should be in the mempool

            # we must be banned
            conn.cb.wait_for_disconnect(timeout=20) # will be disconnected
            assert len(conn.rpc.listbanned()) == 1 # and banned
            conn.rpc.clearbanned()

        # the banscore is set to 101 because rejection of the tx with invalid signature brings 100 points,
        # we don't want to be banned as result of only one tx
        with self.run_node_with_connections("Scenario 4: bad signature parent", 0, ['-banscore=101','-mindebugrejectionfee=0.0000025'],
                                            number_of_connections=1, ip=get_lan_ip()) as (conn,):

            valid_parent_tx, invalid_parent_tx, orphans, rejected_txs = self.prepare_parents_and_children(conn,
                                                                                                          parent_invalidity="bad_signature",
                                                                                                          children_invalidity=None)

            for tx in orphans:
                conn.send_message(msg_tx(tx))

            sleep(1)
            assert conn.rpc.getmempoolinfo()["size"] == 0, "No transactions should be in the mempool!"
            assert len(rejected_txs) == 0, "No transactions should be rejected yet!"

            conn.send_message(msg_tx(valid_parent_tx))
            check_mempool_equals(conn.rpc, [valid_parent_tx])  # Only valid_parent_tx should be in the mempool
            assert len(rejected_txs) == 0, "No transactions should be rejected yet!"

            conn.send_message(msg_tx(invalid_parent_tx))
            check_mempool_equals(conn.rpc, [valid_parent_tx])  # Still only valid_parent_tx should be in the mempool
            self.check_rejected(rejected_txs, [invalid_parent_tx]) # And only invalid parent is rejected
            sleep(1)
            assert len(conn.rpc.listbanned()) == 0  # not banned

    def run_test(self):
        self.run_invalid_tx_scenarios()
        self.run_invalid_orphans_scenarios()


if __name__ == '__main__':
    InvalidTx().main()
