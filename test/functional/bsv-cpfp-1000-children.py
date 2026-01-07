#!/usr/bin/env python3
# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test scenario with CPFP group, plus over 1000 additional secondary
mempool txns.

Parent Txn
  |- A child not paying enough for itself (or parent).
  |- 1000+ child txns paying enough each, but none pay enough for parent.
     |- A grandchild txn paying enough for itself and grandparent.

1) Send parent and children. All will be in secondary mempool.

2) Send grandchild. A CPFP group will be formed containing the parent,
   child and grandchild. That CPFP group plus a further 998 children
   will move to primary mempool. (The limit of 998 is due to the mempool
   limit of 1000 txs to be reprocessed in 1 go)

3) Mine a block. All the remaining secondary mempool txns, except the
   one that still doesn't pay enough, will be moved to primary mempool.
"""

from test_framework.blocktools import ChainManager
from test_framework.mininode import CTransaction, CTxIn, CTxOut, COutPoint, msg_tx, msg_block
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, wait_until


class Cpfp1000children(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        args = ['-genesisactivationheight=1',
                '-maxmempool=10000',
                '-maxnonstdtxvalidationduration=100000',
                '-maxtxnvalidatorasynctasksrunduration=200000',
                '-minminingtxfee=0.00000001']

        with self.run_node_with_connections("Test CPFP group 1000+ unconfirmed txs", 0, args, 1) as (conn,):

            def create_tx(ptx, n, value=1, num_outs=1):
                tx = CTransaction()
                tx.vin.append(CTxIn(COutPoint(ptx.sha256, n), b"", 0xffffffff))
                for i in range(num_outs):
                    tx.vout.append(CTxOut(value, CScript([OP_TRUE])))
                tx.rehash()
                return tx

            chain = ChainManager()
            chain.set_genesis_hash(int(conn.rpc.getbestblockhash(), 16))

            for i in range(101):
                block = chain.next_block(i + 1)
                chain.save_spendable_output()
                conn.send_message(msg_block(block))

            wait_until(lambda: conn.rpc.getbestblockhash() == block.hash)

            # Parent txn with 2500 outputs and 0 fee
            num_outputs = 2500
            out = chain.get_spendable_output()
            total_amount = out.tx.vout[0].nValue
            no_fee_output_amount = int(total_amount / num_outputs)
            tx1 = create_tx(out.tx, 0, no_fee_output_amount, num_outputs)
            conn.send_message(msg_tx(tx1))
            wait_until(lambda: tx1.hash in conn.rpc.getrawmempool())

            # Parent txn is in secondary mempool
            info = conn.rpc.getmempoolinfo()
            assert_equal(info['size'], 1)
            assert_equal(info['journalsize'], 0)

            # Total fee from all children pays enough for the parent, but no child on its own pays enough
            num_child_txns = 2200
            extra_fee_required = 3000
            child_fee = (extra_fee_required / num_child_txns)
            child_output_amount = int(no_fee_output_amount - child_fee)

            # Send bulk of children
            child_hashs = []
            for t in range(num_child_txns):
                tx2 = create_tx(tx1, t, child_output_amount)
                child_hashs.append(tx2.hash)
                conn.send_message(msg_tx(tx2))

            # And send a child that doesn't even pay enough for itself (0 fee)
            no_fee_child = create_tx(tx1, t + 1, tx1.vout[t + 1].nValue)
            child_hashs.append(no_fee_child.hash)
            conn.send_message(msg_tx(no_fee_child))

            # Wait for all txns to arrive in mempool
            def all_in_mempool(child_hashs, raw_mempool):
                return all([ch in raw_mempool for ch in child_hashs])
            wait_until(lambda: all_in_mempool(child_hashs, conn.rpc.getrawmempool()))

            # All txs are currently in secondary mempool
            all_txs = num_child_txns + 2  # parent tx & non-paying child
            info = conn.rpc.getmempoolinfo()
            assert_equal(info['size'], all_txs)
            assert_equal(info['journalsize'], 0)

            # Final grandchild txn pays enough for itself and grandparent
            tx3 = create_tx(tx2, 0, child_output_amount - extra_fee_required)
            conn.send_message(msg_tx(tx3))
            wait_until(lambda: tx3.hash in conn.rpc.getrawmempool())

            # Parent tx, child and grandchild tx, +998 paying children are moved to primary mempool
            all_txs += 1  # grandchild tx
            info = conn.rpc.getmempoolinfo()
            assert_equal(info['size'], all_txs)
            assert_equal(info['journalsize'], 1001)

            # Mine a block
            conn.rpc.generate(1)

            # Make sure cpfp group is no longer in mempool (they were all mined)
            raw_mempool = conn.rpc.getrawmempool()
            assert tx1.hash not in raw_mempool
            assert tx2.hash not in raw_mempool
            assert tx3.hash not in raw_mempool

            # Txs in primary mempool were mined.
            # All remaining fee paying transactions from secondary mempool are moved to primary.
            # The single non-paying child is left in secondary mempool.
            all_txs -= 1001  # mined in a block
            info = conn.rpc.getmempoolinfo()
            assert_equal(info['size'], all_txs)
            assert_equal(info['journalsize'], all_txs - 1)

            # All fee paying txs are mined, single non-paying tx is left in secondary mempool
            conn.rpc.generate(1)
            info = conn.rpc.getmempoolinfo()
            assert_equal(info['size'], 1)
            assert_equal(info['journalsize'], 0)
            assert no_fee_child.hash in conn.rpc.getrawmempool()


if __name__ == '__main__':
    Cpfp1000children().main()
