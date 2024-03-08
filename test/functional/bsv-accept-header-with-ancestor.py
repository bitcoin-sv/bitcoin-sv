#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.mininode import msg_headers, CBlockHeader, msg_block
from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import create_block, create_coinbase, wait_for_tip
from test_framework.util import wait_until, check_for_log_msg

# Scenario:
# 1. Create block B0.
# 2. Send HEADERS message of B0 to bitcoind. Bitcoind should send GETDATA for this block.
# 3. Send BLOCK message of B0 to bitcoind.
# 4. Bitcoind accepts block B0.
# 5. Create blocks B1 and B2.
# 6. Send HEADERS of B2 to bitcoind.
#    Bitcoind should not send GETDATA because previous block is missing.
# 7. Send HEADERS of B1 to bitcoind. It should be accepted (GETDATA should be received).
# 8. Send HEADERS of B2 to bitcoind. It should be accepted now that B1 is known (GETDATA should be received).
# 9. Try to send alternative Genesis block (no previous block). It should be rejected.


def prepareBlock(height, tip_hash):
    block = create_block(int("0x" + tip_hash, 16), create_coinbase(height=height, outputValue=25))
    block.solve()
    return block


class AcceptHeaderWithAncestor(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.num_peers = 1

    def run_test(self):

        self.stop_node(0)

        with self.run_node_with_connections("reject headers if previous block is missing", 0, [], self.num_peers) as p2p_connections:

            connection = p2p_connections[0]
            coinbase_height = 1

            # 1. Create first block.
            block_0 = prepareBlock(coinbase_height, self.nodes[0].getbestblockhash())

            # 2. Connection sends HEADERS msg to bitcoind and waits for GETDATA.
            headers_message = msg_headers()
            headers_message.headers = [CBlockHeader(block_0)]
            connection.cb.send_message(headers_message)
            connection.cb.wait_for_getdata()
            wait_until(lambda: connection.cb.last_message["getdata"].inv[0].hash == block_0.sha256)

            # 3. Connection sends BLOCK to bitcoind.
            connection.cb.send_message(msg_block(block_0))

            # 4. Bitcoind adds block to active chain.
            wait_for_tip(self.nodes[0], block_0.hash)

            # 5. Create two chained blocks.
            block_1 = prepareBlock(coinbase_height + 1, block_0.hash)
            block_2 = prepareBlock(coinbase_height + 2, block_1.hash)

            # 6. Connection sends HEADERS of the second block to bitcoind. It should be rejected.
            headers_message = msg_headers()
            headers_message.headers = [CBlockHeader(block_2)]
            connection.cb.send_message(headers_message)
            wait_until(lambda: check_for_log_msg(self, "received header " + block_2.hash + ": missing prev block", "/node0"))

            # 7. Connection sends HEADERS of the first block to bitcoind. It should be accepted.
            headers_message = msg_headers()
            headers_message.headers = [CBlockHeader(block_1)]
            connection.cb.send_message(headers_message)
            wait_until(lambda: connection.cb.last_message["getdata"].inv[0].hash == block_1.sha256)

            # 8. Connection sends HEADERS of the second block to bitcoind. It should be accepted now that previous block is known.
            headers_message = msg_headers()
            headers_message.headers = [CBlockHeader(block_2)]
            connection.cb.send_message(headers_message)
            wait_until(lambda: connection.cb.last_message["getdata"].inv[0].hash == block_2.sha256)

            # 9. Try to send alternative Genesis block (no previous block). It should be rejected.
            genesis_block = create_block(hashprev=0, coinbase=create_coinbase(height=0, outputValue=25))
            genesis_block.solve()
            connection.cb.send_message(msg_block(genesis_block))
            wait_until(lambda: check_for_log_msg(self, "ERROR: FindPreviousBlockIndex: prev block not found", "/node0"))


if __name__ == '__main__':
    AcceptHeaderWithAncestor().main()
