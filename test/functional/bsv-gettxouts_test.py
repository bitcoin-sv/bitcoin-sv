from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import CTransaction, COutPoint, CTxIn, CTxOut,ToHex
from test_framework.script import CScript, OP_TRUE
from test_framework.util import assert_equal, assert_raises_rpc_error


class GettxoutsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self):
        self.add_nodes(self.num_nodes)
        self.start_nodes()

    def run_test(self):
        # Mine blocks to get spendable utxos
        self.nodes[0].generate(103)

        # Check that the first node has 3 utxos
        utxos = self.nodes[0].listunspent()
        assert_equal(len(utxos), 3)

        # Compare gettxouts results to results from gettxout RPC function
        gettxout_results = []
        for i in range(len(utxos)):
            gettxout_results.append(self.nodes[0].gettxout(txid=utxos[i]["txid"],
                                                           n=utxos[i]["vout"],
                                                           include_mempool=True))

        utxos_list = []
        for utxo in utxos:
            utxos_list.append({"txid": utxo["txid"], "n": utxo["vout"]})

        for i in range(len(utxos)):
            gettxouts_res = self.nodes[0].gettxouts(utxos_list[:i+1], ["*"], True)
            # compare values for each result
            for j in range(i+1):
                assert_equal(gettxouts_res["txouts"][j]["confirmations"], gettxout_results[j]["confirmations"])
                assert_equal(gettxouts_res["txouts"][j]["scriptPubKey"], gettxout_results[j]["scriptPubKey"]["hex"])
                assert_equal(gettxouts_res["txouts"][j]["scriptPubKeyLen"], len(gettxout_results[j]["scriptPubKey"]["hex"])/2)
                assert_equal(gettxouts_res["txouts"][j]["value"], gettxout_results[j]["value"])
                assert_equal(gettxouts_res["txouts"][j]["isStandard"], True)  # all transactions above are standard

        # Empty list of txid, n pairs should return empty list
        gettxouts = self.nodes[0].gettxouts([], ["*"], True)
        assert_equal(len(gettxouts["txouts"]), 0)

        # Check various combinations of return types
        gettxouts_res = self.nodes[0].gettxouts([{"txid": utxos[0]["txid"], "n": utxos[0]["vout"]}],
                                                ["scriptPubKey"], True)
        assert_equal(set(gettxouts_res["txouts"][0].keys()), {"scriptPubKey"})

        gettxouts_res = self.nodes[0].gettxouts([{"txid": utxos[0]["txid"], "n": utxos[0]["vout"]}],
                                                ["scriptPubKey", "value", "confirmations"], True)
        assert_equal(set(gettxouts_res["txouts"][0].keys()), {"scriptPubKey", "value", "confirmations"})

        gettxouts_res = self.nodes[0].gettxouts([{"txid": utxos[0]["txid"], "n": utxos[0]["vout"]}],
                                                ["*"], True)

        assert_equal(set(gettxouts_res["txouts"][0].keys()),
                     {"scriptPubKey", "scriptPubKeyLen", "value", "isStandard", "confirmations"})

        assert_raises_rpc_error(
            -32602, "No return fields set",
            self.nodes[0].gettxouts, [{"txid": utxos[0]["txid"], "n": utxos[0]["vout"]}], [], True)

        # TXOs from mempool
        assert_equal(len(self.nodes[0].getrawmempool()), 0)

        # Create, sign and send transaction (from utxos[1])
        spent_utxo = utxos[1] #  utxo that we want to spend
        inputs = []
        outputs = {}
        inputs.append({"txid": spent_utxo["txid"], "vout": spent_utxo["vout"]})
        outputs[self.nodes[0].getnewaddress()] = spent_utxo["amount"] - 3
        raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        signed_tx = self.nodes[0].signrawtransaction(raw_tx)
        self.nodes[0].sendrawtransaction(signed_tx["hex"], True)

        # new transaction should appear in the mempool
        assert_equal(len(self.nodes[0].getrawmempool()), 1)
        new_utxo_txid = self.nodes[0].getrawmempool()[0]
        spent_utxo_txid = spent_utxo["txid"]

        # Check if new_utxo_txid which is in mempool is discovered for mempool=True and not for mempool=Flase
        gettxouts_res = self.nodes[0].gettxouts([{"txid": new_utxo_txid, "n": 0}], ["*"], False)
        assert_equal(gettxouts_res["txouts"], [{'error': 'missing'}])

        gettxouts_res = self.nodes[0].gettxouts([{"txid": new_utxo_txid, "n": 0}], ["*"], True)
        assert_equal(set(gettxouts_res["txouts"][0].keys()),
                     {"scriptPubKey", "scriptPubKeyLen", "value", "isStandard", "confirmations"})

        # Check if spent_utxo_txid which is spent, but transaction that spends it is in mempool (and not in block)
        gettxouts_res = self.nodes[0].gettxouts([{"txid": spent_utxo_txid, "n": 0}], ["*"], False)
        assert_equal(set(gettxouts_res["txouts"][0].keys()),
                     {"scriptPubKey", "scriptPubKeyLen", "value", "isStandard", "confirmations"})

        gettxouts_res = self.nodes[0].gettxouts([{"txid": spent_utxo_txid, "n": 0}], ["*"], True)
        assert_equal(gettxouts_res["txouts"][0]["error"], "spent")
        assert_equal(gettxouts_res["txouts"][0]["collidedWith"]["txid"], new_utxo_txid)

        # Check for multiple errors (missing, spent) and utxo that we can obtain
        gettxouts_res = self.nodes[0].gettxouts([{"txid": spent_utxo_txid, "n": 0}, {"txid": "abc", "n": 0},
                                                 {"txid": utxos[2]["txid"], "n": utxos[2]["vout"]}], ["*"], True)
        assert_equal(gettxouts_res["txouts"][0]["error"], "spent")
        assert_equal(gettxouts_res["txouts"][1]["error"], "missing")
        assert_equal(gettxouts_res["txouts"][2].keys(),
                     {"scriptPubKey", "scriptPubKeyLen", "value", "isStandard", "confirmations"})

        # Check that txouts returns only first hex of transactions if the same transaction appears multiple times in collidedWith
        gettxouts_res = self.nodes[0].gettxouts([{"txid": spent_utxo_txid, "n": 0}, {"txid": spent_utxo_txid, "n": 0}],
                                                ["*"], True)
        assert_equal(len(gettxouts_res["txouts"]), 2)
        assert_equal(gettxouts_res["txouts"][0]["collidedWith"]["txid"], gettxouts_res["txouts"][1]["collidedWith"]["txid"])
        assert_equal(gettxouts_res["txouts"][0]["collidedWith"].keys(), {"txid", "size", "hex"})
        assert_equal(gettxouts_res["txouts"][1]["collidedWith"].keys(), {"txid", "size"})

        # now generate block - transaction with txid: new_utxo_txid is now in block
        # it should be returned regardles of include_mempool parameter value
        self.nodes[0].generate(1)
        gettxouts_res = self.nodes[0].gettxouts([{"txid": new_utxo_txid, "n": 0}], ["*"], False)
        assert_equal(set(gettxouts_res["txouts"][0].keys()),
                     {"scriptPubKey", "scriptPubKeyLen", "value", "isStandard", "confirmations"})

        gettxouts_res = self.nodes[0].gettxouts([{"txid": new_utxo_txid, "n": 0}], ["*"], True)
        assert_equal(set(gettxouts_res["txouts"][0].keys()),
                     {"scriptPubKey", "scriptPubKeyLen", "value", "isStandard", "confirmations"})

        # It should not be found after it is spent
        gettxouts_res = self.nodes[0].gettxouts([{"txid": spent_utxo_txid, "n": 0}], ["*"], False)
        assert_equal(gettxouts_res["txouts"], [{'error': 'missing'}])
        gettxouts_res = self.nodes[0].gettxouts([{"txid": spent_utxo_txid, "n": 0}], ["*"], True)
        assert_equal(gettxouts_res["txouts"], [{'error': 'missing'}])

        # Invalid TXOs (incorrect syntax on input)
        assert_raises_rpc_error(
            -32602, "Wrong format. Exactly \"txid\" and \"n\" are required fields.",
            self.nodes[0].gettxouts, [{"abc": utxos[0]["txid"], "n": utxos[0]["vout"]}], ["*"], True)

        assert_raises_rpc_error(
            -32602, "Wrong format. Exactly \"txid\" and \"n\" are required fields.",
            self.nodes[0].gettxouts, [{"txid": utxos[0]["txid"], "abc": utxos[0]["vout"]}], ["*"], True)

        assert_raises_rpc_error(
            -32602, "Wrong format. Exactly \"txid\" and \"n\" are required fields.",
            self.nodes[0].gettxouts, [{"txid": utxos[0]["txid"]}], ["*"], True)

        assert_raises_rpc_error(
            -32602, "Wrong format. Exactly \"txid\" and \"n\" are required fields.",
            self.nodes[0].gettxouts, [{"n": utxos[0]["vout"]}], ["*"], True)

        assert_raises_rpc_error(
            -32602, "Wrong format. Exactly \"txid\" and \"n\" are required fields.",
            self.nodes[0].gettxouts, [{utxos[0]["txid"]: utxos[0]["vout"]}], ["*"], True)

        assert_raises_rpc_error(
            -32602, "Wrong format. Exactly \"txid\" and \"n\" are required fields.",
            self.nodes[0].gettxouts, [{}], ["*"], True)
        assert_raises_rpc_error(
            -32602, "\"*\" should not be used with other return fields",
            self.nodes[0].gettxouts, [{"abc": utxos[0]["txid"], "n": utxos[0]["vout"]}], ["*", "value"], True)

        assert_raises_rpc_error(
            -1, "JSON value is not an object as expected",
            self.nodes[0].gettxouts, [utxos[0]["txid"]], ["*"], True)

        assert_raises_rpc_error(
            -1, "JSON value is not an object as expected",
            self.nodes[0].gettxouts, [0], ["*"], True)

        assert_raises_rpc_error(
            -1, "JSON value is not an object as expected",
            self.nodes[0].gettxouts, [None], ["*"], True)

        # Missing/non-existing TXOs
        gettxouts_res = self.nodes[0].gettxouts([{"txid": "abc", "n": utxos[0]["vout"]}], ["*"], True)
        assert_equal(gettxouts_res["txouts"], [{'error': 'missing'}])

        gettxouts_res = self.nodes[0].gettxouts([{"txid": utxos[0]["txid"], "n": len(utxos[0]) + 1}], ["*"], True)
        assert_equal(gettxouts_res["txouts"], [{'error': 'missing'}])

        # Check with non hex string for txid
        gettxouts_res = self.nodes[0].gettxouts([{"txid": "z._", "n": utxos[0]["vout"]}], ["*"], True)
        assert_equal(gettxouts_res["txouts"], [{'error': 'missing'}])

        # check non standard transaction
        utxo = self.nodes[0].listunspent()[0]
        tx1 = CTransaction()
        tx1.vout = [CTxOut(450000, CScript([OP_TRUE]))]  # This is not a standard transactions
        tx1.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), 0))]
        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx1))['hex']
        self.nodes[0].sendrawtransaction(tx_hex, True)
        assert_equal(len(self.nodes[0].getrawmempool()), 1)
        new_tx = self.nodes[0].getrawmempool()[0]

        gettxouts_res = self.nodes[0].gettxouts([{"txid": new_tx, "n": 0}], ["*"], True)
        assert_equal(gettxouts_res["txouts"][0]["isStandard"], False)


if __name__ == '__main__':
    GettxoutsTest().main()
