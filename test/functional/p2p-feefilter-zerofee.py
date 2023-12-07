#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.mininode import NodeConnCB, NodeConn, NetworkThread, msg_feefilter, mininode_lock
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import sync_blocks, p2p_port, wait_until, hashToHex
import time

'''
FeeFilterTest -- test relaying zero fee paying transactions when the
minimum mining fee is also set to zero and the relaying fee is left at default (also zero)
'''


def send_zero_fee_tx_funded_from(node):
    tx_id = node.sendtoaddress(node.getnewaddress(), 1)
    tx = node.getrawtransaction(tx_id, 1)
    n = -1

    for vout in tx['vout']:
        val = vout['value']
        if val > 0.999 and val < 1.001:
            n = vout['n']
            break

    if n > -1:
        inputs = [{'txid': tx_id, 'vout': n}]
        outputs = {node.getnewaddress(): val}
        rawtx = node.createrawtransaction(inputs, outputs)
        signed = node.signrawtransaction(rawtx)
        return node.sendrawtransaction(signed["hex"])


# Wait up to 60 secs to see if the testnode has received all the expected invs
def allInvsMatch(invsExpected, testnode):
    for x in range(5):
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


class FeeFilterTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def setup_nodes(self):
        self.extra_args = [[],[]]
        self.add_nodes(self.num_nodes, self.extra_args)
        self.start_nodes()

    def run_test(self):
        node1 = self.nodes[1]
        node0 = self.nodes[0]
        # Get out of IBD
        node1.generate(1)
        sync_blocks(self.nodes)

        # Setup the p2p connections and start up the network thread.
        test_node = TestNode()
        connection = NodeConn(
            '127.0.0.1', p2p_port(0), self.nodes[0], test_node)
        test_node.add_connection(connection)
        NetworkThread().start()
        test_node.wait_for_verack()

        # Test that invs are received for all txs even if they pay zero fees
        test_node.send_and_ping(msg_feefilter(0))

        test_node.clear_invs()
        zero_txids = [send_zero_fee_tx_funded_from (node1) for _ in range(3)]
        wait_until(lambda: {t for t in zero_txids}.issubset(set(node0.getrawmempool())), timeout=10)
        assert (allInvsMatch(zero_txids, test_node))
        test_node.clear_invs()

        # Test that invs are NOT received for all txs if they pay zero fees and the feefiler
        # is not set to zero
        test_node.send_and_ping(msg_feefilter(100))

        test_node.clear_invs()
        zero_txids = [send_zero_fee_tx_funded_from (node1) for _ in range(3)]
        wait_until(lambda: {t for t in zero_txids}.issubset(set(node0.getrawmempool())), timeout=10)
        assert (not allInvsMatch(zero_txids, test_node))
        test_node.clear_invs()


if __name__ == '__main__':
    FeeFilterTest().main()
