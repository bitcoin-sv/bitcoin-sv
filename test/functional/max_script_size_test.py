#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from math import floor
from time import sleep

from test_framework.blocktools import calc_needed_data_size
from test_framework.cdefs import MAX_SCRIPT_SIZE_BEFORE_GENESIS
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.comptool import RejectResult
from test_framework.mininode import *
from test_framework.util import *
from test_framework.script import OP_CHECKSIG, OP_FALSE, OP_RETURN, CScript, OP_EQUALVERIFY, OP_HASH160, OP_DUP, OP_TRUE, OP_DROP
from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import create_transaction

CHUNK_SIZE=len(CScript([b"a" * 500]))


class TestNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.txinvs = []

    def on_inv(self, conn, message):
        for i in message.inv:
            if (i.type == 1):
                self.txinvs.append(format(i.hash, '064x'))

    def clear_invs(self):
        with mininode_lock:
            self.txinvs = []


class MaxScriptSizeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.genesisactivationheight = 209
        self.maxscriptsize = ONE_MEGABYTE - 200
        self.setup_clean_chain = True
        self.extra_args = [['-genesisactivationheight=%d' % self.genesisactivationheight, "-maxscriptsizepolicy=%d" % self.maxscriptsize, "-maxopsperscriptpolicy=5000", '-banscore=10000000']]

    def setup_network(self):
        self.setup_nodes()

    def run_test_node(self, node_index=0, dstaddr='127.0.0.1', dstportno=0, num_of_connections=1):
        test_node = TestNode()
        conn = NodeConn(dstaddr, p2p_port(dstportno), self.nodes[node_index], test_node)
        test_node.add_connection(conn)
        return test_node,conn

    def run_test(self):
        def new_tx(utxo=None, target_script_size=20000, op_codes=[OP_TRUE], elem=[b"a" * 499, OP_DROP], target_tx_size=None, simple=False, lock_script=None, unlock_script=b""):
            if utxo != None:
                tx_to_spend = utxo['txid']
                vout = utxo['vout']
                value = utxo['amount']

            if simple:
                tx =create_transaction(tx_to_spend, vout, unlock_script, value - target_script_size - 2000)
                tx.rehash()
                return tx

            tx = CTransaction()
            if(lock_script != None):
                tx.vout.append(CTxOut(200000000, lock_script))
            else:
                script = make_script(op_codes=op_codes, elem=elem, target_script_size=target_script_size)
                tx.vout.append(CTxOut(200000000, script))
            tx.vout.append(CTxOut(200000000, CScript([OP_DUP, OP_HASH160,
                                                      hex_str_to_bytes(
                                                          "ab812dc588ca9d5787dde7eb29569da63c3a238c"),
                                                      OP_EQUALVERIFY,
                                                      OP_CHECKSIG])))
            if target_tx_size != None:
                padding_size = calc_needed_data_size(script, target_tx_size)
                tx.vout.append(CTxOut(50000000, CScript([OP_FALSE, OP_RETURN] + [bytes(5)[:1] * padding_size])))
            if utxo == None:
                txHex = node.fundrawtransaction(ToHex(tx), {'feeRate': 2, 'changePosition': len(tx.vout)})[
                    'hex']
            else:
                tx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(tx_to_spend)[::-1]), vout)))
                txHex = ToHex(tx)
            txHex = node.signrawtransaction(txHex)['hex']
            tx = FromHex(CTransaction(), txHex)

            tx.rehash()
            return tx

        def make_script(op_codes=[OP_TRUE], elem=[b"a" * 499, OP_DROP], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS):
            elem_len = len(CScript(elem))
            max_size = calc_needed_data_size(op_codes, target_script_size)
            remainder = (max_size - len(CScript(op_codes + elem * (int)(floor(max_size / elem_len)))))
            script = CScript(op_codes + elem * (int)(floor(max_size / elem_len)) + [b"a" * remainder, OP_DROP])

            if len(script) > target_script_size:
                remainder -= (len(script) - target_script_size)
                script = CScript(op_codes + elem * (int)(floor(max_size / elem_len)) + elem)
            return script

        def make_unlock_script(elem=[b"a" * 500], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS):
            elem_len = len(CScript(elem))
            max_size = calc_needed_data_size([], (target_script_size))
            remainder = (max_size - len(CScript(elem * (int)(floor(max_size / elem_len)))))

            script = CScript([] + elem * (int)(floor(max_size / elem_len)) + [b"a" * remainder, b""])

            if len(script) > target_script_size:
                remainder -= (len(script) - target_script_size)
                script = CScript([] + elem * (int)(floor(max_size / elem_len)) + [b"a" * remainder, b""])
            return script

        def add_to_block_and_send(txs=None, utxo=None, i_utxo=0, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS, target_tx_size=None, len_mem0=1, len_mem1=0, valid=None):
            if txs == None:
                if utxo == None:
                    tx = new_tx(utxo=utxos[i_utxo], target_script_size=target_script_size, target_tx_size=target_tx_size)
                else:
                    tx = new_tx(utxo=utxo, target_script_size=target_script_size, target_tx_size=target_tx_size)
                txs = [tx]
            for tx in txs:
                conn.send_message(msg_tx(tx))

            if valid == None:
                valid = txs
            check_mempool_equals(node, valid)
            mempool0 = node.getrawmempool()
            block_id = node.generate(1)
            mempool1 = node.getrawmempool()

            assert len(mempool0) == len_mem0, "There is/are " + str(len(mempool0))+ " transaction(s) in mempool before the latest block was generated. Should be " + str(len_mem0)
            assert len(mempool1) == len_mem1, "There is/are " + str(len(mempool1))+ " transaction(s) in mempool after the latest block was generated. Should be " + str(len_mem1)
            if len_mem0 > 0:
                return block_id, mempool0[0]
            else:
                return None,None

        node = self.nodes[0]
        test_node, conn = self.run_test_node(0)
        rejected_txs = []

        def on_reject(conn, msg):
            rejected_txs.append(msg)

        conn.cb.on_reject = on_reject

        thr = NetworkThread()
        thr.start()
        sleep(1)
        test_node.wait_for_verack()
        hashes = node.generate(199)
        utxos = node.listunspent()

        # Create a tx with script size MAX_SCRIPT_SIZE_BEFORE_GENESIS, send it via p2p and mine it into a block
        base_tx = new_tx(utxos[0], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS)
        add_to_block_and_send(txs=[base_tx])
        # Create a test tx that'll try to spend it      --> OK
        utxo = {'txid':base_tx, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS)
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)

        # Create a tx with script size MAX_SCRIPT_SIZE_BEFORE_GENESIS+1,
        # Check that it doesn't get into the mempool, because parent is unspendable
        base_tx = new_tx(utxos[1], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS+1)
        add_to_block_and_send(txs=[base_tx])
        utxo = {'txid':base_tx, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS+1)
        add_to_block_and_send(txs=[test_tx], len_mem0=0, valid=[])
        ensure_no_rejection(conn)

        # Create 2 more txes with script size MAX_SCRIPT_SIZE_BEFORE_GENESIS and MAX_SCRIPT_SIZE_BEFORE_GENESIS+1,
        # send them via p2p and mine into a block
        tx1 = new_tx(utxos[2], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS)
        tx2 = new_tx(utxos[3], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS + 1)
        add_to_block_and_send(txs=[tx1, tx2], len_mem0=2)

        # Create 4 txs with small lock scripts, to test large unlock scripts
        tx3 = new_tx(utxos[8], target_script_size=500, lock_script=CScript([OP_DROP] * ((int)(floor(MAX_SCRIPT_SIZE_BEFORE_GENESIS / CHUNK_SIZE + 2))) + [OP_TRUE]))
        tx4 = new_tx(utxos[9], target_script_size=500, lock_script=CScript([OP_DROP] * ((int)(floor((MAX_SCRIPT_SIZE_BEFORE_GENESIS+1) / CHUNK_SIZE + 2))) + [OP_TRUE]))
        tx5 = new_tx(utxos[10], target_script_size=500, lock_script=CScript([OP_DROP] * ((int)(floor(MAX_SCRIPT_SIZE_BEFORE_GENESIS / CHUNK_SIZE + 2))) + [OP_TRUE]))
        tx6 = new_tx(utxos[11], target_script_size=500, lock_script=CScript([OP_DROP] * ((int)(floor((MAX_SCRIPT_SIZE_BEFORE_GENESIS+1) / CHUNK_SIZE + 2))) + [OP_TRUE]))
        add_to_block_and_send(txs=[tx3, tx4, tx5, tx6], len_mem0=4)
        # Test tx with unlock script size MAX_SCRIPT_SIZE_BEFORE_GENESIS is ok
        utxo = {'txid': tx3, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS, unlock_script=make_unlock_script(target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS))
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)

        # Test tx with unlock script size MAX_SCRIPT_SIZE_BEFORE_GENESIS +1 is rejected
        utxo = {'txid': tx4, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS+1, unlock_script=make_unlock_script(target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS + 1))
        add_to_block_and_send(txs=[test_tx], len_mem0=0, valid=[])
        wait_for_reject_message(conn, reject_reason=b'genesis-script-verify-flag-failed (Script is too big)')

        # The last block at genesisactivationheight - 1 was rejected and therefore reverted
        # We generate additional block to get to genesis height
        node.generate(1)
        assert_equal(self.genesisactivationheight - 1, node.getblockchaininfo()['blocks'])
        # Genesis height
        # Check that they the results are same as before -- parents are from before genesis -- should still be unspendable
        utxo = {'txid': tx1, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS)
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)
        #
        utxo = {'txid': tx2, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS+1)
        add_to_block_and_send(txs=[test_tx], len_mem0=0, valid=[])
        ensure_no_rejection(conn)
        #
        # Test tx with unlock script size MAX_SCRIPT_SIZE_BEFORE_GENESIS is ok
        utxo = {'txid': tx5, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS, unlock_script=make_unlock_script(target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS))
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)
        #
        # Test tx with unlock script size MAX_SCRIPT_SIZE_BEFORE_GENESIS +1 is still rejected
        utxo = {'txid': tx6, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS + 1, unlock_script=make_unlock_script(target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS+1))
        add_to_block_and_send(txs=[test_tx], len_mem0=0, valid=[])
        wait_for_reject_message(conn, reject_reason=b'genesis-script-verify-flag-failed (Script is too big)')

        # Create 3 new txes with script sizes :
        # MAX_SCRIPT_SIZE_BEFORE_GENESIS, MAX_SCRIPT_SIZE_BEFORE_GENESIS*5, policyLimit and policyLimit+30 (just under max tx size)
        # send them via p2p and mine into blocks
        # Check that first three pass and the 4th is rejected with 'Script is too big'
        tx1 = new_tx(utxos[4], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS)
        tx2 = new_tx(utxos[5], target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS*5)
        tx3 = new_tx(utxos[6], target_script_size=ONE_MEGABYTE-200)
        tx4 = new_tx(utxos[7], target_script_size=ONE_MEGABYTE - 170)
        add_to_block_and_send(txs=[tx1, tx2, tx3, tx4], len_mem0=4)

        utxo = {'txid': tx1, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS)
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)

        utxo = {'txid': tx2, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS*5)
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)

        utxo = {'txid': tx3, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=ONE_MEGABYTE-200)
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)

        utxo = {'txid': tx4, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=ONE_MEGABYTE - 170)
        add_to_block_and_send(txs=[test_tx], len_mem0=0, valid=[])
        wait_for_reject_message(conn, reject_reason=b'non-mandatory-script-verify-flag (Script is too big)')

        # Create 3 more txs with large unlocking scripts
        # With CORE-195 we treat txs with OP_CODES in unlock script as invalid. Since we're testing utxos from before genesis, the script must
        # be split into shorter chunks to agree with max element size limit. OP_DROPS from unlock script are moved into the lock scripts
        tx1 = new_tx(utxos[12], target_script_size=500, lock_script=CScript([OP_DROP] * ((int)(floor(MAX_SCRIPT_SIZE_BEFORE_GENESIS / CHUNK_SIZE + 2))) + [OP_TRUE]))
        tx2 = new_tx(utxos[13], target_script_size=500, lock_script=CScript([OP_DROP] * ((int)(floor((ONE_MEGABYTE - 200) / CHUNK_SIZE + 2))) + [OP_TRUE]))
        tx3 = new_tx(utxos[14], target_script_size=500, lock_script=CScript([OP_DROP] * ((int)(floor((ONE_MEGABYTE - 200) / CHUNK_SIZE + 2))) + [OP_TRUE]))
        add_to_block_and_send(txs=[tx1, tx2, tx3], len_mem0=3)

        # Test that tx with unlock script size MAX_SCRIPT_SIZE_BEFORE_GENESIS + 1 is ok
        utxo = {'txid': tx1, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS, unlock_script=make_unlock_script(target_script_size=MAX_SCRIPT_SIZE_BEFORE_GENESIS))
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)

        # Test that tx with unlock script size policyLimit is ok
        utxo = {'txid': tx2, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=ONE_MEGABYTE - 200, unlock_script=make_unlock_script(target_script_size=ONE_MEGABYTE - 200))
        add_to_block_and_send(txs=[test_tx])
        ensure_no_rejection(conn)

        # Test that tx with unlock script size over policyLimit is still rejected
        utxo = {'txid': tx3, 'vout': 0, 'amount': 200000000}
        test_tx = new_tx(utxo, simple=True, target_script_size=ONE_MEGABYTE - 170, unlock_script=make_unlock_script(target_script_size=ONE_MEGABYTE - 170))
        add_to_block_and_send(txs=[test_tx], len_mem0=0, valid=[])
        wait_for_reject_message(conn, reject_reason=b'non-mandatory-script-verify-flag (Script is too big)')


if __name__ == '__main__':
    MaxScriptSizeTest().main()
