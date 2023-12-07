#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block
from test_framework.script import CScript, OP_DROP, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_mempool_equals

# Description:
# Creating a complex graph of transacions in the mempool, mining some of them and double-spend (in block) some other
# Checking if descedants of double-spend tx are removed while other stay in mempool


class Conflict(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def create_tx(self, outpoints, noutput):
        tx = CTransaction()
        total_input = 0
        for parent_tx, n in outpoints:
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, n), b"", 0xffffffff))
            total_input += parent_tx.vout[n].nValue

        for _ in range(noutput):
            tx.vout.append(CTxOut(total_input//noutput - 1000, CScript([b"X"*200, OP_DROP, OP_TRUE])))

        tx.rehash()
        return tx

    def run_test(self):
        with self.run_node_with_connections("Scenario 1: Create complex graph of txs, doublespend some fo them and check mempool after"
                                            , 0, ["-minrelaytxfee=0"],
                                            number_of_connections=1) as (conn,):

            # create block with coinbase
            coinbase = create_coinbase(height=1)
            first_block = create_block(int(conn.rpc.getbestblockhash(), 16), coinbase=coinbase)
            first_block.solve()
            conn.send_message(msg_block(first_block))
            wait_until(lambda: conn.rpc.getbestblockhash() == first_block.hash, check_interval=0.3)

            #mature the coinbase
            conn.rpc.generate(150)

            funding_tx = self.create_tx([(coinbase, 0)], 2)

            conn.send_message(msg_tx(funding_tx))
            check_mempool_equals(conn.rpc, [funding_tx])
            conn.rpc.generate(1)

            last_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
            block = create_block(int(last_block_info["hash"], 16), coinbase=create_coinbase(height=last_block_info["height"] + 1), nTime=last_block_info["time"] + 1)
            low_fee_tx = self.create_tx([(funding_tx, 0)], 2)
            block.vtx.append(low_fee_tx)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.calc_sha256()
            block.solve()

            conn.send_message(msg_block(block))
            wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, check_interval=0.3)

            #   tx_double_spend_mempool            tx_to_be_mined
            #          |      |                      |        |
            #     -------------------------------------------------------
            #          |      |                      |        |
            #          |      +-------+     +--------+        |
            #          |              |     |                 |
            #          |     tx_descedant_of_conflict_1   tx_stay_in_mempool_1
            #          |                 |                    |        |
            #          |                 |                    |        |
            #          |        tx_descedant_of_conflict_2    |    tx_stay_in_mempool_2
            #          |                          |           |               |
            #          |                          |           |               |
            #          |                      tx_descedant_of_conflict_3      |
            #          |                                                      |
            #          +---------+           +--------------------------------+
            #                    |           |
            #                tx_descedant_of_conflict_4

            tx_double_spend_mempool = self.create_tx([(low_fee_tx,                 0)], 2)
            tx_double_spend_block = self.create_tx([(low_fee_tx,                 0)], 1)
            tx_to_be_mined = self.create_tx([(low_fee_tx,                 1)], 2)
            tx_descedant_of_conflict_1 = self.create_tx([(tx_double_spend_mempool,    0),
                                                         (tx_to_be_mined,             0)], 1)
            tx_descedant_of_conflict_2 = self.create_tx([(tx_descedant_of_conflict_1, 0)], 1)
            tx_stay_in_mempool_1 = self.create_tx([(tx_to_be_mined,             1)], 2)
            tx_descedant_of_conflict_3 = self.create_tx([(tx_descedant_of_conflict_2, 0),
                                                         (tx_stay_in_mempool_1,       0)], 2)
            tx_stay_in_mempool_2 = self.create_tx([(tx_stay_in_mempool_1,       1)], 1)
            tx_descedant_of_conflict_4 = self.create_tx([(tx_double_spend_mempool,    1),
                                                         (tx_stay_in_mempool_2,       0)], 1)

            conn.send_message(msg_tx(tx_double_spend_mempool))
            conn.send_message(msg_tx(tx_to_be_mined))
            conn.send_message(msg_tx(tx_descedant_of_conflict_1))
            conn.send_message(msg_tx(tx_descedant_of_conflict_2))
            conn.send_message(msg_tx(tx_stay_in_mempool_1))
            conn.send_message(msg_tx(tx_descedant_of_conflict_3))
            conn.send_message(msg_tx(tx_stay_in_mempool_2))
            conn.send_message(msg_tx(tx_descedant_of_conflict_4))

            check_mempool_equals(conn.rpc, [tx_double_spend_mempool,
                                            tx_to_be_mined,
                                            tx_descedant_of_conflict_1,
                                            tx_descedant_of_conflict_2,
                                            tx_stay_in_mempool_1,
                                            tx_descedant_of_conflict_3,
                                            tx_stay_in_mempool_2,
                                            tx_descedant_of_conflict_4])

            block2 = create_block(block.sha256, coinbase=create_coinbase(height=last_block_info["height"] + 2), nTime=last_block_info["time"] + 2)
            block2.vtx.append(tx_double_spend_block)
            block2.vtx.append(tx_to_be_mined)
            block2.hashMerkleRoot = block2.calc_merkle_root()
            block2.calc_sha256()
            block2.solve()

            conn.send_message(msg_block(block2))
            wait_until(lambda: conn.rpc.getbestblockhash() == block2.hash, check_interval=0.3)

            check_mempool_equals(conn.rpc, [tx_stay_in_mempool_1, tx_stay_in_mempool_2])


if __name__ == '__main__':
    Conflict().main()
