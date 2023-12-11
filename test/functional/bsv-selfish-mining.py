#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from decimal import Decimal
from time import sleep
from test_framework.blocktools import ChainManager
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import check_for_log_msg, sync_mempools, wait_until, sync_blocks, connect_nodes_bi


class SelfishMiningTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.chain = ChainManager()

    def setup_network(self):
        # Add & start nodes
        self.add_nodes(self.num_nodes)
        # Create nodes
        # Enable selfish mining detection.
        # Set lowest time difference to 10 sec between the last block and last mempool
        # transaction for the block to be classified as selfishly mined.
        # Set threshold of number of txs in mempool that are not included in received block
        # for the block to be classified as selfishly mined to 40%.
        self.start_node(0, ['-detectselfishmining=1',
                            '-minblockmempooltimedifferenceselfish=10',
                            '-selfishtxpercentthreshold=40',
                            '-minminingtxfee=0.00000500',
                            '-fallbackfee=0.00000250'])
        self.start_node(1,['-minminingtxfee=0.00000500','-fallbackfee=0.00000250'])
        connect_nodes_bi(self.nodes, 0, 1)

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        node0.generate(10)
        sync_blocks(self.nodes)
        node1.generate(10)
        sync_blocks(self.nodes)
        node0.generate(110)
        sync_blocks(self.nodes)

        # Send 3 transactions from node1, those should be relayed to node0.
        txids_in_block = [node1.sendtoaddress(node1.getnewaddress(), 1) for x in range(3)]
        sync_mempools(self.nodes)
        # Stop node1 to prevent relaying transactions from node0 and simulate selfish-mined block.
        self.stop_node(1)
        # Send 7 transactions from node0 that will not be relayed to node1 after 11 s - time diff TH
        # is set to 10 seconds when starting the node here, to avoid waiting too long for fun test to finish.
        sleep(11)
        node0.settxfee(Decimal("0.00002000"))
        txids_not_in_block = [node0.sendtoaddress(node0.getnewaddress(), 1) for x in range(7)]
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)
        node1.generate(1)
        # All transactions that node0 sent were not included in block even though they qualify to be in block - have a fee above the block's fee rate.
        # 70% of txs in mempool are not in block and over 40% TH - this is considered selfish mining.
        wait_until(lambda: check_for_log_msg(self, "7/10 transactions in mempool were not included in block. 7/7 have a fee above the block's fee rate.", "/node0"))

        # Clean the node0's mempool
        node0.generate(1)
        sync_blocks(self.nodes)
        # Set higher fee on node1
        node1.settxfee(Decimal("0.00100000"))
        # Send 3 transactions from node1, those should be relayed to node0
        txids_in_block = [node1.sendtoaddress(node1.getnewaddress(), 1) for x in range(3)]
        sync_mempools(self.nodes)
        # Stop node1 to prevent relaying transactions from node0 and simulate selfish-mined block
        self.stop_node(1)
        sleep(11)
        # Send 4 transactions from node0 with lower fee
        txids_not_in_block = [node0.sendtoaddress(node0.getnewaddress(), 1) for x in range(4)]
        # Set higher fee on node0
        node0.settxfee(Decimal("0.00150000"))
        # Send 3 transactions from node0 with higher fee
        txids_not_in_block.extend([node0.sendtoaddress(node0.getnewaddress(), 1) for x in range(3)])
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)
        node1.generate(1)
        # 7 txs that node0 sent were not included in block but only 3 of them have fee above block's fee.
        # 30% of txs in mempool are not in block and this is under 40% TH - this is not considered selfish miming.
        wait_until(lambda: check_for_log_msg(self, "7/10 transactions in mempool were not included in block. 3/7 have a fee above the block's fee rate.", "/node0"))

        # Clean the node0's mempool
        node0.generate(1)
        sync_blocks(self.nodes)
        # Set higher fee on node1
        node1.settxfee(Decimal("0.00100000"))
        # Send 3 transactions from node1, those should be relayed to node0
        txids_in_block = [node1.sendtoaddress(node1.getnewaddress(), 1) for x in range(3)]
        sync_mempools(self.nodes)
        # Stop node1 to prevent relaying transactions from node0 and simulate selfish-mined block
        self.stop_node(1)
        sleep(11)
        # Send transaction from node0 with lower fee
        node0.settxfee(Decimal("0.00002000"))
        txids_not_in_block = [node0.sendtoaddress(node0.getnewaddress(), 1)]
        # Set higher fee on node0
        node0.settxfee(Decimal("0.00150000"))
        # Send 6 transactions from node0 with higher fee
        txids_not_in_block.extend([node0.sendtoaddress(node0.getnewaddress(), 1) for x in range(6)])
        self.start_node(1)
        connect_nodes_bi(self.nodes, 0, 1)
        node1.generate(1)
        # 7 txs that node0 sent were not included, 6 of them have fee above block's fee.
        # 60% of txs in mempool are not in block and this is over 40% TH - this is considered selfish miming.
        wait_until(lambda: check_for_log_msg(self, "7/10 transactions in mempool were not included in block. 6/7 have a fee above the block's fee rate.", "/node0"))

        # Mine empty block - this is considered selfish mining.
        node1.generate(1)
        wait_until(lambda: check_for_log_msg(self, "7/7 transactions have a fee above the config blockmintxfee value. Block was either empty or none of its transactions are in our mempool.", "/node0"))


if __name__ == '__main__':
    SelfishMiningTest().main()
