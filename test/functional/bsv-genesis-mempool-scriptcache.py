#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# 1. Genesis height is 104. Current height is 102.
# 2. Send tx1 and tx2 that are valid before Genesis (multiple ELSEs). They are accepted to mempool. (tx2: SCRIPT_GENESIS flag is off)
# 3. Generate an empty block. Height is 103. Mempool is cleared.
# 4. Send tx1 and tx2 again. Tx2 should not be accepted to mempool (Genesis rules).
# 5. Send tx1 and tx2 again in block. Block should be rejected. (tx2: SCRIPT_GENESIS flag is on)
###
# 6. Current height is 103.
# 7. Send tx3 and tx4 that are valid after Genesis (disabled opcodes in unexecuted branches). They are accepted to mempool. (tx4: SCRIPT_GENESIS flag is on)
# 8. Invalidate block 103. Mempool is cleared.
# 9. Send tx3 again and generate new block with tx3 in it (height is now 103).
# 10. Send tx4 again. It should not be accepted to mempool because tx3 (UTXO) is pre-Genesis.
# 11. Send tx4 in block. Block should be rejected. (tx4: SCRIPT_GENESIS flag is on)
###
# 12. Current height is 103.
# 13. Generate new block with tx5 and tx6 that are valid after genesis. It is on genesis height (104).
# 14. Invalidate block on height 104. tx5 and tx6 are in mempool.
# 15. Invalidate block on height 103. tx5 and tx6 are deleted from mempool.

from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import *
from test_framework.blocktools import create_transaction, create_block, create_coinbase, prepare_init_chain
from test_framework.util import assert_equal, hashToHex, wait_until
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx, msg_block, mininode_lock
from time import sleep


def add_tx_to_block(block, txs):
    block.vtx.extend(txs)
    block.hashMerkleRoot = block.calc_merkle_root()
    block.calc_sha256()
    block.solve()


