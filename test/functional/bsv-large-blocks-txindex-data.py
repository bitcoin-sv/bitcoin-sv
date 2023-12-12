#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test support for files that have transaction on position greater than 4GB from
the block header inside a block.

Scenario:
1. Enable -txindex
2. Send enough transactions to have some of them inside the next block that
   start at position greater than 4GB from next block header
3. Mine block (should correctly write CDiskTxPos for all transactions)
4. Read back the raw transactions (should correctly read CDiskTxPos of each
   transaction)

"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_RETURN, OP_TRUE, OP_NOP, OP_FALSE
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal, p2p_port
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.mininode import msg_tx, CTransaction, FromHex
from test_framework.cdefs import ONE_GIGABYTE, ONE_MEGABYTE
from test_framework.util import get_rpc_proxy, wait_until, sync_blocks
from time import sleep


class BlockFileStore(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 104
        self.nocleanup = True
        self.extra_args = [
            [
                '-whitelist=127.0.0.1',
                '-excessiveblocksize=%d' % (ONE_GIGABYTE * 6),
                '-blockmaxsize=%d' % (ONE_GIGABYTE * 6),
                '-maxmempool=%d' % (ONE_GIGABYTE * 10),
                '-maxmempoolsizedisk=0',
                '-maxtxsizepolicy=%d' % ONE_GIGABYTE,
                '-maxscriptsizepolicy=0',
                '-maxstdtxvalidationduration=55000',
                '-maxnonstdtxvalidationduration=55001',
                '-maxtxnvalidatorasynctasksrunduration=55002',
                '-rpcservertimeout=1000',
                '-genesisactivationheight=%d' % self.genesisactivationheight,
                "-txindex"
            ]
        ]

    def run_test(self):
        self.test.run()

    def get_tests(self):

        # shorthand for functions
        block = self.chain.next_block
        node = get_rpc_proxy(self.nodes[0].url, 1, timeout=6000, coveragedir=self.nodes[0].coverage_dir)

        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 200, 200)

        yield test

        txHashes = []
        for i in range(18):
            txLarge = create_transaction(out[i].tx, out[i].n, b"", ONE_MEGABYTE * 256, CScript([OP_FALSE, OP_RETURN, bytearray([42] * (ONE_MEGABYTE * 256))]))
            self.test.connections[0].send_message(msg_tx(txLarge))
            self.check_mempool(node, [txLarge], timeout=6000)
            txHashes.append([txLarge.hash, txLarge.sha256])

        txOverflow = create_transaction(out[18].tx, out[18].n, b"", ONE_MEGABYTE * 305, CScript([OP_FALSE, OP_RETURN, bytearray([42] * (ONE_MEGABYTE * 305))]))
        self.test.connections[0].send_message(msg_tx(txOverflow))
        self.check_mempool(node, [txOverflow], timeout=6000)
        txHashes.append([txOverflow.hash, txOverflow.sha256])

        txOverflow = create_transaction(out[19].tx, out[19].n, b"", ONE_MEGABYTE, CScript([OP_FALSE, OP_RETURN, bytearray([42] * ONE_MEGABYTE)]))
        self.test.connections[0].send_message(msg_tx(txOverflow))
        self.check_mempool(node, [txOverflow], timeout=6000)
        txHashes.append([txOverflow.hash, txOverflow.sha256])

        # Mine block with new transactions.
        self.log.info("BLOCK 2 - mining")
        minedBlock2 = node.generate(1)
        self.log.info("BLOCK 2 - mined")

        for txHash in txHashes:
            tx = FromHex(CTransaction(), self.nodes[0].getrawtransaction(txHash[0]))
            tx.rehash()
            assert_equal(tx.sha256, txHash[1])


if __name__ == '__main__':
    BlockFileStore().main()
