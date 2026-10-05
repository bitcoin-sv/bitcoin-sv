#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2026 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""Test rescanblockchain wallet RPC and bitcoin-cli."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class RescanBlockchainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        # Node 3 runs in manual-prune mode so we can exercise the
        # "Can't rescan beyond pruned data" error path of rescanblockchain.
        # `-preferredblockfilesize` is set very low so that block-file
        # rollover happens after only a few empty regtest blocks, which is
        # what lets `pruneblockchain` actually remove block data later.
        self.extra_args = [
            [],
            [],
            [],
            ["-prune=1", "-preferredblockfilesize=1024"],
        ]

    def run_test(self):
        self.log.info("Mine spendable balance on node 0")
        self.nodes[0].generate(101)
        self.sync_all()

        label = "test_label"
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

        # Import keys on node 1 without rescan; balance should remain zero.
        self.nodes[1].importprivkey(privkey1, label, False)
        self.nodes[1].importprivkey(privkey2, label, False)
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("0"))

        self.log.info("Bounded rescan via bitcoin-cli")
        assert_equal(
            self.nodes[1].cli.rescanblockchain(height1, height1),
            {"start_height": height1, "stop_height": height1})
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("1.0"))

        txids = {tx["txid"] for tx in self.nodes[1].listtransactions(label, 100, 0, True)}
        assert txid1 in txids
        assert txid2 not in txids

        self.log.info("Second bounded rescan via RPC")
        assert_equal(
            self.nodes[1].rescanblockchain(height2, height2),
            {"start_height": height2, "stop_height": height2})
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))

        txids = {tx["txid"] for tx in self.nodes[1].listtransactions(label, 100, 0, True)}
        assert txid1 in txids
        assert txid2 in txids

        self.log.info("Default full-range rescan via bitcoin-cli")
        assert_equal(
            self.nodes[1].cli.rescanblockchain(),
            {
                "start_height": 0,
                "stop_height": self.nodes[1].getblockcount(),
            })
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))

        self.log.info("Parameter validation")
        # stop_height == start_height is a valid request and is already
        # exercised by the two bounded rescans above.
        assert_raises_rpc_error(
            -8,
            "Invalid negative start_height",
            self.nodes[1].rescanblockchain,
            -1)
        assert_raises_rpc_error(
            -8,
            "Invalid start_height",
            self.nodes[1].rescanblockchain,
            self.nodes[1].getblockcount() + 1)
        assert_raises_rpc_error(
            -8,
            "Invalid stop_height",
            self.nodes[1].rescanblockchain,
            0,
            self.nodes[1].getblockcount() + 1)
        assert_raises_rpc_error(
            -8,
            "Invalid negative stop_height",
            self.nodes[1].rescanblockchain,
            0,
            -1)
        assert_raises_rpc_error(
            -8,
            "stop_height must be greater than or equal to start_height",
            self.nodes[1].rescanblockchain,
            height2,
            height1)
        assert_raises_rpc_error(
            -1,
            "JSON value is not an integer as expected",
            self.nodes[1].rescanblockchain,
            "not-an-int")
        assert_raises_rpc_error(
            -1,
            "JSON value is not an integer as expected",
            self.nodes[1].rescanblockchain,
            0,
            "not-an-int")

        self.log.info("Genesis-only rescan succeeds")
        assert_equal(
            self.nodes[1].rescanblockchain(0, 0),
            {"start_height": 0, "stop_height": 0})
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))
        # Genesis block contains no transactions relevant to this wallet,
        # so the rescan must not add or alter any entries.
        assert_equal(
            len(self.nodes[1].listtransactions(label, 100, 0, True)), 2)

        self.log.info("Rescan is idempotent")
        # Re-scanning a range that has already been scanned must not duplicate
        # transactions or change the wallet balance.
        result = self.nodes[1].rescanblockchain(height1, height2)
        assert_equal(result, {"start_height": height1, "stop_height": height2})
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))
        first_txs = self.nodes[1].listtransactions(label, 100, 0, True)
        assert_equal(
            self.nodes[1].rescanblockchain(height1, height2), result)
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))
        second_txs = self.nodes[1].listtransactions(label, 100, 0, True)
        assert_equal(second_txs, first_txs)

        self.log.info("Rescan with existing wallet activity finds new import")
        # Seed node 1 with one of its own transactions so it has wallet state
        # before the next import + rescan; the rescan must not disturb it.
        own_address = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(own_address, Decimal("5.0"))
        self.nodes[0].generate(1)
        self.sync_all()
        total_after_seed = self.nodes[1].getbalance("*", 0, True)

        address3 = self.nodes[0].getnewaddress()
        privkey3 = self.nodes[0].dumpprivkey(address3)
        txid3 = self.nodes[0].sendtoaddress(address3, Decimal("4.0"))
        block3_hash = self.nodes[0].generate(1)[0]
        height3 = self.nodes[0].getblock(block3_hash)["height"]
        self.sync_all()

        new_label = "later_import"
        self.nodes[1].importprivkey(privkey3, new_label, False)
        assert_equal(self.nodes[1].getbalance(new_label, 0, True), Decimal("0"))

        assert_equal(
            self.nodes[1].rescanblockchain(height3, height3),
            {"start_height": height3, "stop_height": height3})
        assert_equal(self.nodes[1].getbalance(new_label, 0, True), Decimal("4.0"))
        # Earlier import's balance is preserved.
        assert_equal(self.nodes[1].getbalance(label, 0, True), Decimal("3.0"))
        # Total wallet balance reflects only the newly found tx.
        assert_equal(
            self.nodes[1].getbalance("*", 0, True),
            total_after_seed + Decimal("4.0"))
        new_label_txids = {
            tx["txid"]
            for tx in self.nodes[1].listtransactions(new_label, 100, 0, True)
        }
        assert_equal(new_label_txids, {txid3})

        self.log.info("Default rescan from genesis finds txs at multiple heights")
        # Use node 2's fresh wallet so the no-arg rescan must actually walk
        # every block from genesis to the tip to find these transactions.
        # Three external keys are funded at well-separated chain heights so
        # that early, middle and late sections of the chain are each covered.
        node2 = self.nodes[2]
        fresh_addresses = [self.nodes[0].getnewaddress() for _ in range(3)]
        fresh_privkeys = [self.nodes[0].dumpprivkey(a) for a in fresh_addresses]
        assert len(set(fresh_addresses)) == 3

        early_txid = self.nodes[0].sendtoaddress(fresh_addresses[0], Decimal("0.5"))
        self.nodes[0].generate(1)

        self.nodes[0].generate(10)
        mid_txid = self.nodes[0].sendtoaddress(fresh_addresses[1], Decimal("0.75"))
        self.nodes[0].generate(1)

        self.nodes[0].generate(10)
        late_txid = self.nodes[0].sendtoaddress(fresh_addresses[2], Decimal("1.25"))
        self.nodes[0].generate(1)

        self.sync_all()

        fresh_label = "fresh_full"
        for pk in fresh_privkeys:
            node2.importprivkey(pk, fresh_label, False)
        assert_equal(node2.getbalance(fresh_label, 0, True), Decimal("0"))

        tip_height = node2.getblockcount()
        assert_equal(
            node2.rescanblockchain(),
            {"start_height": 0, "stop_height": tip_height})
        assert_equal(node2.getbalance(fresh_label, 0, True), Decimal("2.5"))
        found_txids = {
            tx["txid"]
            for tx in node2.listtransactions(fresh_label, 100, 0, True)
        }
        assert_equal(found_txids, {early_txid, mid_txid, late_txid})

        self.log.info("Rescan fails when target range contains pruned blocks")
        node3 = self.nodes[3]
        # Manual prune (-prune=1) only takes effect once the chain exceeds
        # PruneAfterHeight (1000 in regtest), so extend the chain first.
        PRUNE_AFTER_HEIGHT = 1000
        needed = max(0, PRUNE_AFTER_HEIGHT + 10 - self.nodes[0].getblockcount())
        if needed:
            self.nodes[0].generate(needed)
        self.sync_all()

        # Drop block data for early heights on the pruned node.
        # pruneblockchain returns the height it was asked to prune up to,
        # clamped down to chainHeight - MIN_BLOCKS_TO_KEEP (288) if the
        # request lands within the keep-window of the tip. With chainHeight
        # >= PRUNE_AFTER_HEIGHT + 10 == 1010 and a request of 500, no
        # clamping applies and we expect the exact value back. (Whether
        # block data was actually freed from disk is verified by the
        # rescan failure assertion below — without freed data, the
        # rescanblockchain call would not raise.)
        pruned_to = node3.pruneblockchain(500)
        assert_equal(pruned_to, 500)

        # Any rescan whose range covers a pruned block must fail with the
        # documented RPC error.
        assert_raises_rpc_error(
            -1,
            "Can't rescan beyond pruned data",
            node3.rescanblockchain,
            0)

        # Conversely, a rescan whose range lies entirely above the pruned
        # heights must succeed. safe_height is taken close to the tip (well
        # inside MIN_BLOCKS_TO_KEEP=288), so the chosen block is guaranteed
        # to still have block data on disk.
        safe_height = node3.getblockcount() - 10
        assert safe_height > pruned_to
        assert_equal(
            node3.rescanblockchain(safe_height, safe_height),
            {"start_height": safe_height, "stop_height": safe_height})


if __name__ == "__main__":
    RescanBlockchainTest().main()
