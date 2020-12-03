#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, COIN
from test_framework.script import CScript, OP_DROP, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_mempool_equals, assert_equal


class MemepoolAncestorsLimits(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def create_tx(self, outpoints, noutput, feerate):
        tx = CTransaction()
        total_input = 0
        for parent_tx, n in outpoints:
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, n), b"", 0xffffffff))
            total_input += parent_tx.vout[n].nValue

        for _ in range(noutput):
            tx.vout.append(CTxOut(total_input//noutput, CScript([b"X"*200, OP_DROP, OP_TRUE])))

        tx.rehash()

        tx_size = len(tx.serialize())
        fee_per_output = int(tx_size * feerate // noutput)

        for output in tx.vout:
            output.nValue -= fee_per_output

        tx.rehash()
        return tx

    def run_test(self):

        limitancestorcount = 20
        limitcpfpgroupmemberscount = 10

        with self.run_node_with_connections("Tests for ancestors count limit, for primary and secondary mempool", 0,
                                            ["-blockmintxfee=0.00001",
                                             f"-limitancestorcount={limitancestorcount}",
                                             f"-limitcpfpgroupmemberscount={limitcpfpgroupmemberscount}",
                                             "-checkmempool=1",],
                                            number_of_connections=1) as (conn,):

            mining_fee = 1.01 # in satoshi per byte
            relayfee = float(conn.rpc.getnetworkinfo()["relayfee"] * COIN / 1000) + 0.01  # in satoshi per byte

            # create block with coinbase
            coinbase = create_coinbase(height=1)
            first_block = create_block(int(conn.rpc.getbestblockhash(), 16), coinbase=coinbase)
            first_block.solve()
            conn.send_message(msg_block(first_block))
            wait_until(lambda: conn.rpc.getbestblockhash() == first_block.hash, check_interval=0.3)

            #mature the coinbase
            conn.rpc.generate(150)


            funding_tx = self.create_tx([(coinbase, 0)], 2, 1.5)

            conn.send_message(msg_tx(funding_tx))
            check_mempool_equals(conn.rpc, [funding_tx])
            conn.rpc.generate(1)

            rejected_txs = []
            def on_reject(conn, msg):
                rejected_txs.append(msg)
            conn.cb.on_reject = on_reject

            # create oversized primary mempool chain, the last tx in the chain will be over the limit
            last_outpoint = (funding_tx, 0)
            primary_mempool_chain = []
            for _ in range(limitancestorcount + 1):
                tx = self.create_tx([last_outpoint], 1, mining_fee)
                primary_mempool_chain.append(tx)
                last_outpoint = (tx, 0)

            # create oversized secondary mempool chain, the last tx in the chain will be over the limit
            last_outpoint = (funding_tx, 1)
            secondary_mempool_chain = []
            for _ in range(limitcpfpgroupmemberscount + 1):
                tx = self.create_tx([last_outpoint], 1, relayfee)
                secondary_mempool_chain.append(tx)
                last_outpoint = (tx, 0)

            # send transactions to the node
            for tx in primary_mempool_chain[:-1]:
                conn.send_message(msg_tx(tx))

            for tx in secondary_mempool_chain[:-1]:
                conn.send_message(msg_tx(tx))


            # all transactions that are sent should en up in the mempool, chains are at the limit
            check_mempool_equals(conn.rpc, primary_mempool_chain[:-1] + secondary_mempool_chain[:-1])


            # now send transactions that try to extend chain over the limit, should be rejected
            for tx_to_reject in [primary_mempool_chain[-1], secondary_mempool_chain[-1]]:
                conn.send_message(msg_tx(tx_to_reject))
                wait_until(lambda: len(rejected_txs) == 1)
                assert_equal(rejected_txs[0].data, tx_to_reject.sha256)
                assert_equal(rejected_txs[0].reason, b'too-long-mempool-chain')
                rejected_txs.clear()


            # lets mine transactions from beggining of the chain, this will shorten the chains
            last_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
            block = create_block(int(last_block_info["hash"], 16), coinbase=create_coinbase(height=last_block_info["height"] + 1), nTime=last_block_info["time"] + 1)
            block.vtx.append(primary_mempool_chain[0])
            block.vtx.append(secondary_mempool_chain[0])
            block.hashMerkleRoot = block.calc_merkle_root()
            block.calc_sha256()
            block.solve()

            conn.send_message(msg_block(block))
            wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, check_interval=0.3)

            # try to send transactions again, now chains are shorter and transactions will be accepted
            for tx_to_reject in [primary_mempool_chain[-1], secondary_mempool_chain[-1]]:
                conn.send_message(msg_tx(tx_to_reject))
            check_mempool_equals(conn.rpc, primary_mempool_chain[1:] + secondary_mempool_chain[1:])

            # invalidate the block, this will force mined transactions back to mempool
            # as we do not check chain lenght after reorg we will end up wit long chains in the mempool
            conn.rpc.invalidateblock(block.hash)
            check_mempool_equals(conn.rpc, primary_mempool_chain + secondary_mempool_chain)

if __name__ == '__main__':
    MemepoolAncestorsLimits().main()
