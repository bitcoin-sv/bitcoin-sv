#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
import time
import decimal
from test_framework.cdefs import DEFAULT_MAX_STD_TXN_VALIDATION_DURATION

'''
Test if consolidation transactions pass the feefilter
'''
# For Release build with sanitizers enabled (TSAN / ASAN / UBSAN), recommended timeoutfactor is 4.
# For Debug build, recommended timeoutfactor is 4.
# For Debug build with sanitizers enabled, recommended timeoutfactor is 5.


def getInputScriptPubKey(node, input, index):
    txid = hashToHex(input.prevout.hash)
    raw = node.getrawtransaction(txid)
    tx = FromHex(CTransaction(), raw)
    tx.rehash()
    return tx.vout[index].scriptPubKey


def expectedInvsReceived(invsExpected, testnode, timeout = 60):
    expectedSet = set(invsExpected)
    for x in range(timeout):
        with mininode_lock:
            if expectedSet.issubset(testnode.txinvs):
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


class FeeFilterTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        #self.setup_clean_chain = True
        self.utxo_test_sats = 10000
        self.utxo_test_bsvs = satoshi_round(self.utxo_test_sats / COIN)
        self.blockmintxfee_sats = 500
        self.minrelaytxfee_sats = 250

    def setup_nodes(self):
        self.extra_args = [
            [
                "-whitelist=127.0.0.1",
                "-whitelistforcerelay=1"
                "-mindebugrejectionfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
                "-minminingtxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
                "-minconsolidationfactor=10",
                "-acceptnonstdtxn=1",
                "-maxstdtxvalidationduration=1",  # enable this setting to more reproducibly fail with old node
                "-txindex=1"
            ],
            [
                "-whitelist=127.0.0.1",
                "-whitelistforcerelay=1"
                "-mindebugrejectionfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
                "-minminingtxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
                "-minconsolidationfactor=10",
                "-acceptnonstdtxn=1",
                "-maxstdtxvalidationduration=1",  # enable this setting to more reproducibly fail with old node
                "-txindex=1"
            ]
        ]

        self.add_nodes(self.num_nodes, self.extra_args)
        self.start_nodes()

    def create_utxos_value10000(self, node, utxo_count, min_confirmations):

        utxos = []
        addr = node.getnewaddress()
        for i in range (utxo_count):
            txid = node.sendtoaddress(addr, self.utxo_test_bsvs)
            tx = FromHex(CTransaction(), node.getrawtransaction(txid))
            tx.rehash()
            utxos.append(tx)

        if min_confirmations > 0:
            node.generate(min_confirmations)
            sync_blocks(self.nodes)

        return utxos

    def create_and_sign_tx(self, node, in_count, min_confirmations):
        utxos = self.create_utxos_value10000 (node, in_count, min_confirmations)
        inputs = []
        sum_values_bsvs = 0
        for u in utxos:
            for i in range(len(u.vout)):
                if u.vout[i].nValue == self.utxo_test_sats:
                    sum_values_bsvs = sum_values_bsvs + self.utxo_test_bsvs
                    inputs.append({"txid": u.hash, "vout": i})

        assert (len(utxos) == in_count)
        assert (sum_values_bsvs == in_count * self.utxo_test_bsvs)

        addr = node.getnewaddress()
        outputs = {}
        outputs[addr] = sum_values_bsvs

        rawtx = node.createrawtransaction(inputs, outputs)
        return node.signrawtransaction(rawtx)['hex']

    def run_test(self):
        node1 = self.nodes[1]
        node0 = self.nodes[0]

        # Get out of IBD
        node1.generate(300)
        sync_blocks(self.nodes)

        ## BEGIN setup consolidation transactions
        self.consolidation_factor = int(node1.getnetworkinfo()['minconsolidationfactor'])
        self.minConfirmations = int(node1.getnetworkinfo()['minconfconsolidationinput'])
        # test ratio between size of input script and size of output script
        tx_hex = self.create_and_sign_tx(node1, 1, min_confirmations=self.minConfirmations)
        tx = FromHex(CTransaction(), tx_hex)
        tx.rehash()
        sin = len(getInputScriptPubKey(node1, tx.vin[0], 0))
        sout = len(tx.vout[0].scriptPubKey)

        enough_inputs = sout * self.consolidation_factor // sin
        enough_inputs = max(enough_inputs, 2)
        enough_confirmations = self.minConfirmations
        # END setup consolidation transactions
        sync_blocks(self.nodes)

        # Setup the p2p connections and start up the network thread.
        test_node = TestNode()
        connection = NodeConn(
            '127.0.0.1', p2p_port(0), node0, test_node)
        test_node.add_connection(connection)

        NetworkThread().start()
        test_node.wait_for_verack()

        tx_hex = self.create_and_sign_tx(node1, in_count=enough_inputs, min_confirmations=enough_confirmations)
        tx_hex3 = self.create_and_sign_tx(node1, in_count=enough_inputs, min_confirmations=enough_confirmations)

        sync_blocks(self.nodes)
        sync_mempools(self.nodes)

        test_node.clear_invs()

        # Consolidation transaction will be relayed,
        # as the modified fees are set to minminingtxfee >= feefilter
        test_node.send_and_ping(msg_feefilter(self.blockmintxfee_sats))

        # Send consolidation and regular tx - both should be accepted and relayed
        txid1 = node1.sendrawtransaction(tx_hex)
        txid2 = node1.sendtoaddress(node1.getnewaddress(), 1)

        wait_until(lambda: txid1 in node0.getrawmempool(), timeout=5)
        wait_until(lambda: txid2 in node0.getrawmempool(), timeout=5)

        # Check that tx1 and tx2 were relayed to test_node
        assert(expectedInvsReceived([txid1, txid2], test_node, 60))

        # Now the feefilter is set to minminingtxfee+1;
        # tx3 is not relayed as modified fees < feefilter
        # tx4 is relayed, as node1's txfee is set high enough - control tx

        test_node.send_and_ping(msg_feefilter(self.blockmintxfee_sats+1))
        test_node.clear_invs()

        txid3 = node1.sendrawtransaction(tx_hex3)
        txid4 = node1.sendtoaddress(node1.getnewaddress(), 1)

        wait_until(lambda: txid3 in node0.getrawmempool(), timeout=5)
        wait_until(lambda: txid4 in node0.getrawmempool(), timeout=5)

        # Check that tx3 was not relayed to test_node but tx4 was
        assert(expectedInvsReceived([txid4], test_node, 60))
        assert(not expectedInvsReceived([txid3], test_node, 5))


if __name__ == '__main__':
    FeeFilterTest().main()
