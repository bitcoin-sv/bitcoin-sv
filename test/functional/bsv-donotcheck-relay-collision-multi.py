#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Verify that nocheck transactions already received via relaying are
still prioritized after being received a second time via sendrawtransaction.
This is needed due to MAPI sending the same transaction to multiple nodes via sendrawtransaction
with parameter "donotcheckfee" set to True
"""

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time


# Wait up to 60 secs to see if the testnode has received all the expected invs
def allInvsMatch(invsExpected, testnode):
    for x in range(60):
        with mininode_lock:
            invsrelayed = [e for e in invsExpected if e in testnode.txinvs]
            if invsExpected == invsrelayed:
                return True
        time.sleep(1)
    return False


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.txinvs = []

    def on_inv(self, conn, message):
        for i in message.inv:
            if (i.type == 1):
                self.txinvs.append(hashToHex(i.hash))

    def clear_invs(self):
        with mininode_lock:
            self.txinvs = []


class NoCheckCollisionTest(BitcoinTestFramework):

    def created_signed_transaction(self, sats, node):
        utxo = create_confirmed_utxos(sats, node, 1, age=101)[0]

    def set_test_params(self):
        self.num_nodes = 2
        self.mining_relay_factor = 4
        self.minrelaytxfee_sats = 250
        self.extra_args = [
            [
                "-whitelist=127.0.0.1",
                "-mindebugrejectionfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
                "-minminingtxfee={}".format(Decimal(self.mining_relay_factor * self.minrelaytxfee_sats)/COIN),
            ],
            [
                "-whitelist=127.0.0.1",
                "-mindebugrejectionfee={}".format(Decimal(self.minrelaytxfee_sats) / COIN),
                "-minminingtxfee={}".format(Decimal(self.mining_relay_factor * self.minrelaytxfee_sats)/COIN),
            ]
        ]

    def run_test(self):
        node1 = self.nodes[1]
        node0 = self.nodes[0]
        # Get out of IBD
        node1.generate(1)
        sync_blocks(self.nodes)

        # Setup the p2p connections and start up the network thread.
        test_node = TestNode()
        connection = NodeConn(
            '127.0.0.1', p2p_port(0), node0, test_node)
        test_node.add_connection(connection)
        NetworkThread().start()
        test_node.wait_for_verack()

        # create 4 transactions (dummy, low_fee, high_fee, low_fee_nocheck)
        dummy_index = 0
        low_fee_index = 1
        low_fee_nocheck_index = 2
        high_fee_index = 3
        nb_test_transactions = 4

        utxos = create_confirmed_utxos(Decimal(1000)/Decimal(COIN), node1, nb_test_transactions, age=101)
        sync_blocks(self.nodes)
        test_node.clear_invs()

        relayfee = 0
        signed_txns = []
        txnids = []

        for i in range(nb_test_transactions):
            fee = relayfee
            donotcheck = False
            if i == high_fee_index:
                fee = relayfee * self.mining_relay_factor
            if i == low_fee_nocheck_index:
                donotcheck = True

            inputs = [{"txid": utxos[i]["txid"], "vout": utxos[i]["vout"]}]
            outputs = {node1.getnewaddress(): satoshi_round(utxos[i]['amount'] - fee)}
            rawtx = node1.createrawtransaction(inputs, outputs)
            signed_tx = node1.signrawtransaction(rawtx)["hex"]

            # first signed transaction is only for calculating relayfee
            if i == dummy_index:
                size_in_kilobyte = Decimal(len(signed_tx)) / Decimal(1000)
                relayfeerate = Decimal(self.minrelaytxfee_sats) / 100000000
                relayfee = size_in_kilobyte * relayfeerate
                relayfee += satoshi_round(relayfee / Decimal(10.0)) #size of these txns fluctuates
                signed_txns.append(None) # dummy
                txnids.append(None) # dummy
                continue

            txid = node1.decoderawtransaction(signed_tx)["txid"]
            rejected_txns = node1.sendrawtransactions([{'hex': signed_tx, 'dontcheckfee': donotcheck}])
            txnids.append(txid)
            signed_txns.append(signed_tx)

        # Test that all transactions are relayed (skip dummy tx)
        assert(allInvsMatch(txnids[dummy_index + 1:], test_node))
        test_node.clear_invs()

        # resend low_fee and low_fee_nocheck transactions to node0 via sendrawtransaction even though
        # node0 got those transaction already via relaying
        # if donotcheck is False then transaction must be rejected
        # if donotcheck is True, resending must succeed
        try:
            rejected_txns = node0.sendrawtransactions([{'hex':signed_txns[low_fee_index], 'dontcheckfee':False}]) # last param is donotcheck
        except:
            self.log.info("test 0 - sent twice must be rejected:PASS")

        #low_fee_nocheck_txid_tmp = node0.sendrawtransaction(signed_txns[low_fee_nocheck_index], False, True)  # last param is donotcheck
        low_fee_nocheck_txid_tmp = node0.decoderawtransaction(signed_txns[low_fee_nocheck_index])["txid"]
        rejected_txns = node0.sendrawtransactions([{'hex':signed_txns[low_fee_nocheck_index], 'dontcheckfee':True}])  # last param is donotcheck
        assert (low_fee_nocheck_txid_tmp == txnids[low_fee_nocheck_index])

        # mine transactions in node0 and ensure that the low fee trasnaction with donotcheck equals true
        # is mined because of prioritisation
        node0.generate(1)

        # high fee tx must be minde
        tx = node0.getrawtransaction(txnids[high_fee_index], 1)
        confirmations = tx.get('confirmations', 0)
        assert_equal(confirmations, 1)
        self.log.info("test 1 - high fee tx was minded:PASS")

        # low fee tx must not be minded
        tx = node0.getrawtransaction(txnids[low_fee_index], 1)
        confirmations = tx.get('confirmations', 0)
        assert_equal(confirmations, 0)
        self.log.info("test 2 - low fee tx was not minded:PASS")

        # however, low fee but donotcheck equals true tx must be minded
        tx = node0.getrawtransaction(txnids[low_fee_nocheck_index], 1)
        confirmations = tx.get('confirmations', 0)
        assert_equal(confirmations, 1)
        self.log.info("test 3 - low fee donotcheck tx was minded:PASS")


if __name__ == '__main__':
    NoCheckCollisionTest().main()
