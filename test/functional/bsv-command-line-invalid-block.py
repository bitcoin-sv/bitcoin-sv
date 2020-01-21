#!/usr/bin/env python3
# Copyright (c) 2019  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from time import sleep
import socket

from test_framework.blocktools import create_block, create_coinbase
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, ToHex
from test_framework.script import CScript, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG, OP_ADD
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_raises_rpc_error


class InvalidateBlock(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def make_block(self, connection):
        # coinbase_pubkey = self._current_test.COINBASE_KEY.get_pubkey() if self._current_test.COINBASE_KEY else None
        tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
        coinbase_tx = create_coinbase(tip["height"] + 1)
        coinbase_tx.rehash()

        block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)

        block.solve()
        return block

    def confirm_tip(self, conn, hash):
        wait_until(lambda: conn.rpc.getbestblockhash() == hash, timeout=10, check_interval=0.2,
                   label=f"waiting until {hash} become tip")

    def run_test(self):

        with self.run_node_with_connections("Preparation", 0, [], 1) as (conn,):
            generated_blocks = conn.rpc.generate(100)

        with self.run_node_with_connections("Invalidate tip", 0, [f"-invalidateblock={generated_blocks[-1]}"], 1) as (conn,):
            self.confirm_tip(conn, generated_blocks[-2])
            conn.rpc.reconsiderblock(generated_blocks[-1])
            self.confirm_tip(conn, generated_blocks[-1])

        with self.run_node_with_connections("Invalidate two blocks not at tip", 0, [f"-invalidateblock={generated_blocks[-4]}", f"-invalidateblock={generated_blocks[-3]}"], 1) as (conn,):
            self.confirm_tip(conn, generated_blocks[-5])

        with self.run_node_with_connections("Reconsider block invalidated in previous run.", 0, [], 1) as (conn,):
            self.confirm_tip(conn, generated_blocks[-5])
            conn.rpc.reconsiderblock(generated_blocks[-3])
            self.confirm_tip(conn, generated_blocks[-1])

        with self.run_node_with_connections("Invalidate block deep in the chain", 0, [f"-invalidateblock={generated_blocks[-90]}", "-reindex=1"], 1) as (conn,):
            self.confirm_tip(conn, generated_blocks[-91])

        with self.run_node_with_connections("Try to Invalidate non-existing block", 0, [f"-invalidateblock=1000000000000000000000000000000000000000000000000000000000000000"], 1) as (conn,):
            self.confirm_tip(conn, generated_blocks[-91])
            next_block = self.make_block(conn)

        with self.run_node_with_connections("Try to invalidate block that is not received yet.", 0, [f"-invalidateblock={next_block.hash}"], 1) as (conn,):
            conn.send_message(msg_block(next_block))
            self.confirm_tip(conn, generated_blocks[-91])
            assert_raises_rpc_error(-5, 'Block not found', conn.rpc.reconsiderblock, next_block.hash)

        with self.run_node_with_connections("Proof that the 'next_block' would be accepted.", 0, [], 1) as (conn,):
            conn.send_message(msg_block(next_block))
            self.confirm_tip(conn, next_block.hash)


if __name__ == '__main__':
    InvalidateBlock().main()
