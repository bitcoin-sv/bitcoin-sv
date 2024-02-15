#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import create_block, create_coinbase, create_transaction
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, COIN
from test_framework.script import CScript, OP_DROP, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_mempool_equals
from decimal import Decimal


class Cpfp(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def create_tx(self, outpoints, noutput, feerate):
        tx = CTransaction()
        total_input = 0
        for parent_tx, n in outpoints:
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, n), b"", 0xffffffff))
            total_input += parent_tx.vout[n].nValue

        for _ in range(noutput):
            tx.vout.append(CTxOut(total_input//noutput, CScript([b"X"*200, OP_DROP, OP_TRUE])))

        tx.rehash()

        tx_size = len(tx.serialize())
        fee_per_output = int(tx_size * feerate // noutput)

        for output in tx.vout:
            output.nValue -= fee_per_output

        tx.rehash()
        return tx

    def run_test(self):
        with self.run_node_with_connections("Scenario 1: Low fee, non-whitelisted peer", 0, ["-minminingtxfee=0.00001", "-mindebugrejectionfee=0.00000250"],
                                            number_of_connections=1) as (conn,):

            mining_fee = 1.01 # in satoshi per byte
            relayfee = float(Decimal("0.00000250") * COIN / 1000) + 0.01  # in satoshi per byte

            # create block with coinbase
            coinbase = create_coinbase(height=1)
            first_block = create_block(int(conn.rpc.getbestblockhash(), 16), coinbase=coinbase)
            first_block.solve()
            conn.send_message(msg_block(first_block))
            wait_until(lambda: conn.rpc.getbestblockhash() == first_block.hash, check_interval=0.3)

            #mature the coinbase
            conn.rpc.generate(150)

            funding_tx = self.create_tx([(coinbase, 0)], 10, 1.5)

            conn.send_message(msg_tx(funding_tx))
            check_mempool_equals(conn.rpc, [funding_tx])
            conn.rpc.generate(1)

            last_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
            block = create_block(int(last_block_info["hash"], 16), coinbase=create_coinbase(height=last_block_info["height"] + 1), nTime=last_block_info["time"] + 1)
            low_fee_tx = self.create_tx([(funding_tx, 0)], 2, relayfee)
            block.vtx.append(low_fee_tx)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.calc_sha256()
            block.solve()

            conn.send_message(msg_block(block))
            wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, check_interval=0.3)

            tx_pays_relay1 = self.create_tx([(low_fee_tx,     0)], 2, relayfee)
            tx_pays_relay2 = self.create_tx([(tx_pays_relay1, 0)], 1, relayfee)
            tx_pays_enough_for_itself = self.create_tx([(tx_pays_relay1, 1)], 1, mining_fee)
            tx_pays_for_ancestors = self.create_tx([(tx_pays_relay2, 0)], 1, 3.5 * mining_fee)
            tx_pays_relay3 = self.create_tx([(tx_pays_for_ancestors, 0)], 1, relayfee)

            conn.send_message(msg_tx(tx_pays_relay1))
            check_mempool_equals(conn.rpc, [tx_pays_relay1])
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 1) #"should be coinbase only"

            conn.send_message(msg_tx(tx_pays_relay2))
            check_mempool_equals(conn.rpc, [tx_pays_relay1, tx_pays_relay2])
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 1) #"should be coinbase only"

            conn.send_message(msg_tx(tx_pays_enough_for_itself))
            check_mempool_equals(conn.rpc, [tx_pays_relay1, tx_pays_relay2, tx_pays_enough_for_itself])
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 1) #"should be coinbase only"

            #try  to rebuild journal, mining candidate should stay the same
            conn.rpc.rebuildjournal()
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 1)

            # this will trigger cpfp for two unpaying antcestors (tx_pays_relay1 and tx_pays_relay1) then
            # after that tx_pays_enough_for_itself will be free of ancestor debt and it will be accepted also
            conn.send_message(msg_tx(tx_pays_for_ancestors))
            check_mempool_equals(conn.rpc, [tx_pays_relay1, tx_pays_relay2, tx_pays_enough_for_itself, tx_pays_for_ancestors])
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 5), #"all tx plus coinbase"

            # still, non paying child of the tx_pays_for_ancestors will not be accepted
            conn.send_message(msg_tx(tx_pays_relay3))
            check_mempool_equals(conn.rpc, [tx_pays_relay1, tx_pays_relay2, tx_pays_enough_for_itself, tx_pays_for_ancestors, tx_pays_relay3])
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 5), #"all tx, except tx_pays_relay3 plus coinbase"

            #try  to rebuild journal, mining candidate should stay the same
            conn.rpc.rebuildjournal()
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 5)

            # we will mine a new block, all transactions from the journal will end up in new block, mempool will contain
            # only tx_pays_relay3
            conn.rpc.generate(1)
            check_mempool_equals(conn.rpc, [tx_pays_relay3])

            # now we invalidate block two blocks, at this point tx_pays_for_ancestors does not pay enough for
            # all non-paying children, nothing will end up in the journal.
            # non paying children are: low_fee_tx, tx_pays_relay1, tx_pays_relay2
            conn.rpc.invalidateblock(block.hash)
            check_mempool_equals(conn.rpc, [low_fee_tx, tx_pays_relay1, tx_pays_relay2, tx_pays_enough_for_itself, tx_pays_for_ancestors, tx_pays_relay3])
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 1) #"should be coinbase only"

            # when we reconsider invalidate block, everything should be the same
            conn.rpc.reconsiderblock(block.hash)
            check_mempool_equals(conn.rpc, [tx_pays_relay3])
            wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 1) #"should be coinbase only"


if __name__ == '__main__':
    Cpfp().main()
