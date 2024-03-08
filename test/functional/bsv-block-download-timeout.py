#!/usr/bin/env python3
# Copyright (c) 2021  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from time import sleep, time

from test_framework.blocktools import create_block, create_coinbase, send_by_headers
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block
from test_framework.script import CScript, OP_DROP, OP_TRUE, OP_FALSE, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_mempool_equals, connect_nodes_bi, count_log_msg

"""
Testing block downloading timeouts:
Three cases with two subcases each:
Preparation:
    - preparing a funding transaction (with enough outputs) on three node instances, one for each case
    - creating a block with significant size used for testing

CASE 1: Testing on a node with single connection, "timeout base" set to 60s (10 meaning 10% of the 10min)
    - sending prepared block with sending rate set so that expected transfer time would be 70s -> timeout occures
    - sending prepared block with sending rate set so that expected transfer time would be 50s -> timeout does not occur

CASE 2: Testing on a node with single connection, mocktime set to future so the node enters the IBD, "timeout base IBD" set to 60s
    - sending prepared block with sending rate set so that expected transfer time would be 70s -> timeout occures
    - sending prepared block with sending rate set so that expected transfer time would be 50s -> timeout does not occur

CASE 3: Testing on a node with two connections, after download starts on the test connection other blocks are downloading
       through the additional connection. This simulates situation when the node is downloading blocks from two
       different peers at the same time. "timeout base" and "timeout per peer" are set to 30s each.
       The total timeout is: <timeout base> + <number of additional peers> * <timeout per peer> = 30s + 1 * 30s = 60s
    - sending prepared block with sending rate set so that expected transfer time would be 70s -> timeout occures
    - sending prepared block with sending rate set so that expected transfer time would be 50s -> timeout does not occur

"""


