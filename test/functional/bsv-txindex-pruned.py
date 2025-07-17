#!/usr/bin/python3
# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import satoshi_round
from test_framework.authproxy import JSONRPCException

from decimal import Decimal

'''
Test that the txindex works correctly with pruning enabled.

Blocks are created of with single large OP_RETURN txns to match
the block file size so we can control exactly which txns get
pruned at each stage.
'''

NUM_TXNS = 10


class BsvTxIndexPrunedTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txindex',
                            '-prune=5000',
                            '-genesisactivationheight=1',
                            '-maxmempool=1GB',
                            '-minminingtxfee=0.00000001',
                            '-blockmaxsize=4GB',
                            '-preferredblockfilesize=8388608',
                            '-pruneminblockstokeep=10']]

    def send_large_op_return(self):
        # Generate a large OP_RETURN output (8MB)
        data_size = 8 * 1024 * 1024
        # Avoid allocating a huge bytes object in memory
        op_return_data_hex = 'ff' * data_size  # OP_RETURN with large data as hex string
        fee = satoshi_round(Decimal(0.01))

        # Build raw transaction
        utxo = self.nodes[0].listunspent()[0]
        inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
        outputs = {self.nodes[0].getnewaddress(): utxo["amount"] - fee, "data": op_return_data_hex}
        raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        signed_tx = self.nodes[0].signrawtransaction(raw_tx)["hex"]

        # Send to mempool
        txid_large = self.nodes[0].sendrawtransaction(signed_tx)
        assert txid_large in self.nodes[0].getrawmempool(), "Large transaction not found in mempool"

    def run_test(self):
        self.nodes[0].generate(999)
        self.send_large_op_return()
        self.nodes[0].generate(1)

        # Send some transactions to ourselves over the next few blocks.
        # One txn per block starting from block height 1001.
        txids = []
        address = self.nodes[0].getnewaddress()
        for b in range(NUM_TXNS):
            txid = self.nodes[0].sendtoaddress(address, 1)
            txids.append(txid)
            self.send_large_op_return()
            self.nodes[0].generate(1)
            print(f"Sent transaction {txid} in block {1001 + b}")

        def check_txn_in_txindex(txid, func, *args):
            try:
                func(*args)
                return True
            except JSONRPCException:
                raise AssertionError(f"Transaction {txid} not found in txindex")

        # Check we can getrawtransaction/getmerkleproof for each txid
        for txid in txids:
            check_txn_in_txindex(txid, self.nodes[0].getrawtransaction, txid)
            check_txn_in_txindex(txid, self.nodes[0].getmerkleproof, txid)
            check_txn_in_txindex(txid, self.nodes[0].getmerkleproof2, "", txid)

        # Prune up to but not including block height 1001
        self.nodes[0].generate(10)
        self.nodes[0].pruneblockchain(1000)

        # Verify we can still get all the txns
        print("Verifying transactions after initial pruning...")
        for txid in txids:
            check_txn_in_txindex(txid, self.nodes[0].getrawtransaction, txid)
            check_txn_in_txindex(txid, self.nodes[0].getmerkleproof, txid)
            check_txn_in_txindex(txid, self.nodes[0].getmerkleproof2, "", txid)

        def handle_rpc_exception(txid, height, rpc, b, idx, e):
            print(f"Transaction {txid} not found with {rpc} after pruning block {height}: {e}")
            if b < idx:
                raise AssertionError(f"Transaction {txid} unexpectedly missing with {rpc} after pruning block {height}")

        # Prune each block, and verify we can still get the txns
        print("Verifying transactions during main pruning...")
        for b in range(NUM_TXNS):
            height = 1001 + b
            self.nodes[0].pruneblockchain(height)
            print(f"Pruned up to block {height}")
            for idx, txid in enumerate(txids):
                all_ok = True
                try:
                    self.nodes[0].getrawtransaction(txid)
                except JSONRPCException as e:
                    handle_rpc_exception(txid, height, "getrawtransaction", b, idx, e)
                    all_ok = False
                try:
                    self.nodes[0].getmerkleproof(txid)
                except JSONRPCException as e:
                    handle_rpc_exception(txid, height, "getmerkleproof", b, idx, e)
                    all_ok = False
                try:
                    self.nodes[0].getmerkleproof2("", txid)
                except JSONRPCException as e:
                    handle_rpc_exception(txid, height, "getmerkleproof2", b, idx, e)
                    all_ok = False

                if all_ok:
                    print(f"Transaction {txid} found ok by all RPCs after pruning block {height}")


if __name__ == '__main__':
    BsvTxIndexPrunedTest().main()
