#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check RPC functions used to unfreeze TXOs
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class FrozenTXORPCUnfreezeFunds (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self, split=False):
        self.setup_nodes()

    def run_test(self):
        # preparing test cases...
        self.nodes[0].addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0}],
                    "policyExpiresWithConsensus": False
                },
                {
                    "txOut" : {
                        "txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0}],
                    "policyExpiresWithConsensus": False
                },
                {
                    "txOut" : {
                        "txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0}],
                    "policyExpiresWithConsensus": False
                }]
        })

        self.log.info("Unfreezing funds on consensus level (#2 with keepOnPolicy=false)...")
        result = self.nodes[0].addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0, "stop": 1}, {"start": 2, "stop": 3}],
                    "policyExpiresWithConsensus": False
                },
                {
                    "txOut" : {
                        "txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0, "stop": 1}, {"start": 2, "stop": 3}],
                    "policyExpiresWithConsensus": True
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Querying frozen funds and checking that data was updated...")
        result = self.nodes[0].queryBlacklist()
        assert_equal(len(result["funds"]), 3)
        funds = sorted(result["funds"], key=lambda f: f["txOut"]["txId"])
        assert_equal(funds[0], {"txOut" : {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 2147483647}], "policyExpiresWithConsensus": 0, "blacklist": ["policy", "consensus"]})
        assert_equal(funds[1], {"txOut" : {"txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 1}, {"start": 2, "stop": 3}], "policyExpiresWithConsensus": 0, "blacklist": ["policy", "consensus"]})
        assert_equal(funds[2], {"txOut" : {"txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 1}, {"start": 2, "stop": 3}], "policyExpiresWithConsensus": 1, "blacklist": ["policy", "consensus"]})

        self.log.info("Cleanup expired consensus record #2 and update #1 from consensus to policy...")

        # should do nothing until height 3 since records only expire then
        for h in range(0, 3):
            result = self.nodes[0].clearBlacklists({"removeAllEntries": False, "expirationHeightDelta": 0})
            assert_equal(result["numRemovedEntries"], 0)

            self.nodes[0].generate(1)

        # should still do nothing at height 3 if records must be at least one block old
        result = self.nodes[0].clearBlacklists({"removeAllEntries": False, "expirationHeightDelta": 1})
        assert_equal(result["numRemovedEntries"], 0)

        self.nodes[0].generate(1) # generate one more block to increase height to 2

        # now entries should be removed/updated
        result = self.nodes[0].clearBlacklists({"removeAllEntries": False, "expirationHeightDelta": 1})
        assert_equal(result["numRemovedEntries"], 1)

        self.log.info("Querying frozen funds and checking that data was updated...")
        result = self.nodes[0].queryBlacklist()
        assert_equal(len(result["funds"]), 2)
        funds = sorted(result["funds"], key=lambda f: f["txOut"]["txId"])
        assert_equal(funds[0], {"txOut" : {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 2147483647}], "policyExpiresWithConsensus": 0, "blacklist": ["policy", "consensus"]})
        assert_equal(funds[1], {"txOut" : {"txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "vout" : 0},                                                                                         "blacklist": ["policy"]})

        self.log.info("Unfreezing fund on policy level...")
        result = self.nodes[0].removeFromPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                        "vout" : 0
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Unfreezing non-existent fund should fail...")
        result = self.nodes[0].removeFromPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                        "vout" : 0
                    }
                }]
        })
        np = result["notProcessed"]
        assert_equal(len(np), 1)
        assert_equal(np[0]["reason"], "not found")
        assert_equal(np[0]["txOut"], {"txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", "vout" : 0})

        self.log.info("Unfreezing fund on policy level that is on consensus level is not allowed...")
        result = self.nodes[0].removeFromPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                        "vout" : 0
                    }
                }]
        })
        np = result["notProcessed"]
        assert_equal(len(np), 1)
        assert_equal(np[0]["reason"], "in consensus")
        assert_equal(np[0]["txOut"], {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0})

        self.log.info("Querying frozen funds and checking there is 1 in consensus blacklist")
        result = self.nodes[0].queryBlacklist()
        funds = result["funds"]
        assert_equal(len(funds), 1)
        assert_equal(funds[0], {"txOut" : {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 2147483647}], "policyExpiresWithConsensus": 0, "blacklist": ["policy", "consensus"]})


if __name__ == '__main__':
    FrozenTXORPCUnfreezeFunds().main()
