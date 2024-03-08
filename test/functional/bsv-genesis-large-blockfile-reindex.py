#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test reindex of large (>4GB) files.
Scenario:
1. Generate block with 120 MB transaction.
2. Generate block with 4x1GB transaction + 305MB transaction that would overflow site if it was 32 bit. This block should be written in new file.
4. Generate block with 1MB transaction that goes into third block file.
5. Restart node with reindex option
6. Check if block count is correct after reindex
"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_RETURN, OP_TRUE, OP_NOP, OP_FALSE
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal, assert_greater_than, p2p_port
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.mininode import msg_tx
from test_framework.cdefs import ONE_GIGABYTE, ONE_MEGABYTE
from test_framework.util import get_rpc_proxy, wait_until, sync_blocks
from time import sleep


class LargeBlockFileReindex(ComparisonTestFramework):

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
                '-maxstdtxvalidationduration=15000',
                '-maxnonstdtxvalidationduration=15001',
                '-maxtxnvalidatorasynctasksrunduration=15002',
                '-rpcservertimeout=1000',
                '-genesisactivationheight=%d' % self.genesisactivationheight
            ]
        ]

    def run_test(self):
        self.test.run()

    def get_tests(self):

        # Shorthand for functions
        block = self.chain.next_block

        # Get proxy with bigger timeout
        node = get_rpc_proxy(self.nodes[0].url, 1, timeout=6000, coveragedir=self.nodes[0].coverage_dir)

        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 200, 200)

        yield test

        # Create transaction that will almost fill block file when next block will be generated (~130 MB)
        tx1 = create_transaction(out[0].tx, out[0].n, b"", ONE_MEGABYTE * 120,  CScript([OP_FALSE, OP_RETURN, bytearray([42] * (ONE_MEGABYTE * 120))]))
        self.test.connections[0].send_message(msg_tx(tx1))
        # Wait for transaction processing
        self.check_mempool(node, [tx1], timeout=6000)
        # Mine block with new transaction.
        minedBlock1 = node.generate(1)

        # Send 4 large (~1GB) transactions that will go into next block
        for i in range(4):
            txLarge = create_transaction(out[1 + i].tx, out[1 + i].n, b"", ONE_GIGABYTE, CScript([OP_FALSE, OP_RETURN, bytearray([42] * (ONE_GIGABYTE - ONE_MEGABYTE))]))
            self.test.connections[0].send_message(msg_tx(txLarge))
            self.check_mempool(node, [txLarge], timeout=6000)
        # Send transaction with size that will overflow 32 bit size
        txOverflow = create_transaction(out[5].tx, out[5].n, b"", ONE_MEGABYTE * 305, CScript([OP_FALSE, OP_RETURN, bytearray([42] * (ONE_MEGABYTE * 305))]))
        self.test.connections[0].send_message(msg_tx(txOverflow))
        self.check_mempool(node, [txOverflow], timeout=6000)
        # Mine block with new transactions with size > 4GB. This will write to new block file on disk.
        minedBlock2 = node.generate(1)
        # Make sure that block is larger than 32 bit max
        assert_greater_than(node.getblock(minedBlock2[0], True)['size'], 2**32)

        # Generate another transaction for next block
        tx2 = create_transaction(out[10].tx, out[10].n, b"", ONE_MEGABYTE, CScript([OP_FALSE, OP_RETURN, bytearray([42] * (ONE_MEGABYTE))]))
        self.test.connections[0].send_message(msg_tx(tx2))
        self.check_mempool(node, [tx2], timeout=6000)
        # Mine block with new transactions. This will write to new block file on disk.
        minedBlock3 = node.generate(1)

        # Get block count
        blockcount = node.getblockcount()

        # Restart node with reindex option
        self.stop_nodes()
        self.extra_args[0].append("-reindex")
        self.start_nodes(self.extra_args)

        # Get proxy with bigger timeout
        node = get_rpc_proxy(self.nodes[0].url, 1, timeout=6000, coveragedir=self.nodes[0].coverage_dir)

        # Get block count after reindex and compare it to block count before node shutdown - should be equal
        while node.getblockcount() < blockcount:
            sleep(0.1)
        assert_equal(node.getblockcount(), blockcount)


if __name__ == '__main__':
    LargeBlockFileReindex().main()
