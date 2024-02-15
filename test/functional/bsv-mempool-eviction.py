#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from math import ceil

from test_framework.blocktools import create_coinbase, create_block
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.mininode import CTransaction, CTxIn, COutPoint, CTxOut, msg_block, msg_tx
from test_framework.script import CScript, OP_DROP, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_mempool_equals

# In this test we are testing eviction order
# Steps:
# 1. Send transactions that we will be evicted later
# 2. Fill the mempool up to maximum capacity with high-paying txs (capacity: 300MB, fits 299 txs of 1MB, overall memory
#    usage of the single transaction is bigger because of metadata associated with txs in the mempool)
# 3. Send high paying transaction (one by one) and check which tx are evicted

# For Release build with sanitizers enabled (TSAN / ASAN / UBSAN), recommended timeoutfactor is 1.
# For Debug build, recommended timeoutfactor is 3.
# For Debug build with sanitizers enabled, recommended timeoutfactor is 5.


class Evictions(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def tx_size(self, tx):
        return len(tx.serialize())

    def create_tx(self, outpoints, noutput, feerate, totalSize = 0, size_of_nonpayin_txs=0):
        tx = CTransaction()
        total_input = 0
        for parent_tx, n in outpoints:
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, n), b"", 0xffffffff))
            total_input += parent_tx.vout[n].nValue

        for _ in range(noutput):
            tx.vout.append(CTxOut(total_input//noutput, CScript([OP_TRUE])))

        if totalSize:
            tx.rehash()
            missingSize = totalSize - self.tx_size(tx)
            assert missingSize >= 0
            tx.vout[0].scriptPubKey = CScript([b"X"*(missingSize - 10), OP_DROP, OP_TRUE])

        tx.rehash()
        overall_size = self.tx_size(tx) + size_of_nonpayin_txs
        fee_per_output = ceil(overall_size * feerate / noutput)

        for output in tx.vout:
            output.nValue -= fee_per_output

        tx.rehash()
        return tx

    def run_test(self):

        with self.run_node_with_connections("Eviction order test; fill the mempool over its size and see what txs will be evicted.",
                                            0, ["-minminingtxfee=0.00001", # 1 satoshi/byte
                                                "-minrelaytxfee=0",
                                                "-maxmempool=300MB",
                                                "-maxmempoolsizedisk=0",
                                                "-genesisactivationheight=1",
                                                '-maxstdtxvalidationduration=5000',
                                                '-maxnonstdtxvalidationduration=5001',
                                                '-maxstackmemoryusageconsensus=5MB',
                                                '-maxstackmemoryusagepolicy=5MB',
                                                '-maxscriptsizepolicy=5MB',
                                                '-checkmempool=0',
                                                ],
                                            number_of_connections=1) as (conn,):

            mining_fee = 1.01  # in satoshi per byte

            # create block with coinbase
            coinbase1 = create_coinbase(height=1)
            first_block = create_block(int(conn.rpc.getbestblockhash(), 16), coinbase=coinbase1)
            first_block.solve()
            conn.send_message(msg_block(first_block))
            wait_until(lambda: conn.rpc.getbestblockhash() == first_block.hash, check_interval=1)

            coinbase2 = create_coinbase(height=2)
            second_block = create_block(int(conn.rpc.getbestblockhash(), 16), coinbase=coinbase2)
            second_block.solve()
            conn.send_message(msg_block(second_block))
            wait_until(lambda: conn.rpc.getbestblockhash() == second_block.hash, check_interval=1)

            #mature the coinbase
            conn.rpc.generate(100)

            funding_tx = self.create_tx([(coinbase1, 0), (coinbase2, 0)], 16, mining_fee, 0)

            conn.send_message(msg_tx(funding_tx))
            check_mempool_equals(conn.rpc, [funding_tx])
            conn.rpc.generate(1)

            #        (funding_tx, 0)          (funding_tx, 1)  (funding_tx, 2)  (funding_tx, 3)   (funding_tx, 4-14)       (funding_tx, 15)
            # ---------------------------------------------------------------------------------------------------------------------
            #           group1tx1                lowPaying3           tx1            tx3            (long chain of      (chain of high
            #               |                         |                |              |             high paying txs)    paying txs used to
            #           group1tx2                lowPaying4           tx2          lowPaying5     to fill the mempool)  push low paying txs
            #          /          \                                                                                      out of mempools)
            #    group1paying   group2tx1
            #         |             |
            #    lowPaying1     group2tx2
            #                       |
            #                   group2paying
            #                       |
            #                   lowPaying2

            group1tx1 = self.create_tx([(funding_tx, 0)], noutput=1, feerate=0, totalSize=ONE_MEGABYTE)
            group1tx2 = self.create_tx([(group1tx1, 0)], noutput=2, feerate=0, totalSize=ONE_MEGABYTE)
            group1paying = self.create_tx([(group1tx2, 0)], noutput=1, feerate=1.4, totalSize=ONE_MEGABYTE,
                                          size_of_nonpayin_txs=self.tx_size(group1tx1)+self.tx_size(group1tx2))

            group2tx1 = self.create_tx([(group1tx2, 1)], noutput=1, feerate=0, totalSize=ONE_MEGABYTE)
            group2tx2 = self.create_tx([(group2tx1, 0)], noutput=1, feerate=0, totalSize=ONE_MEGABYTE)
            group2paying = self.create_tx([(group2tx2, 0)], noutput=1, feerate=1.6, totalSize=ONE_MEGABYTE,
                                          size_of_nonpayin_txs=self.tx_size(group2tx1)+self.tx_size(group2tx2))

            tx1 = self.create_tx([(funding_tx, 2)], noutput=1, feerate=1.1, totalSize=ONE_MEGABYTE)
            tx2 = self.create_tx([(tx1, 0)], noutput=1, feerate=1.8, totalSize=ONE_MEGABYTE)
            tx3 = self.create_tx([(funding_tx, 3)], noutput=1, feerate=1.1, totalSize=ONE_MEGABYTE)

            lowPaying1 = self.create_tx([(group1paying, 0)], noutput=1, feerate=0.1, totalSize=ONE_MEGABYTE)
            lowPaying2 = self.create_tx([(group2paying, 0)], noutput=1, feerate=0.2, totalSize=ONE_MEGABYTE)
            lowPaying3 = self.create_tx([(funding_tx, 1)], noutput=1, feerate=0.3, totalSize=ONE_MEGABYTE)
            lowPaying4 = self.create_tx([(lowPaying3, 0)], noutput=1, feerate=0.4, totalSize=ONE_MEGABYTE)
            lowPaying5 = self.create_tx([(tx3, 0)], noutput=1, feerate=0.5, totalSize=ONE_MEGABYTE)

            primaryMempoolTxs = [group1tx1, group1tx2, group1paying, group2tx1, group2tx2, group2paying, tx1, tx2, tx3]
            secondaryMempoolTxs = [lowPaying1, lowPaying2, lowPaying3, lowPaying4, lowPaying5]

            for tx in primaryMempoolTxs + secondaryMempoolTxs:
                conn.send_message(msg_tx(tx))
            check_mempool_equals(conn.rpc, primaryMempoolTxs + secondaryMempoolTxs)
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == len(primaryMempoolTxs) + 1)

            txs_in_mempool = set(primaryMempoolTxs + secondaryMempoolTxs)
            outpoints_to_spend = [(funding_tx, n) for n in range(4,15)]

            while len(txs_in_mempool) < 299:
                tx = self.create_tx([outpoints_to_spend.pop(0),], noutput=2, feerate=5, totalSize=ONE_MEGABYTE)
                outpoints_to_spend.append((tx, 0))
                outpoints_to_spend.append((tx, 1))
                conn.send_message(msg_tx(tx))
                txs_in_mempool.add(tx)

            check_mempool_equals(conn.rpc, txs_in_mempool, timeout=600, check_interval=2)

            eviction_order = [lowPaying1, lowPaying2, lowPaying4, lowPaying3, lowPaying5,
                              tx3,
                              group2paying, group2tx2, group2tx1,
                              group1paying, group1tx2, group1tx1,
                              tx2, tx1]

            conn.rpc.log.info(f"lowPaying1 = {lowPaying1.hash}")
            conn.rpc.log.info(f"lowPaying2 = {lowPaying2.hash}")
            conn.rpc.log.info(f"lowPaying3 = {lowPaying3.hash}")
            conn.rpc.log.info(f"lowPaying4 = {lowPaying4.hash}")
            conn.rpc.log.info(f"lowPaying5 = {lowPaying5.hash}")
            conn.rpc.log.info(f"tx1 = {tx1.hash}")
            conn.rpc.log.info(f"tx2 = {tx2.hash}")
            conn.rpc.log.info(f"tx3 = {tx3.hash}")
            conn.rpc.log.info(f"group2paying = {group2paying.hash}")
            conn.rpc.log.info(f"group2tx2 = {group2tx2.hash}")
            conn.rpc.log.info(f"group2tx1 = {group2tx1.hash}")
            conn.rpc.log.info(f"group1paying = {group1paying.hash}")
            conn.rpc.log.info(f"group1tx2 = {group1tx2.hash}")
            conn.rpc.log.info(f"group1tx1 = {group1tx1.hash}")

            outpoint_to_spend = (funding_tx, 15)

            for evicting in eviction_order:
                tx = self.create_tx([outpoint_to_spend,], noutput=1, feerate=30, totalSize=ONE_MEGABYTE)
                outpoint_to_spend = (tx, 0)
                conn.send_message(msg_tx(tx))
                txs_in_mempool.add(tx)
                txs_in_mempool.remove(evicting)
                check_mempool_equals(conn.rpc, txs_in_mempool, check_interval=0.5, timeout=60)

                # when there are still some secondary mempool transaction in the mempool
                if len(txs_in_mempool & set(secondaryMempoolTxs)) != 0:
                    # the mempoolminfee should not exceed minminingtxfee
                    assert conn.rpc.getmempoolinfo()['mempoolminfee'] <= conn.rpc.getsettings()['minminingtxfee']

        with self.run_node_with_connections("Restart the node with using the disk for storing transactions.",
                                            0, ["-minminingtxfee=0.00001", # 1 satoshi/byte
                                                "-minrelaytxfee=0",
                                                "-maxmempool=300MB",
                                                "-maxmempoolsizedisk=10MB",
                                                "-genesisactivationheight=1",
                                                '-maxstdtxvalidationduration=5000',
                                                '-maxnonstdtxvalidationduration=5001',
                                                '-maxstackmemoryusageconsensus=5MB',
                                                '-maxstackmemoryusagepolicy=5MB',
                                                '-maxscriptsizepolicy=5MB',
                                                '-checkmempool=0',
                                                ],
                                            number_of_connections=1) as (conn,):

            # check that we have all txs in the mempool
            check_mempool_equals(conn.rpc, txs_in_mempool, check_interval=1, timeout=(60 * self.options.timeoutfactor))

            # check that we are not using the tx database
            assert conn.rpc.getmempoolinfo()['usagedisk'] == 0

            #now we have room for some more txs
            for _ in range(3):
                tx = self.create_tx([outpoint_to_spend,], noutput=1, feerate=1, totalSize=ONE_MEGABYTE)
                outpoint_to_spend = (tx, 0)
                conn.send_message(msg_tx(tx))
                txs_in_mempool.add(tx)
                check_mempool_equals(conn.rpc, txs_in_mempool, check_interval=0.5, timeout=60)

            # make sure that we are using the tx database now
            assert conn.rpc.getmempoolinfo()['usagedisk'] != 0

        with self.run_node_with_connections("Restart the node once again to see if transaction were stored in the db.",
                                            0, ["-minminingtxfee=0.00001", # 1 satoshi/byte
                                                "-minrelaytxfee=0",
                                                "-maxmempool=300MB",
                                                "-maxmempoolsizedisk=10MB",
                                                "-genesisactivationheight=1",
                                                '-maxstdtxvalidationduration=5000',
                                                '-maxnonstdtxvalidationduration=5001',
                                                '-maxstackmemoryusageconsensus=5MB',
                                                '-maxstackmemoryusagepolicy=5MB',
                                                '-maxscriptsizepolicy=5MB',
                                                '-checkmempool=0',
                                                ],
                                            number_of_connections=1) as (conn,):

            # check that we have all txs in the mempool
            check_mempool_equals(conn.rpc, txs_in_mempool, check_interval=1, timeout=(60 * self.options.timeoutfactor))

            # make sure that we are using the tx database
            assert conn.rpc.getmempoolinfo()['usagedisk'] != 0


if __name__ == '__main__':
    Evictions().main()
