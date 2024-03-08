#!/usr/bin/env python3
# Copyright (c) 2018-2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check that orphan transaction with max allowed size is accepted.
Check that the getorphaninfo rpc is working.
"""

from test_framework.blocktools import create_transaction, create_coinbase, create_block
from test_framework.mininode import msg_tx, msg_block
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_equal, check_mempool_equals
from test_framework.cdefs import DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS

import time


def make_new_block(connection):
    "Create and send block with coinbase, returns conbase (tx, key) tuple"
    tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
    coinbase_tx = create_coinbase(tip["height"] + 1)
    coinbase_tx.rehash()
    block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
    block.solve()
    connection.send_message(msg_block(block))
    wait_until(lambda: connection.rpc.getbestblockhash() == block.hash, timeout=10)
    return coinbase_tx


def make_big_orphan(tx_parent, size_bytes):
    add_bytes = size_bytes
    diff = size_bytes
    while diff != 0:
        tx_child = create_transaction(tx_parent, 0, CScript(), tx_parent.vout[0].nValue - 2 * size_bytes, CScript([OP_TRUE] + [bytes(1) * add_bytes]))
        tx_child.rehash()
        diff = size_bytes - len(tx_child.serialize())
        add_bytes += diff
    return tx_child


class TestMaxSizedOrphan(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        with self.run_node_with_connections("Scenario 1", 0, ['-banscore=100000', '-genesisactivationheight=110', '-maxstdtxvalidationduration=100'],
                                            number_of_connections=1) as (conn,):

            coinbase1 = make_new_block(conn)

            for _ in range(110):
                make_new_block(conn)

            tx_parent = create_transaction(coinbase1, 0, CScript(), coinbase1.vout[0].nValue - 1000, CScript([OP_TRUE]))
            tx_parent.rehash()
            tx_orphan = make_big_orphan(tx_parent, DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS)
            assert_equal(len(tx_orphan.serialize()), DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS)

            before = conn.rpc.getorphaninfo()["size"]
            conn.send_message(msg_tx(tx_orphan))
            # Making sure parent is not sent right away for bitcond to detect an orphan
            wait_until(lambda: conn.rpc.getorphaninfo()["size"]>before, timeout=2)

            after = conn.rpc.getorphaninfo()["size"]
            assert_equal(before + 1, after)

            conn.send_message(msg_tx(tx_parent))
            check_mempool_equals(conn.rpc, [tx_parent, tx_orphan])


if __name__ == '__main__':
    TestMaxSizedOrphan().main()
