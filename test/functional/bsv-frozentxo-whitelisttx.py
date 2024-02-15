#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check RPC functions used to whitelist confiscation transactions
"""
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut, ToHex
from test_framework.script import CScript, OP_FALSE, OP_RETURN, OP_TRUE, hash160
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class FrozenTXORPCWhitelistTx (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def _create_confiscation_tx(self, txo):
        ctx = CTransaction()

        ctx.vin.append(CTxIn(COutPoint(int(txo["txId"], 16), txo["vout"]), b'', 0xffffffff))

        # OP_RETURN output that makes this a confiscation transaction
        ctx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN, b'cftx'] + [
            b'\x01' +                       # protocol version number
            hash160(b'confiscation order2') # hash of confiscation order document
        ])))
        ctx.vout.append(CTxOut(42, CScript([OP_TRUE])))

        ctx.calc_sha256()
        return ctx

    def run_test(self):
        self.log.info("Querying whitelisted transactions to check that initially there are none")
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        assert_equal(result["confiscationTxs"], [])

        txo1 = {"txId" : "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee", "vout" : 0}
        txo2 = {"txId" : "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", "vout" : 0}
        txo3 = {"txId" : "fefefefefefefefefefefeefefefefefefefefeffefefefefefefefefefefefe", "vout" : 2}

        # Valid confiscation transactions used by this test
        # NOTE: Checking whitelisting of invalid confiscation transactions is done in test 'bsv-frozentxo-confiscation.py'.
        ctx1 = self._create_confiscation_tx(txo1)
        ctx2 = self._create_confiscation_tx(txo2)
        ctx3 = self._create_confiscation_tx(txo3)

        self.log.info("Checking that transactions cannot be whitelisted if inputs are not frozen")
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 123,
                        "hex": ToHex(ctx1)
                    }
                },
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 456,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [
            {'confiscationTx': {'txId': ctx1.hash}, 'reason': 'confiscated TXO is not consensus frozen'},
            {'confiscationTx': {'txId': ctx2.hash}, 'reason': 'confiscated TXO is not consensus frozen'}
        ])

        self.log.info("Freezing some funds...")
        result = self.nodes[0].addToPolicyBlacklist({"funds": [{"txOut" : txo3}]})
        assert_equal(result["notProcessed"], [])
        result = self.nodes[0].addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : txo1,
                    "enforceAtHeight": [{"start": 100}],
                    "policyExpiresWithConsensus": False
                },
                {
                    "txOut" : txo2,
                    "enforceAtHeight": [{"start": 456, "stop": 457}],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Checking that transactions cannot be whitelisted if inputs are not consensus frozen at specific height")
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 50, # height too low
                        "hex": ToHex(ctx1)
                    }
                },
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 457, # height too high
                        "hex": ToHex(ctx2)
                    }
                },
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 100,
                        "hex": ToHex(ctx3) # not consensus frozen
                    }
                }]
        })
        assert_equal(result["notProcessed"], [
            {'confiscationTx': {'txId': ctx1.hash}, 'reason': 'confiscated TXO is not consensus frozen'},
            {'confiscationTx': {'txId': ctx2.hash}, 'reason': 'confiscated TXO is not consensus frozen'},
            {'confiscationTx': {'txId': ctx3.hash}, 'reason': 'confiscated TXO is not consensus frozen'}
        ])

        self.log.info("Whitelisting transactions...")
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 123,
                        "hex": ToHex(ctx1)
                    }
                },
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 456,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Querying whitelisted transactions and checking there are 2")
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        assert_equal(len(result["confiscationTxs"]), 2) # there should be 2
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"]) # must be sorted since order in result is unspecified
        assert_equal(wltxs[0], {"confiscationTx" : {"txId" : ctx1.hash, "enforceAtHeight" : 123}})
        assert_equal(wltxs[1], {"confiscationTx" : {"txId" : ctx2.hash, "enforceAtHeight" : 456}})

        wltxs0 = wltxs

        self.log.info("Querying whitelisted transactions in verbose mode")
        result = self.nodes[0].queryConfiscationTxidWhitelist(True)
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"]) # must be sorted since order in result is unspecified
        assert_equal(wltxs[0], {"confiscationTx" : {"txId" : ctx1.hash, "enforceAtHeight" : 123, "inputs" : [{"txOut" : txo1}]}})
        assert_equal(wltxs[1], {"confiscationTx" : {"txId" : ctx2.hash, "enforceAtHeight" : 456, "inputs" : [{"txOut" : txo2}]}})

        self.log.info("Restarting node...")
        self.restart_node(0)
        self.log.info("Checking that list of whitelisted transactions remained the same")
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"])
        assert_equal(wltxs, wltxs0)

        self.log.info("Whitelisting same transactions again should have no effect")
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 123,
                        "hex": ToHex(ctx1)
                    }
                },
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 456,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Trying to increase enforceAtHeight on whitelisted tx should have no effect")
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 789,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Checking that list of whitelisted transactions remained the same")
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"])
        assert_equal(wltxs, wltxs0)

        self.log.info("Decreasing enforceAtHeight on whitelisted tx should be allowed")
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 200,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])

        self.log.info("Checking that enforceAtHeight was updated in list of whitelisted transactions")
        wltxs0[1]["confiscationTx"]["enforceAtHeight"] = 200
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"])
        assert_equal(wltxs, wltxs0)

        self.log.info("Clearing blacklists with removeAllEntries=false should not affect whitelisted txs")
        result = self.nodes[0].clearBlacklists({"removeAllEntries" : False, "expirationHeightDelta": 0})
        assert_equal(result["numRemovedEntries"], 0)
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"])
        assert_equal(wltxs, wltxs0)

        self.log.info("Clearing confiscation whitelist should remove all whitelisted txs")
        result = self.nodes[0].clearConfiscationWhitelist()
        assert_equal(result["numFrozenBackToConsensus"], 2)
        assert_equal(result["numUnwhitelistedTxs"], 2)
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        assert_equal(result["confiscationTxs"], [])

        self.log.info("Whitelisting same transactions again")
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 123,
                        "hex": ToHex(ctx1)
                    }
                },
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 456,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 200,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"])
        assert_equal(wltxs, wltxs0)

        self.log.info("Clearing blacklists with removeAllEntries=true,keepExistingPolicyEntries=true should remove all whitelisted txs")
        result = self.nodes[0].clearBlacklists({"removeAllEntries" : True, "keepExistingPolicyEntries": True})
        assert_equal(result["numRemovedEntries"], 4) # 2 CTXs + 2 consensus frozen TXOs
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        assert_equal(result["confiscationTxs"], [])

        self.log.info("Freezing TXOs and whitelisting same transactions again")
        result = self.nodes[0].addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : txo1,
                    "enforceAtHeight": [{"start": 100}],
                    "policyExpiresWithConsensus": False
                },
                {
                    "txOut" : txo2,
                    "enforceAtHeight": [{"start": 456, "stop": 457}],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 123,
                        "hex": ToHex(ctx1)
                    }
                },
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 456,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])
        result = self.nodes[0].addToConfiscationTxidWhitelist({
            "confiscationTxs": [
                {
                    "confiscationTx" : {
                        "enforceAtHeight" : 200,
                        "hex": ToHex(ctx2)
                    }
                }]
        })
        assert_equal(result["notProcessed"], [])
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        wltxs = sorted(result["confiscationTxs"], key=lambda f: f["confiscationTx"]["txId"])
        assert_equal(wltxs, wltxs0)

        self.log.info("Clearing blacklists with removeAllEntries=true should remove all whitelisted txs")
        result = self.nodes[0].clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 5) # 2 CTXs + 3 TXOs
        result = self.nodes[0].queryConfiscationTxidWhitelist()
        assert_equal(result["confiscationTxs"], [])


if __name__ == '__main__':
    FrozenTXORPCWhitelistTx().main()
