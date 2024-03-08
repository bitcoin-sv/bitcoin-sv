#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check RPC functions used to freeze TXOs
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *


class FrozenTXORPCFreezeFunds (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self, split=False):
        self.setup_nodes()

    def run_test(self):
        self.log.info("Querying frozen funds to check that initially there are none")
        result = self.nodes[0].queryBlacklist()
        assert_equal(result["funds"], [])

        self.log.info("Freezing funds on policy level...")
        result = self.nodes[0].addToPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                        "vout" : 0
                    }
                },
                {
                    "txOut" : {
                        "txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                        "vout" : 0
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Querying frozen funds and checking there are 2 in policy blacklist")
        result = self.nodes[0].queryBlacklist()
        assert_equal(len(result["funds"]), 2) # there should be 2 frozen funds
        funds = sorted(result["funds"], key=lambda f: f["txOut"]["txId"]) # must be sorted since order of funds in result is unspecified
        assert_equal(funds[0], {"txOut" : {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0}, "blacklist": ["policy"]})
        assert_equal(funds[1], {"txOut" : {"txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "vout" : 0}, "blacklist": ["policy"]})

        self.log.info("Refreezing fund + new fund on policy level...")
        result = self.nodes[0].addToPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                        "vout" : 0
                    }
                },
                {
                    "txOut" : {
                        "txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                        "vout" : 0
                    }
                }]
        })
        np = result["notProcessed"]
        assert_equal(len(np), 1)
        assert_equal(np[0]["reason"], "already in policy")
        assert_equal(np[0]["txOut"], {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0})

        self.log.info("Freezing fund on consensus level...")
        result = self.nodes[0].addToConsensusBlacklist({
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
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Freezing fund on consensus level (prev not on policy)...")
        result = self.nodes[0].addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 1, "stop": 2}, {"start": 4, "stop": 5}],
                    "policyExpiresWithConsensus": True
                },
                {
                    "txOut" : {
                        "txId" : "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd", # same TXO can be specified more than once overriding previous values
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 0}],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Refreezing fund + new fund on consensus level...")
        result = self.nodes[0].addToConsensusBlacklist({
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
                        "txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
                        "vout" : 0
                    },
                    "enforceAtHeight": [{"start": 1, "stop": 2}, {"start": 11, "stop": 12}],
                    "policyExpiresWithConsensus": True
                }]
        })
        np = result["notProcessed"]
        assert_equal(len(np), 1)
        assert_equal(np[0]["reason"], "already in consensus")
        assert_equal(np[0]["txOut"], {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0})

        self.log.info("Freezing fund on policy level that is already on consensus level...")
        result = self.nodes[0].addToPolicyBlacklist({
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
        assert_equal(np[0]["reason"], "already in consensus")
        assert_equal(np[0]["txOut"], {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0})

        self.log.info("Querying frozen funds and checking there are 4 in consensus blacklist")
        result = self.nodes[0].queryBlacklist()
        assert_equal(len(result["funds"]), 4) # there should be 4 frozen funds
        funds = sorted(result["funds"], key=lambda f: f["txOut"]["txId"])
        assert_equal(funds[0], {"txOut" : {"txId" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 2147483647}], "policyExpiresWithConsensus": 0, "blacklist": ["policy", "consensus"]})
        assert_equal(funds[1], {"txOut" : {"txId" : "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 2147483647}], "policyExpiresWithConsensus": 0, "blacklist": ["policy", "consensus"]})
        assert_equal(funds[2], {"txOut" : {"txId" : "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", "vout" : 0}, "enforceAtHeight": [{"start": 1, "stop": 2}, {"start": 11, "stop": 12}], "policyExpiresWithConsensus": 1, "blacklist": ["policy", "consensus"]})
        assert_equal(funds[3], {"txOut" : {"txId" : "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd", "vout" : 0}, "enforceAtHeight": [{"start": 0, "stop": 2147483647}], "policyExpiresWithConsensus": 0, "blacklist": ["policy", "consensus"]})

        # Freeze another TXO to policy blacklist. Needed by check of clearBlacklists
        self.nodes[0].addToPolicyBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
                        "vout" : 0
                    }
                }]
        })

        self.log.info("Cleanup expired records should do nothing since nothing is expired")
        result = self.nodes[0].clearBlacklists({"removeAllEntries" : False, "expirationHeightDelta": 0})
        assert_equal(result["numRemovedEntries"], 0)

        self.log.info("Unfreezing all frozen funds except policy frozen ones")
        result = self.nodes[0].clearBlacklists({"removeAllEntries" : True,  "keepExistingPolicyEntries": True})
        assert_equal(result["numRemovedEntries"], 4) # 4 consensus + 0 policy

        result = self.nodes[0].queryBlacklist()
        assert_equal(result["funds"], [{'txOut': {'txId': 'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee', 'vout': 0}, 'blacklist': ['policy']}]) # one policy frozen fund should remain

        self.log.info("Freezing fund on consensus level...")
        result = self.nodes[0].addToConsensusBlacklist({
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
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Unfreezing all frozen funds")
        result = self.nodes[0].clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 3) # 2 consensus + 1 policy

        result = self.nodes[0].queryBlacklist()
        assert_equal(result["funds"], []) # there should be no frozen funds

        result = self.nodes[0].clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 0) # nothing should be removed when nothing is frozen


if __name__ == '__main__':
    FrozenTXORPCFreezeFunds().main()
