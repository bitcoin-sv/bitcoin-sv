#!/usr/bin/env python3
# Copyright (c) 2026 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Check that transactions that contain op codes in unlock scripts are rejected
before AND after genesis activation, and accepted after Chronicle activation.
Tests both -acceptnonstdtxn=0 and -acceptnonstdtxn=1.

With -acceptnonstdtxn=0, rejection is at IsStandardTx policy level pre-Chronicle.
With -acceptnonstdtxn=1, transactions are accepted pre-genesis but rejected
post-genesis at the mandatory script verification level (SIGPUSHONLY).
After Chronicle activation, non-push-only scriptSig is accepted with nVersion=2.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.script import CScript, OP_TRUE, OP_ADD, OP_DROP
from test_framework.blocktools import create_transaction, create_coinbase, create_block
from test_framework.util import assert_equal, wait_until
from test_framework.mininode import msg_tx, msg_block


def make_new_block(connection):
    "Create and send block with coinbase, returns coinbase tx"
    tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
    coinbase_tx = create_coinbase(tip["height"] + 1)
    coinbase_tx.rehash()
    block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
    block.solve()
    connection.send_message(msg_block(block))
    wait_until(lambda: connection.rpc.getbestblockhash() == block.hash, timeout=10)
    return coinbase_tx


class GenesisChroniclePushOnlyNonStd(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.genesisactivationheight = 103
        self.chronicleactivationheight = 106

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def create_non_pushonly_tx(self, coinbase, version=1):
        """Create a transaction with non-push-only scriptSig (OP_ADD, OP_DROP)"""
        tx = create_transaction(coinbase, 0, CScript([1, 1, OP_ADD, OP_DROP]), 100000, CScript([OP_TRUE]))
        if version != 1:
            tx.nVersion = version
            tx.rehash()
        return tx

    def assert_rejected_transaction(self, conn, coinbase, expected_reason=b'scriptsig-not-pushonly'):
        rejected_txs = []

        def on_reject(conn, msg):
            rejected_txs.append(msg)

        tx = self.create_non_pushonly_tx(coinbase)
        conn.transport.cb.on_reject = on_reject
        conn.send_message(msg_tx(tx))
        wait_until(lambda: len(rejected_txs) > 0, timeout=10)
        assert_equal(rejected_txs[0].reason, expected_reason)

    def assert_accepted_transaction(self, conn, coinbase, version=1):
        tx = self.create_non_pushonly_tx(coinbase, version)
        conn.send_message(msg_tx(tx))
        wait_until(lambda: tx.hash in conn.rpc.getrawmempool(), timeout=10)

    def run_test(self):
        common_args = ['-whitelist=127.0.0.1',
                       '-genesisactivationheight=%d' % self.genesisactivationheight,
                       '-chronicleactivationheight=%d' % self.chronicleactivationheight]

        # === Test with -acceptnonstdtxn=0 (node 0) ===
        args_nonstd0 = common_args + ['-acceptnonstdtxn=0']

        with self.run_node_with_connections("Test non-push-only rejection (nonstd=0)",
                                            0, args_nonstd0, number_of_connections=1) as (conn,):
            coinbases = []
            for _ in range(101):
                coinbases.append(make_new_block(conn))

            # tip is on height 101 (nHeight=102, pre-genesis): REJECT
            assert_equal(conn.rpc.getblock(conn.rpc.getbestblockhash())['height'], 101)
            self.assert_rejected_transaction(conn, coinbases[0])

            make_new_block(conn)

            # tip is on height 102 (nHeight=103, post-genesis): REJECT
            assert_equal(conn.rpc.getblock(conn.rpc.getbestblockhash())['height'], 102)
            self.assert_rejected_transaction(conn, coinbases[1])

            for _ in range(3):
                make_new_block(conn)

            # tip is on height 105 (nHeight=106, post-chronicle): ACCEPT with nVersion=2
            assert_equal(conn.rpc.getblock(conn.rpc.getbestblockhash())['height'], 105)
            self.assert_accepted_transaction(conn, coinbases[2], version=2)

            # post-chronicle: nVersion=1 still REJECTED (mandatory script verify, not IsStandard)
            self.assert_rejected_transaction(conn, coinbases[3],
                                             b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

        # === Test with -acceptnonstdtxn=1 (node 1) ===
        args_nonstd1 = common_args + ['-acceptnonstdtxn=1']

        with self.run_node_with_connections("Test non-push-only with nonstd=1",
                                            1, args_nonstd1, number_of_connections=1) as (conn,):
            coinbases = []
            for _ in range(101):
                coinbases.append(make_new_block(conn))

            # tip is on height 101 (nHeight=102, pre-genesis): ACCEPT
            assert_equal(conn.rpc.getblock(conn.rpc.getbestblockhash())['height'], 101)
            self.assert_accepted_transaction(conn, coinbases[0])

            make_new_block(conn)

            # tip is on height 102 (nHeight=103, post-genesis): REJECT (mandatory)
            assert_equal(conn.rpc.getblock(conn.rpc.getbestblockhash())['height'], 102)
            self.assert_rejected_transaction(conn, coinbases[1],
                                             b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')

            for _ in range(3):
                make_new_block(conn)

            # tip is on height 105 (nHeight=106, post-chronicle): ACCEPT with nVersion=2
            assert_equal(conn.rpc.getblock(conn.rpc.getbestblockhash())['height'], 105)
            self.assert_accepted_transaction(conn, coinbases[2], version=2)

            # post-chronicle: nVersion=1 still REJECTED (mandatory script verify)
            self.assert_rejected_transaction(conn, coinbases[3],
                                             b'mandatory-script-verify-flag-failed (Only non-push operators allowed in signatures)')


if __name__ == '__main__':
    GenesisChroniclePushOnlyNonStd().main()
