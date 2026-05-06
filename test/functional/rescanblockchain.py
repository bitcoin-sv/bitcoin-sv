#!/usr/bin/env python3
# Copyright (c) 2026 Bitcoin Association
# Distributed under Open BSV software license, see accompanying file LICENSE.
"""Test rescanblockchain wallet RPC and CLI."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class RescanBlockchainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def run_test(self):
        self.log.info("mine spendable balance")
        self.nodes[0].generate(101)
        self.sync_all()

        label = "rescanned"
        address1 = self.nodes[0].getnewaddress()
        address2 = self.nodes[0].getnewaddress()
        privkey1 = self.nodes[0].dumpprivkey(address1)
        privkey2 = self.nodes[0].dumpprivkey(address2)

        txid1 = self.nodes[0].sendtoaddress(address1, Decimal("1.0"))
        block1_hash = self.nodes[0].generate(1)[0]
        height1 = self.nodes[0].getblock(block1_hash)["height"]

        txid2 = self.nodes[0].sendtoaddress(address2, Decimal("2.0"))
        block2_hash = self.nodes[0].generate(1)[0]
        height2 = self.nodes[0].getblock(block2_hash)["height"]

        self.sync_all()

        self.nodes[1].importprivkey(privkey1, label, False)
        self.nodes[1].importprivkey(privkey2, label, False)
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("0"))

        node1_pid = self.nodes[1].process.pid

        self.log.info("bounded rescan through bitcoin-cli")
        assert_equal(
            self.nodes[1].cli.rescanblockchain(height1, height1),
            {"start_height": height1, "stop_height": height1})
        assert_equal(self.nodes[1].process.pid, node1_pid)
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("1.0"))

        txids = {tx["txid"] for tx in self.nodes[1].listtransactions(label, 100, 0, True)}
        assert_equal(txid1 in txids, True)
        assert_equal(txid2 in txids, False)

        self.log.info("second bounded rescan through RPC")
        assert_equal(
            self.nodes[1].rescanblockchain(height2, height2),
            {"start_height": height2, "stop_height": height2})
        assert_equal(self.nodes[1].process.pid, node1_pid)
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))

        txids = {tx["txid"] for tx in self.nodes[1].listtransactions(label, 100, 0, True)}
        assert_equal(txids.issuperset({txid1, txid2}), True)

        self.log.info("default full-range rescan through bitcoin-cli")
        assert_equal(
            self.nodes[1].cli.rescanblockchain(),
            {
                "start_height": 0,
                "stop_height": self.nodes[1].getblockcount(),
            })
        assert_equal(self.nodes[1].process.pid, node1_pid)
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))

        self.log.info("parameter validation")
        assert_raises_rpc_error(
            -8,
            "Block height out of range",
            self.nodes[1].rescanblockchain,
            -1)
        assert_raises_rpc_error(
            -8,
            "Block height out of range",
            self.nodes[1].rescanblockchain,
            self.nodes[1].getblockcount() + 1)
        assert_raises_rpc_error(
            -8,
            "stop_height must be greater than or equal to start_height",
            self.nodes[1].rescanblockchain,
            height2,
            height1)


if __name__ == '__main__':
    RescanBlockchainTest().main()