class BSVGenesisMempoolScriptCache(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 104
        self.extra_args = [['-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight]]

    def run_test(self):
        self.test.run()

    def get_tests(self):
        # shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 101, 100)

        yield test

        ########## SCENARIO 1

        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 2)

        # Create and send tx1 and tx2 that are valid before genesis.
        tx1 = create_transaction(out[0].tx, out[0].n, b'', 100000, CScript([OP_IF, OP_0, OP_ELSE, OP_1, OP_ELSE, OP_2, OP_ENDIF]))
        tx2 = create_transaction(tx1, 0, CScript([OP_0]), 1, CScript([OP_TRUE]))

        self.test.connections[0].send_message(msg_tx(tx1))
        self.test.connections[0].send_message(msg_tx(tx2))
        # wait for transactions to be accepted.
        wait_until(lambda: len(node.getrawmempool()) == 2, timeout=5)
        assert_equal(True, tx1.hash in node.getrawmempool())
        assert_equal(True, tx2.hash in node.getrawmempool())

        # Generate an empty block, height is then 103 and mempool is cleared.
        block103 = block(1, spend=out[1])
        yield self.accepted()

        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 1)
        assert_equal(len(node.getrawmempool()), 0)

        # Send transactions tx1 and tx2 once again, this time with Genesis rules (mempool height is 104).
        self.test.connections[0].send_message(msg_tx(tx1))
        self.test.connections[0].send_message(msg_tx(tx2))
        # wait for transaction processing
        # Tx2 should not be valid anymore.
        wait_until(lambda: len(node.getrawmempool()) == 1, timeout=5)
        assert_equal(True, tx1.hash in node.getrawmempool())
        assert_equal(False, tx2.hash in node.getrawmempool())

        # Now send tx1 and tx2 again, but this time in block. Block should be rejected.
        block = create_block(int("0x" + node.getbestblockhash(), 16), create_coinbase(height=1, outputValue=25))
        add_tx_to_block(block, [tx1,tx2])

        rejected_blocks = []
        rejected_txs = []

        def on_reject(conn, msg):
            if (msg.message == b'block'):
                rejected_blocks.append(msg)
                assert(msg.reason.startswith(b'blk-bad-inputs'))

            if msg.message == b'tx':
                rejected_txs.append(msg)

        self.test.connections[0].cb.on_reject = on_reject
        self.test.connections[0].send_message(msg_block(block))

        wait_until(lambda: len(rejected_blocks) > 0, timeout=5, lock=mininode_lock)
        assert_equal(rejected_blocks[0].data, block.sha256)
        assert_equal(False, block.hash == node.getbestblockhash())

        ########## SCENARIO 2

        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 1)

        # Create and send tx3 and tx4 that are valid after genesis.
        tx3 = create_transaction(out[2].tx, out[2].n, b'', 100000, CScript([OP_IF, OP_2, OP_2MUL, OP_ENDIF, OP_1]))
        tx4 = create_transaction(tx3, 0, CScript([OP_0]), 1, CScript([OP_TRUE]))

        self.test.connections[0].send_message(msg_tx(tx3))
        self.test.connections[0].send_message(msg_tx(tx4))
        # wait for transaction in mempool
        wait_until(lambda: tx3.hash in node.getrawmempool(), timeout=5)
        wait_until(lambda: tx4.hash in node.getrawmempool(), timeout=5)

        # Invalidate block -->  we are then at state before Genesis. Mempool is cleared.
        node.invalidateblock(hashToHex(block103.sha256))
        assert_equal(False, tx3.hash in node.getrawmempool())
        assert_equal(False, tx4.hash in node.getrawmempool())

        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 2)

        # Send tx3 again, this time in pre-genesis rules. It is accepted to mempool.
        self.test.connections[0].send_message(msg_tx(tx3))

        wait_until(lambda: tx3.hash in node.getrawmempool(), timeout=5)

        # Generate a block (height 103) with tx3 in it.
        node.generate(1)
        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 1)
        assert_equal(True, tx3.hash in node.getblock(node.getbestblockhash())['tx'])

        # Send tx4 again, again with Genesis rules. It should not be accepted to mempool.
        self.test.connections[0].send_message(msg_tx(tx4))

        wait_until(lambda: tx4.sha256 in [msg.data for msg in rejected_txs], timeout=5, lock=mininode_lock)
        assert_equal(len(node.getrawmempool()), 0)

        # Send tx4 again, this time in block. Block should be rejected.
        block = create_block(int("0x" + node.getbestblockhash(), 16), create_coinbase(height=1, outputValue=25))
        add_tx_to_block(block, [tx4])

        self.test.connections[0].send_message(msg_block(block))

        # Wait for hash in rejected blocks
        wait_until(lambda: block.sha256 in [msg.data for msg in rejected_blocks], timeout=5, lock=mininode_lock)
        assert_equal(False, block.hash == node.getbestblockhash())

        ########## SCENARIO 3

        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 1)

        # Generate a block (height 104) with tx5 and tx6 (valid after genesis).
        tx5 = create_transaction(out[3].tx, out[3].n, b'', 100000, CScript([OP_IF, OP_2, OP_2MUL, OP_ENDIF, OP_1]))
        tx6 = create_transaction(tx5, 0, CScript([OP_0]), 1, CScript([OP_TRUE]))
        blockGenesis = create_block(int("0x" + node.getbestblockhash(), 16), create_coinbase(height=1, outputValue=25))
        add_tx_to_block(blockGenesis, [tx5, tx6])
        self.test.connections[0].send_message(msg_block(blockGenesis))

        wait_until(lambda: blockGenesis.hash == node.getbestblockhash(), timeout=5)
        assert_equal(True, tx5.hash in node.getblock(node.getbestblockhash())['tx'])
        assert_equal(True, tx6.hash in node.getblock(node.getbestblockhash())['tx'])

        # Invalidate block 104. tx5 and tx6 are in now in mempool.
        node.invalidateblock(hashToHex(blockGenesis.sha256))
        assert_equal(True, tx5.hash in node.getrawmempool())
        assert_equal(True, tx6.hash in node.getrawmempool())

        assert_equal(node.getblock(node.getbestblockhash())['height'], self.genesisactivationheight - 1)

        # Invalidate block 103. tx5 and tx6 are not in mempool anymore.
        node.invalidateblock(node.getbestblockhash())
        assert_equal(False, tx5.hash in node.getrawmempool())
        assert_equal(False, tx6.hash in node.getrawmempool())


if __name__ == '__main__':
    BSVGenesisMempoolScriptCache().main()
