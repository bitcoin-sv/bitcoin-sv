#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Check -acceptp2sh=0 flag which treats transactions with P2SH in output as non-standard (but does not cause disconnection).
Check -acceptp2sh=1 flag which treats transactions with P2SH in output as standard. 
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.mininode import *
from test_framework.script import CScript, OP_HASH160, OP_TRUE, OP_EQUAL, hash160

class BSVDiscourageP2SH(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def assert_rejected_transaction(self, transaction, node, reason):
        def on_reject(conn, msg):
            assert_equal(msg.reason, reason)

        node.on_reject = on_reject
        node.send_message(msg_tx(transaction))
        node.wait_for_reject()

    def makeP2SHTransaction(self):
        # Redeem script is not checked in our case.
        p2sh_script = CScript([OP_HASH160, hash160(CScript([OP_TRUE])), OP_EQUAL])

        p2sh_tx = CTransaction()
        p2sh_tx.vout.append(CTxOut(10000, p2sh_script))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(p2sh_tx),{ 'changePosition' : len(p2sh_tx.vout)})['hex'] 
        ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
        p2sh_tx = FromHex(CTransaction(), ftxHex)
        p2sh_tx.rehash()

        return p2sh_tx

    def run_test(self):
        
        self.nodes[0].generate(101)
        self.stop_node(0)
        
        with self.run_node_with_connections("treat P2SH as non-standard", 0, ['-whitelist=127.0.0.1', '-acceptnonstdtxn=0', '-acceptp2sh=0'], 1) as connections:
            p2sh_tx = self.makeP2SHTransaction()
            self.assert_rejected_transaction(p2sh_tx, connections[0].cb, b'scriptpubkey')
            # check that rejected transaction did not cause disconnection
            connections[0].cb.sync_with_ping()

        with self.run_node_with_connections("treat P2SH as standard", 0, ['-whitelist=127.0.0.1', '-acceptnonstdtxn=0', '-acceptp2sh=1'], 1) as connections:
            p2sh_tx = self.makeP2SHTransaction()
            connections[0].cb.send_message(msg_tx(p2sh_tx))
            connections[0].cb.sync_with_ping()
            wait_until(lambda: len(self.nodes[0].getrawmempool()) == 1)
            assert_equal(set(self.nodes[0].getrawmempool()), {p2sh_tx.hash})

if __name__ == '__main__':
    BSVDiscourageP2SH().main()