class BlockDownloadTimeout(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def create_tx(self, outpoints, noutput, fee, additional_output_size=ONE_MEGABYTE):
        tx = CTransaction()
        total_input = 0
        for parent_tx, n in outpoints:
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, n), b"", 0xffffffff))
            total_input += parent_tx.vout[n].nValue

        for _ in range(noutput):
            tx.vout.append(CTxOut(total_input//noutput - fee, CScript([b"X", OP_DROP, OP_TRUE])))

        if additional_output_size:
            tx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN, b"X"*additional_output_size])))

        tx.rehash()
        return tx

    def create_block(self, funding_tx, block_target_size, prev_hash, prev_time, prev_height):

        block = create_block(prev_hash,
                             coinbase=create_coinbase(height=prev_height + 1),#coinbase=create_coinbase(height= + 1),
                             nTime=prev_time + 1)#nTime=last_block_info["time"] + 1)

        n_txs = len(funding_tx.vout)
        tx_target_size = block_target_size // n_txs

        for n in range(n_txs):
            tx = self.create_tx([(funding_tx, n)], 1, 2*tx_target_size, tx_target_size)
            block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.calc_sha256()
        block.solve()
        return block

    def test_send_block_to_node(self, label, node_index, block, send_rate, expected_time_to_send ,cmd_timeout_base,
                                cmd_timeout_base_ibd, cmd_timeout_per_peer, expect_timeout,
                                mocktime=0,
                                additional_conn_blocks=[],
                                additional_conn_send_rate=0):

        node_args = [
            "-genesisactivationheight=1",  # to be able to process large transactions
            f"-blockdownloadtimeoutbasepercent={cmd_timeout_base}",
            f"-blockdownloadtimeoutbaseibdpercent={cmd_timeout_base_ibd}",
            f"-blockdownloadtimeoutperpeerpercent={cmd_timeout_per_peer}",
        ]

        if mocktime:
            node_args.append(f"-mocktime={mocktime}")

        with self.run_node_with_connections(title=label,
                                            node_index=node_index,
                                            args=node_args,
                                            number_of_connections=2) as (conn, conn_additional):

            timeout_log_line = f"Timeout downloading block {block.hash}"
            timeout_log_line_count = count_log_msg(conn.rpc, timeout_log_line)

            conn.rate_limit_sending(send_rate)
            send_by_headers(conn, [block], True)

            # sleep for a half of time that will be needed for sending the block
            sleep(expected_time_to_send / 3)

            if additional_conn_blocks:
                conn_additional.rate_limit_sending(additional_conn_send_rate)
                send_by_headers(conn_additional, additional_conn_blocks, True)

            if expect_timeout:
                # wait until we are disconnected from the node (this will be a consequence of the block downloading timeout)
                wait_until(lambda: not conn.connected, timeout=expected_time_to_send, check_interval=0.3)
                assert count_log_msg(conn.rpc, timeout_log_line) == timeout_log_line_count + 1
            else:
                wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, timeout=expected_time_to_send, check_interval=3)
                assert count_log_msg(conn.rpc, timeout_log_line) == timeout_log_line_count

    def run_test(self):
        with self.run_node_with_connections("Preparation", 0,
                                            [],
                                            number_of_connections=1) as (conn,):

            self.start_node(1)
            self.start_node(2)

            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 0, 2)

            # create block with coinbase
            coinbase = create_coinbase(height=1)
            first_block = create_block(int(conn.rpc.getbestblockhash(), 16), coinbase=coinbase)
            first_block.solve()
            conn.send_message(msg_block(first_block))
            wait_until(lambda: conn.rpc.getbestblockhash() == first_block.hash, check_interval=0.3)

            #mature the coinbase
            conn.rpc.generate(101)

            N_TX_IN_BLOCK = 30
            funding_tx = self.create_tx([(coinbase, 0)], 3, 1000, 0)
            funding_tx_1 = self.create_tx([(funding_tx, 0)], N_TX_IN_BLOCK, 1000, 0)
            funding_tx_2 = self.create_tx([(funding_tx, 1)], N_TX_IN_BLOCK, 1000, 0)
            funding_tx_3 = self.create_tx([(funding_tx, 2)], N_TX_IN_BLOCK, 1000, 0)

            conn.send_message(msg_tx(funding_tx))
            conn.send_message(msg_tx(funding_tx_1))
            conn.send_message(msg_tx(funding_tx_2))
            conn.send_message(msg_tx(funding_tx_3))
            check_mempool_equals(conn.rpc, [funding_tx, funding_tx_1, funding_tx_2, funding_tx_3])
            conn.rpc.generate(1)
            check_mempool_equals(conn.rpc, [])

            last_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())

            wait_until(lambda: self.nodes[1].getbestblockhash() == last_block_info["hash"])
            wait_until(lambda: self.nodes[2].getbestblockhash() == last_block_info["hash"])

            self.stop_node(1)
            self.stop_node(2)

        block = self.create_block(funding_tx=funding_tx_1, block_target_size=10*ONE_MEGABYTE,
                                  prev_hash=int(last_block_info["hash"], 16), prev_height=last_block_info["height"], prev_time=last_block_info["time"])

        block_bytes = block.serialize()
        block_size = len(block_bytes)
        block_bytes = None

        TARGET_SENDING_TIME_FAST = 50
        TOTAL_BLOCK_DOWNLOAD_TIMEOUT = 60
        TARGET_SENDING_TIME_SLOW = 70
        target_send_rate_fast = block_size / TARGET_SENDING_TIME_FAST
        cmd_total_timeout = 100 * TOTAL_BLOCK_DOWNLOAD_TIMEOUT // (10 * 60) # 100% * (timeout) / 10min ,
        target_send_rate_slow = block_size / TARGET_SENDING_TIME_SLOW

        """ CASE 1 """
        self.test_send_block_to_node(
            label="Single connection, sending slowly, setting only base timeout, timeout occures",
            node_index=0,
            block=block,
            send_rate=target_send_rate_slow,
            expected_time_to_send=TARGET_SENDING_TIME_SLOW,
            cmd_timeout_base=cmd_total_timeout,
            cmd_timeout_base_ibd=1000000, # we are not in IBD so this param is not relevant
            cmd_timeout_per_peer=1000000, # we are sending on only one connection so this param is not relevant
            expect_timeout=True,
        )

        self.test_send_block_to_node(
            label="Single connection, sending fast enough, setting only base timeout, timeout does not occure",
            node_index=0,
            block=block,
            send_rate=target_send_rate_fast,
            expected_time_to_send=TARGET_SENDING_TIME_FAST,
            cmd_timeout_base=cmd_total_timeout,
            cmd_timeout_base_ibd=1, # we are not in IBD so this param is not relevant
            cmd_timeout_per_peer=1, # we are sending on only one connection so this param is not relevant
            expect_timeout=False,
        )

        """ CASE 2 """
        self.test_send_block_to_node(
            label="Single connection, node in the initial block download, setting only base IBD timeout, sending slowly, timeout occures",
            node_index=1,
            block=block,
            send_rate=target_send_rate_slow,
            expected_time_to_send=TARGET_SENDING_TIME_SLOW,
            cmd_timeout_base=100000, # we are in IBD so this param is not relevant
            cmd_timeout_base_ibd=cmd_total_timeout, # we are in IBD
            cmd_timeout_per_peer=100000, # we are sending on only one connection so this param is not relevant
            mocktime=int(time()+48*60*60), # make node go 48 hours into future to put it in the IBD
            expect_timeout=True,
        )

        self.test_send_block_to_node(
            label="Single connection, node in the initial block download, setting only base IBD timeout, sending fast enough, timeout does not occure",
            node_index=1,
            block=block,
            send_rate=target_send_rate_fast,
            expected_time_to_send=TARGET_SENDING_TIME_FAST,
            cmd_timeout_base=1, # we are in IBD so this param is not relevant
            cmd_timeout_base_ibd=cmd_total_timeout, # we are in IBD
            cmd_timeout_per_peer=1, # we are sending on only one connection so this param is not relevant
            mocktime=int(time()+48*60*60), # make node go 48 hours into future to put it in the IBD
            expect_timeout=False,
        )

        """ CASE 3 """
        block_2 = self.create_block(funding_tx=funding_tx_2,
                                    block_target_size=10*ONE_MEGABYTE,
                                    prev_hash=int(last_block_info["hash"], 16),
                                    prev_height=last_block_info["height"],
                                    prev_time=last_block_info["time"])

        block_3 = self.create_block(funding_tx=funding_tx_3,
                                    block_target_size=10*ONE_MEGABYTE,
                                    prev_hash=int(block_2.hash, 16),
                                    prev_height=last_block_info["height"]+1,
                                    prev_time=last_block_info["time"]+1)

        self.test_send_block_to_node(
            label="Two connections, on both connections we are sending blocks, sending slowly, timeout occures",
            node_index=2,
            block=block,
            send_rate=target_send_rate_slow,
            expected_time_to_send=TARGET_SENDING_TIME_SLOW,
            cmd_timeout_base=cmd_total_timeout//2, # half of the timeout is contributed by the base
            cmd_timeout_base_ibd=100000, # we are not in IBD so this param is not relevant
            cmd_timeout_per_peer=cmd_total_timeout//2, # another half of the timeout is contributed by the additional (single) per peer
            additional_conn_blocks=[block_2, block_3], # blocks to send on through additional connection
            additional_conn_send_rate=target_send_rate_slow,
            expect_timeout=True,
        )

        self.test_send_block_to_node(
            label="Two connections, on both connections we are sending blocks, sending slowly, sending fast enough, timeout does not occure",
            node_index=2,
            block=block,
            send_rate=target_send_rate_fast,
            expected_time_to_send=TARGET_SENDING_TIME_FAST,
            cmd_timeout_base=cmd_total_timeout//2, # half of the timeout is contributed by the base
            cmd_timeout_base_ibd=1, # we are not in IBD so this param is not relevant
            cmd_timeout_per_peer=cmd_total_timeout//2, # another half of the timeout is contributed by the additional (single) per peer
            additional_conn_blocks=[block_2, block_3], # blocks to send on through additional connection
            additional_conn_send_rate=target_send_rate_slow,
            expect_timeout=False,
        )


if __name__ == '__main__':
    BlockDownloadTimeout().main()
