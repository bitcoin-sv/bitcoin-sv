#!/usr/bin/env python3
# Copyright (c) 2026 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test crash bug when processing duplicate blocktxn P2P messages.

This test checks a security vulnerability where a malicious peer can crash
the node by sending duplicate blocktxn messages with invalid data.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import (
    NodeConnCB,
    msg_cmpctblock,
    msg_blocktxn,
    BlockTransactions,
    HeaderAndShortIDs,
    uint256_from_str,
    uint256_from_compact,
    mininode_lock,
    CTransaction,
    CTxIn,
    CTxOut,
    COutPoint,
)
from test_framework.script import CScript, OP_TRUE
from test_framework.blocktools import create_block, create_coinbase
from test_framework.util import wait_until, check_for_log_msg, open_log_file
import os


class GetBlockTxnNode(NodeConnCB):
    """Custom node connection to intercept getblocktxn messages"""

    def __init__(self):
        super().__init__()
        self.getblocktxn_received = None

    def on_getblocktxn(self, conn, message):
        """Called when node sends getblocktxn request"""
        self.getblocktxn_received = message


class DuplicateBlockTxnCrashTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def build_block_with_transactions(self, node):
        """Build a block with transactions not in the node's mempool"""
        height = node.getblockcount()
        tip = node.getbestblockhash()
        mtp = node.getblockheader(tip)['mediantime']

        # Create block
        block = create_block(int(tip, 16), create_coinbase(height + 1), mtp + 1)

        # Get a UTXO to spend using listunspent RPC
        utxos = node.listunspent()
        assert len(utxos) > 0, "No UTXOs available to spend"
        utxo = utxos[0]

        # Create transaction that spend from the UTXO
        # These transactions will NOT be in the node's mempool
        prev_tx = utxo['txid']
        prev_vout = utxo['vout']
        amount = int(utxo['amount'] * 100000000)  # Convert to satoshis

        # Transaction doesn't need to be validly signed for this test
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(prev_tx, 16), prev_vout), b''))
        tx.vout.append(CTxOut(amount - 1000, CScript([OP_TRUE])))
        tx.rehash()
        block.vtx.append(tx)

        # Calculate merkle root and solve PoW
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        return block

    def run_test(self):
        # Generate initial blockchain
        self.nodes[0].generate(101)

        # Create test block with transactions not in mempool
        test_block = self.build_block_with_transactions(self.nodes[0])

        # Create compact block message with invalid merkle root
        comp_block = HeaderAndShortIDs()
        comp_block.initialize_from_block(test_block, prefill_list=[0])

        # Convert to P2P wire format
        p2p_cmpct = comp_block.to_p2p()

        # CRITICAL: Set an invalid merkle root (random bytes)
        # Then re-solve the PoW so the header is still valid
        p2p_cmpct.header.hashMerkleRoot = uint256_from_str(os.urandom(32))

        # Re-solve the PoW with the new merkle root so the header is valid
        # Manually implement solve() for CBlockHeader
        p2p_cmpct.header.rehash()
        target = uint256_from_compact(p2p_cmpct.header.nBits)
        while p2p_cmpct.header.sha256 > target:
            p2p_cmpct.header.nNonce += 1
            p2p_cmpct.header.rehash()

        # The new block hash after re-solving with the modified merkle root
        modified_block_hash = p2p_cmpct.header.sha256

        # Create the compact block message
        cmpctblock_msg = msg_cmpctblock(p2p_cmpct)

        # Create blocktxn message with correct transactions
        # Use the MODIFIED block hash (not the original test_block.sha256)
        correct_txs = test_block.vtx[1:]  # Exclude coinbase (it was prefilled)
        blocktxn_msg = msg_blocktxn()
        blocktxn_msg.block_transactions = BlockTransactions(
            modified_block_hash,  # Use the new block hash
            correct_txs
        )

        # Execute test with fresh node and open log file
        self.stop_node(0)

        with self.run_node_with_connections("Duplicate blocktxn crash test", 0, [], 1, cb_class=GetBlockTxnNode) as (test_conn,):
            # Open log file to track messages incrementally
            with open_log_file(self, "/node0") as log_file:

                # Send compact block and verify receipt
                test_conn.send_message(cmpctblock_msg)

                # Verify node received the compact block (check log)
                wait_until(lambda: check_for_log_msg(self, "received: cmpctblock", log_file=log_file), timeout=10)

                # Wait for node to request missing transactions
                def received_getblocktxn():
                    return test_conn.cb.getblocktxn_received is not None

                wait_until(received_getblocktxn, timeout=10, lock=mininode_lock)

                # Verify the request is for the expected block (the modified one)
                received_blockhash = test_conn.cb.getblocktxn_received.block_txn_request.blockhash
                expected_blockhash = modified_block_hash
                assert received_blockhash == expected_blockhash, \
                    f"Block hash mismatch: received {received_blockhash}, expected {expected_blockhash}"

                # Send first blocktxn and verify error handling
                test_conn.send_message(blocktxn_msg)

                # Verify node received the blocktxn message
                wait_until(lambda: check_for_log_msg(self, "received: blocktxn", log_file=log_file), timeout=10)

                # Send second blocktxn and verify rejection with no crash
                test_conn.send_message(blocktxn_msg)

                wait_until(lambda: check_for_log_msg(self, "received: blocktxn", log_file=log_file), timeout=10)
                wait_until(lambda: check_for_log_msg(self, "reason: invalid-cmpctblk-txns", log_file=log_file), timeout=10)


if __name__ == '__main__':
    DuplicateBlockTxnCrashTest().main()
