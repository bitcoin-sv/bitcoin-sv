#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test scenario:
First part: tests consists of cases that test each of the transaction specific config settings (increased and decreased)
they all follow the same patern:
 - create base transactions,.. - prepare the stage (optional)
 - create 16 same transactions (the kind to trigger specific threshold we're testing)
 - send them using sendrawtransactions() in groups of 4:
    - first 4 the usual way, no overrides
    - second 4 with the general override (override for all transactions sent in batch)
    - third 4 no general override, the first transaction has per-tx override
    - fourth 4 with the general override, the first transaction has per-tx override (different directions)
Second part: reorg scenario
Check that the transaction specific config is not applied when reorg happens, so the transactions that were accepted
when originally sent will now be rejected.
"""
import json
import random
import glob
from decimal import Decimal
from math import floor, ceil
from time import sleep
import re

from test_framework.cdefs import ONE_MEGABYTE, ELEMENT_OVERHEAD, DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS
from test_framework.mininode import NodeConn, NetworkThread, NodeConnCB, mininode_lock, FromHex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import TestNode
from test_framework.util import hex_str_to_bytes, assert_raises_rpc_error, assert_equal, wait_for_reject_message, \
    wait_until, p2p_port, bytes_to_hex_str, connect_nodes_bi, hashToHex, satoshi_round, connect_nodes, sync_blocks
from test_framework.script import OP_1, OP_FALSE, OP_RETURN, OP_TRUE, OP_CHECKSIG, OP_DROP, OP_DUP, OP_HASH160, \
    OP_EQUALVERIFY, OP_ADD, OP_CAT, OP_MUL
from test_framework.blocktools import COIN, CScript, CTransaction, CTxOut, CTxIn, COutPoint, uint256_from_str, ToHex, \
    calc_needed_data_size, create_tx
from test_framework.blocktools import create_transaction


def getInputScriptPubKey(node, input, index):
    txid = hashToHex(input.prevout.hash)
    raw = node.getrawtransaction(txid)
    tx = FromHex(CTransaction(), raw)
    tx.rehash()
    return tx.vout[index].scriptPubKey


def count_in_log(rpc, msg, node_dir, from_line=0):
    count = 0
    txes = set()
    for line in open(glob.glob(rpc.options.tmpdir + node_dir + "/regtest/bitcoind.log")[0]).readlines()[from_line:]:
        if msg in line:
            # Check if the specified reject message was found in log and add the transaction id to a set (to avoid duplicates)
            if "rejected" in line or "invalid orphan" in line:
                try:
                    txid = re.match(".*txn= [a-f0-9]+", line).group(0).split(' ')[-1]
                    txes.add(txid)
                except:
                    print(line)
    # Return number of distinct transaction ids that were rejected with specified reject message
    return len(txes)


class SendrawtransactionsSkipFlags(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2

    def setup_network(self):
        self.maxtxsize = 150000
        self.datacarriersize = 145000
        self.maxscriptsize = 140000
        self.maxscriptnumlength = 2000
        self.maxstackmemoryusagepolicy = 7000
        self.limitancestorcount = 5
        self.limitcpfpgroupmemberscount = 7
        self.acceptnonstdoutputs = 1 #default
        self.datacarrier = 1 #default
        self.maxstdtxvalidationduration = 5
        self.maxnonstdtxvalidationduration = 10
        self.minconsolidationfactor = 10
        self.maxconsolidationinputscriptsize = 151
        self.minconfconsolidationinput = 5
        self.acceptnonstdconsolidationinput = False
        self.extra_args = [["-debug",
                            "-genesisactivationheight=1",
                            "-minminingtxfee=0.000003",
                            "-mindebugrejectionfee=0.0000025",
                            "-checkmempool=0",
                            "-maxscriptsizepolicy=%d" % self.maxscriptsize,
                            "-maxtxsizepolicy=%d" % self.maxtxsize,
                            "-datacarriersize=%d" % self.datacarriersize,
                            "-maxscriptnumlengthpolicy=%d" % self.maxscriptnumlength,
                            "-maxstackmemoryusagepolicy=%d" % self.maxstackmemoryusagepolicy,
                            "-limitancestorcount=%d" % self.limitancestorcount,
                            "-limitcpfpgroupmemberscount=%d" % self.limitcpfpgroupmemberscount,
                            "-acceptnonstdoutputs=%s" % self.acceptnonstdoutputs,
                            "-datacarrier=%s" % self.datacarrier,
                            "-acceptnonstdtxn=false", #avoid accepting nonstd txes (doesnt check datacarrier size..) - regtest policy
                            "-minconsolidationfactor=%d" % self.minconsolidationfactor,
                            "-maxconsolidationinputscriptsize=%d" % self.maxconsolidationinputscriptsize,
                            "-minconfconsolidationinput=%d" % self.minconfconsolidationinput,
                            "-acceptnonstdconsolidationinput=0",
                            '-banscore=10000000',
                            '-txindex=1',
                            ], []]
        self.setup_nodes()
        connect_nodes_bi(self.nodes, 0,1)

    def create_transaction(self, fee, op_codes, input_txs, no_outputs, amount, feerate=1.001, dustrelay=0, out0_value=0):
        ftx = CTransaction()
        for input_tx, n in input_txs:
            ftx.vin.append(
                CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(input_tx["txid"])[::-1]), n)))
        st = 0
        if dustrelay > 0:
            out0_value = ((len(CTxOut().serialize()) + 148) * dustrelay) / 1000 * COIN
        if out0_value > 0:
            ftx.vout.append(CTxOut(int(out0_value), CScript(op_codes)))
            st = 1
        for _ in range(no_outputs):
            ftx.vout.append(CTxOut(int((float(amount) - out0_value - fee)//no_outputs), CScript(op_codes)))
        ftx.rehash()
        tx = self.nodes[0].signrawtransaction(ToHex(ftx))['hex']
        ftxSigned = FromHex(CTransaction(), tx)
        if fee < len(tx)*feerate:
            for i in range(st, no_outputs+st):
                ftx.vout[i].nValue = (ftx.vout[i].nValue - ceil((len(ftxSigned.serialize())*feerate - fee)/no_outputs))
        if dustrelay > 0:
            ftx.vout[0].nValue = int(((len(ftx.vout[0].serialize()) + 148) * dustrelay) / 1000 * COIN)
        tx = self.nodes[0].signrawtransaction(ToHex(ftx))['hex']
        return tx

    def create_simple_datatx(self, utxo, fee, data_size):
        amount = int(int(utxo['amount'] * COIN) - fee)/COIN
        inputs = []
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        outputs = {}
        addr = self.nodes[0].getnewaddress()
        outputs[addr] = amount
        data = bytearray(random.getrandbits(8) for _ in range(24)) + bytearray(data_size - 24)
        outputs["data"] = bytes_to_hex_str(data)
        rawTxn = self.nodes[0].createrawtransaction(inputs, outputs)
        signedTxn = self.nodes[0].signrawtransaction(rawTxn)["hex"]
        return signedTxn

    def make_script(self, target_script_size, op_codes=[OP_TRUE], elem=[b"a" * 499, OP_DROP]):
        elem_len = len(CScript(elem))
        max_size = calc_needed_data_size(op_codes, target_script_size)
        remainder = (max_size - len(CScript(op_codes + elem * (int)(floor(max_size / elem_len)))))
        script = CScript(op_codes + elem * (int)(floor(max_size / elem_len)) + [b"a" * remainder, OP_DROP])

        while len(script) != target_script_size:
            remainder -= (len(script) - target_script_size)
            script = CScript(op_codes + elem * (int)(floor(max_size / elem_len)) + [b"a" * remainder, OP_DROP])
        return script

    def create_customscripts_tx(self, utxo, target_tx_size=0, target_script_size=0, op_codes=[OP_TRUE], elem=[b"a" * 499, OP_DROP], lock_script=None, unlock_script=b"", simple=False):
        tx_to_spend = utxo['txid']
        vout = utxo['vout']
        if target_script_size == 0 and lock_script:
            target_script_size = len(lock_script)
        if simple:
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(tx_to_spend)[::-1]), vout), unlock_script, 0xffffffff))
            tx.vout.append(CTxOut(int(int(utxo['amount'] * COIN) - target_script_size - 2000), CScript()))
            tx.calc_sha256()
            tx.rehash()
            return tx

        out_amount = 199000000
        while True:
            tx = CTransaction()
            if(lock_script != None):
                tx.vout.append(CTxOut(int(int(utxo['amount'] * COIN) - target_script_size - 200000000 - len(lock_script)), lock_script))
            elif target_script_size != 0:
                script = self.make_script(target_script_size=target_script_size, op_codes=op_codes, elem=elem)
                tx.vout.append(CTxOut(int(int(utxo['amount'] * COIN) - target_script_size - 205000000), script))
            else:
                out_amount = int(utxo['amount'] * COIN) - target_tx_size//4 - 2000
            tx.vout.append(CTxOut(out_amount, CScript([OP_DUP, OP_HASH160,
                                                      hex_str_to_bytes(
                                                          "ab812dc588ca9d5787dde7eb29569da63c3a238c"),
                                                      OP_EQUALVERIFY,
                                                      OP_CHECKSIG])))

            txin = CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(tx_to_spend)[::-1]), vout))
            tx.vin.append(txin)
            txHex = self.nodes[0].signrawtransaction(ToHex(tx))['hex']
            txSigned = FromHex(CTransaction(), txHex)
            if target_tx_size != 0:
                padding_size = (2*target_tx_size - len(ToHex(txSigned)))//2
                txSigned.vout.append(CTxOut(1000, CScript([OP_FALSE, OP_RETURN] + [bytes(5)[:1] *(padding_size)])))
            txHex = ToHex(tx)
            tx = FromHex(CTransaction(), txHex)

            txSigned = FromHex(CTransaction(), self.nodes[0].signrawtransaction(ToHex(txSigned))['hex'])
            txSigned.rehash()

            if target_tx_size != 0 and len(txSigned.serialize()) > target_tx_size:
                while True:
                    padding_size-=1
                    txSigned.vout[-1] = (CTxOut(1000, CScript([OP_FALSE, OP_RETURN] + [bytes(5)[:1] * padding_size])))
                    txSigned = FromHex(CTransaction(), self.nodes[0].signrawtransaction(ToHex(txSigned))['hex'])
                    txSigned.rehash()
                    if abs(len(txSigned.serialize()) - target_tx_size) <= 1:
                        return txSigned
            elif target_tx_size != 0 and len(txSigned.serialize()) < target_tx_size:
                while True:
                    padding_size += 1
                    txSigned.vout[-1] = (CTxOut(1000, CScript([OP_FALSE, OP_RETURN] + [bytes(5)[:1] * padding_size])))
                    txSigned = FromHex(CTransaction(), self.nodes[0].signrawtransaction(ToHex(txSigned))['hex'])
                    txSigned.rehash()
                    if abs(len(txSigned.serialize()) - target_tx_size) <= 1:
                        return txSigned
            if target_tx_size == 0 or abs(len(txSigned.serialize()) - target_tx_size) <= 1:
                return txSigned

    def send_and_check(self, ftxs, config_overrides_gen=None, invalid_txs=[]):
        if not config_overrides_gen:
            resultset = self.nodes[0].sendrawtransactions(ftxs)
        else:
            resultset = self.nodes[0].sendrawtransactions(ftxs, config_overrides_gen)

        for tx in invalid_txs:
            if tx['reject_reason']:
                tx['marked_invalid_ok'] = False
                assert 'invalid' in resultset.keys(), tx['txid'] + " not marked invalid. Should be marked invalid with reason " + tx['reject_reason']
                for inv_tx in resultset['invalid']:
                    if inv_tx['txid'] == tx['txid']:
                        tx['marked_invalid_ok'] = True
                        assert inv_tx['reject_reason'] == tx['reject_reason'], \
                            tx['txid'] \
                            + " marked invalid with reason " \
                            + inv_tx['reject_reason'] \
                            + ", should be with " \
                            + tx['reject_reason']
                        break
                assert tx['marked_invalid_ok'], tx['txid'] + " not marked invalid. Should be marked invalid with reason " + tx['reject_reason']

            else:
                wait_until(lambda: (tx['txid'] in self.nodes[0].getrawmempool()), timeout=5, label=tx['txid'] + " not in mempool after 5 secs.")

    def send_and_check_batch(self, txes, config_overrides, invalid_txs):
        self.send_and_check(txes[0:4], invalid_txs=invalid_txs[0:4])
        self.send_and_check(txes[4:8], config_overrides_gen=config_overrides, invalid_txs=invalid_txs[4:8])
        self.send_and_check(txes[8:12], invalid_txs=invalid_txs[8:12])
        self.send_and_check(txes[12:16], config_overrides_gen=config_overrides, invalid_txs=invalid_txs[12:16])

    def create_transactions_and_send_with_overridden_config(self, utxos, tx_params, config_overrides_gen, config_overrides_pertx, invalid_txs=[], lock_script=None, custom_scripts=False, simple=True):
        ftxes = []
        # create 16 identical transactions
        for i in range(len(utxos)):
            utxo = utxos[i]
            if custom_scripts:
                ftx = ToHex(self.create_customscripts_tx(utxo, target_tx_size=tx_params['tx_size'], target_script_size=tx_params['script_size'], lock_script=lock_script, simple=simple))
            elif 'data_size' in tx_params.keys():
                ftx = self.create_simple_datatx(utxo, tx_params['fee'], data_size=tx_params['data_size'])
            else:
                ftx = self.create_transaction(tx_params['fee'], tx_params['op_codes'], [(utxo, 0)], 1, int(utxo['amount'] * COIN))
            tx = FromHex(CTransaction(), ftx)
            ftxes.append({"hex": ftx, "config": config_overrides_pertx[i]})
            #invalid_txs[i]['txid'] = tx.hash assigns None for some reason!!
            invalid_txs[i].update({'txid': str(tx)[18:82]})

        #send 4 transactions without overriden policy values
        self.send_and_check(ftxes[0:4], invalid_txs=invalid_txs[0:4])
        #send 4 transactions with overriden policy values on global level only
        self.send_and_check(ftxes[4:8], config_overrides_gen=config_overrides_gen, invalid_txs=invalid_txs[4:8])
        #send 4 transactions with overriden policy values per transaction only
        self.send_and_check(ftxes[8:12], invalid_txs=invalid_txs[8:12])
        #send 4 transactions with overriden policy values on global lever and per transaction
        self.send_and_check(ftxes[12:16], config_overrides_gen=config_overrides_gen, invalid_txs=invalid_txs[12:16])

    def prepare_base_txs(self, utxos, target_tx_size=0, target_script_size=0, lock_script=None):
        txs = [self.create_customscripts_tx(utxo, target_tx_size=target_tx_size, target_script_size=target_script_size, lock_script=lock_script) for utxo in utxos]
        txids = [self.nodes[0].sendrawtransaction(ToHex(tx), False, False) for tx in txs]
        return [{'txid': txids[i], 'vout': 0, 'amount': txs[i].vout[0].nValue/ COIN} for i in range(len(txs))]

    def create_utxos_value10000(self, node, utxo_count, min_confirmations, script=None, utxo=None):
        utxos = []
        addr = node.getnewaddress()
        for i in range(utxo_count):
            txid = node.sendtoaddress(addr, satoshi_round(10000 / COIN))
            wait_until(lambda: txid in node.getrawmempool(), timeout=5)
            tx = FromHex(CTransaction(), node.getrawtransaction(txid))
            tx.rehash()
            utxos.append(tx)
        if script:
            if not utxo:
                spendable = [utxo for utxo in self.nodes[0].listunspent() if utxo['confirmations'] > 100]
                i = 0
                utxo = spendable[i]
                while utxo['amount'] <= 0.010000:
                    utxo = spendable[i]
                    i += 1
            tx = self.create_transaction(0, script, [(utxo, 0)], 1, utxo['amount'] * COIN, out0_value=10000)
            txid = node.sendrawtransaction(tx)
            tx = FromHex(CTransaction(), node.getrawtransaction(txid))
            tx.rehash()
            utxos[0] = tx
        if min_confirmations > 0:
            node.generate(min_confirmations)
        return utxos

    def create_and_sign_consolidationtx(self, node, in_count, min_confirmations, script=None, utxo=None, utxos=None):
        if not utxos:
            utxos = self.create_utxos_value10000(node, in_count, min_confirmations, script=script, utxo=utxo)
        inputs = []
        sum_values_bsvs = 0
        for u in utxos:
            for i in range(len(u.vout)):
                if u.vout[i].nValue == 10000:
                    sum_values_bsvs = sum_values_bsvs + satoshi_round(10000 / COIN)
                    inputs.append({"txid": u.hash, "vout": i})

        assert (len(utxos) == in_count)
        assert (sum_values_bsvs == in_count * satoshi_round(10000 / COIN))

        addr = node.getnewaddress()
        outputs = {}
        outputs[addr] = sum_values_bsvs

        rawtx = node.createrawtransaction(inputs, outputs)
        return node.signrawtransaction(rawtx)['hex']

    def create_transaction_for_skipscriptflags(self, fee, op_codes, input_tx, output_to_spend, amount):
        ftx = CTransaction()
        ftx.vin.append(
            CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(input_tx["txid"])[::-1]), output_to_spend)))
        ftx.vout.append(CTxOut(amount - fee, CScript(op_codes)))
        ftx.rehash()
        tx = self.nodes[0].signrawtransaction(ToHex(ftx))['hex']
        return tx

    def run_test_node(self, node_index=0, dstaddr='127.0.0.1', dstportno=0, num_of_connections=1):
        test_node = TestNode()
        conn = NodeConn(dstaddr, p2p_port(dstportno), self.nodes[node_index], test_node)
        test_node.add_connection(conn)
        return test_node,conn

    def run_test(self):
        #generate 500 utxos
        for i in range(5):
            self.nodes[0].generate(100)
        sync_blocks(self.nodes, timeout=120)
        height1 = self.nodes[1].getblockchaininfo()['blocks']
        self.stop_node(1)

        # Accumulate utxos to use later. Make sure they have enough confirmations so they wont be affected by reorg
        # and that amount is large enough to use for spending in larger transactions
        utxos = [utxo for utxo in self.nodes[0].listunspent() if utxo['confirmations'] > 50 and utxo['amount'] > 6]

        config_overrides_decrease = {"maxtxsizepolicy": 100000, "datacarriersize": 99000, "maxscriptsizepolicy": 7000,
                                     "maxscriptnumlengthpolicy": 200, "maxstackmemoryusagepolicy": 6000, "limitancestorcount": 3,
                                     "limitcpfpgroupmemberscount": 5,
                                     "minconsolidationfactor": 2, "minconfconsolidationinput": 3,
                                     "acceptnonstdconsolidationinput": False}
        config_overrides_increase = {"maxtxsizepolicy": ONE_MEGABYTE*100, "datacarriersize": 99995000, "maxscriptsizepolicy": 20000000,
                                     "maxscriptnumlengthpolicy": 250000, "maxstackmemoryusagepolicy": ONE_MEGABYTE*100,
                                     "limitancestorcount": 7, "limitcpfpgroupmemberscount": 10,
                                     "minconsolidationfactor": 15, "minconfconsolidationinput": 8, "acceptnonstdconsolidationinput": True}

        self.log.info("Test sendrawtransactions with overriden policy values")
        i_utxo = 0

        def falses(no):
            return [{'reject_reason': False} for _ in range(no)]

        self.log.info("PASS\n")

        self.log.info("Test handling configs with errors")
        signed_tx = self.create_transaction_for_skipscriptflags(1000, [OP_1, OP_1, OP_1], utxos[0], 0, int(utxos[0]['amount'] * COIN))
        txid = self.nodes[0].decoderawtransaction(signed_tx)["txid"]

        # Invalid config setting is used
        assert_raises_rpc_error(-8, "abc is not a valid policy setting.",
                                self.nodes[0].sendrawtransactions,
                                [{'hex': signed_tx, 'allowhighfees': False, 'dontcheckfee': False}],
                                {'abc': -1})

        res = self.nodes[0].sendrawtransactions(
            [{'hex': signed_tx, 'dontcheckfee': True, 'config': {'abc': -1}}])
        assert_equal(res['invalid'][0]['txid'], txid)
        assert_equal(res['invalid'][0]['reject_reason'], 'abc is not a valid policy setting.')

        # Valid config setting with invalid value
        assert_raises_rpc_error(-8, "Policy value for max tx size must not be less than 0",
                                self.nodes[0].sendrawtransactions,
                                [{'hex': signed_tx, 'allowhighfees': False, 'dontcheckfee': False}],
                                {'maxtxsizepolicy': -1})

        self.log.info("PASS\n")

        self.log.info("Test datacarriersize")
        # All transaction params are over policy thresholds (and potentially decreased overrides) but under
        # increased overrides
        # valid are the transactions sent with increased override values as global override and pertx override alone
        # transactions sent without override are invalid with reject reason "datacarrier-size-exceeded" and so is the
        # one with both global and pertx override (pertx override wins over the global one)
        tx_params = {"fee": 50000, "data_size": 146000}

        invalid_txs = ([{'reject_reason': "datacarrier-size-exceeded"} for _ in range(4)]
                       + falses(5)
                       + [{'reject_reason': "datacarrier-size-exceeded"} for _ in range(3)]
                       + [{'reject_reason': "datacarrier-size-exceeded"}]
                       + falses(3))
        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None, {"datacarriersize": 99000}, None, None, None]

        self.create_transactions_and_send_with_overridden_config(utxos[i_utxo:i_utxo+16], tx_params, config_overrides_increase, overrides_pertx, invalid_txs=invalid_txs)
        i_utxo+=16

        invalid_txs = (falses(4)
                       + [{'reject_reason': "datacarrier-size-exceeded"} for _ in range(4)]
                       + [{'reject_reason': "datacarrier-size-exceeded"}]
                       + falses(4)
                       + [{'reject_reason': "datacarrier-size-exceeded"} for _ in range(3)])

        overrides_pertx = (
            [None for _ in range(8)]
            + [config_overrides_decrease, None, None, None, {"datacarriersize": 100000}, None, None, None])

        tx_params = {"fee": 30000, "data_size": 99000}

        self.create_transactions_and_send_with_overridden_config(utxos[i_utxo:i_utxo + 16], tx_params,
                                                                 config_overrides_decrease, overrides_pertx,
                                                                 invalid_txs=invalid_txs)
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test maxscriptsize")
        base_utxos = self.prepare_base_txs(utxos[i_utxo:i_utxo + 16], target_script_size=self.maxscriptsize+1)

        invalid_txs = (
            [{'reject_reason': "non-mandatory-script-verify-flag (Script is too big)"} for _ in range(4)]
            + falses(5)
            + [{'reject_reason': "non-mandatory-script-verify-flag (Script is too big)"} for _ in range(3)]
            + [{'reject_reason': "mandatory-script-verify-flag-failed (Script is too big)"}]
            + falses(3))

        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"maxscriptsizepolicy": self.maxscriptsize}, None, None, None]
        tx_params = {"fee": 600, "script_size": self.maxscriptsize+1, "tx_size": 0}

        self.create_transactions_and_send_with_overridden_config(base_utxos, tx_params,
                                                                 config_overrides_increase, overrides_pertx,
                                                                 invalid_txs=invalid_txs, custom_scripts=True)
        i_utxo += 16

        base_utxos = self.prepare_base_txs(utxos[i_utxo:i_utxo+16], target_script_size=self.maxscriptsize)

        invalid_txs = (falses(4)
                       + [{'reject_reason': "mandatory-script-verify-flag-failed (Script is too big)"} for _ in range(4)]
                       + [{'reject_reason': "mandatory-script-verify-flag-failed (Script is too big)"}]
                       + falses(4)
                       + [{'reject_reason': "mandatory-script-verify-flag-failed (Script is too big)"} for _ in range(3)])

        overrides_pertx = (
            [None for _ in range(8)]
            + [config_overrides_decrease, None, None, None, {"maxscriptsizepolicy": self.maxscriptsize + 1}, None, None, None])

        tx_params = {"fee": 600, "script_size": self.maxscriptsize, "tx_size": 0}

        self.create_transactions_and_send_with_overridden_config(base_utxos,tx_params,
                                                                 config_overrides_decrease, overrides_pertx,
                                                                 invalid_txs=invalid_txs, custom_scripts=True)
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test maxscriptnumlength")
        lock_script_max = CScript(
            [bytearray([42] * (self.maxscriptnumlength + 1)), bytearray([42] * (self.maxscriptnumlength + 1)), OP_ADD])

        base_utxos = self.prepare_base_txs(utxos[i_utxo:i_utxo+16], lock_script=lock_script_max)

        invalid_txs = (
            [{'reject_reason': "max-script-num-length-policy-limit-violated (Script number overflow)"} for _ in range(4)]
            + falses(5)
            + [{'reject_reason': "max-script-num-length-policy-limit-violated (Script number overflow)"} for _ in range(3)]
            + [{'reject_reason': "max-script-num-length-policy-limit-violated (Script number overflow)"}]
            + falses(3))

        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"maxscriptnumlengthpolicy": 200}, None, None, None]
        tx_params = {"fee": 600, "script_num_len": self.maxscriptnumlength + 1, 'op_codes': []}

        self.create_transactions_and_send_with_overridden_config(base_utxos, tx_params,
                                                                 config_overrides_increase, overrides_pertx,
                                                                 invalid_txs=invalid_txs)
        i_utxo += 16

        lock_script_max = CScript(
            [bytearray([42] * (self.maxscriptnumlength)), bytearray([42] * (self.maxscriptnumlength)), OP_ADD])
        base_utxos = self.prepare_base_txs(utxos[i_utxo:i_utxo + 16], lock_script=lock_script_max)

        invalid_txs = (falses(4)
                       + [{'reject_reason': "max-script-num-length-policy-limit-violated (Script number overflow)"} for _ in range(4)]
                       + [{'reject_reason': "max-script-num-length-policy-limit-violated (Script number overflow)"}]
                       + falses(4)
                       + [{'reject_reason': "max-script-num-length-policy-limit-violated (Script number overflow)"} for _ in range(3)])

        overrides_pertx = (
            [None for _ in range(8)]
            + [config_overrides_decrease, None, None, None, {"maxscriptnumlengthpolicy": self.maxscriptnumlength}, None, None, None])

        tx_params = {"fee": 600, "script_num_len": self.maxscriptnumlength, 'op_codes': []}

        self.create_transactions_and_send_with_overridden_config(base_utxos, tx_params,
                                                                 config_overrides_decrease, overrides_pertx,
                                                                 invalid_txs=invalid_txs)
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test maxstackmemoryusagepolicy")
        lock_script_max = CScript([b"a" * (self.maxstackmemoryusagepolicy - 2 * ELEMENT_OVERHEAD), b"b", OP_CAT])
        base_utxos = self.prepare_base_txs(utxos[i_utxo:i_utxo + 16], lock_script=lock_script_max)

        invalid_txs = ([
            {'reject_reason': "non-mandatory-script-verify-flag (Stack size limit exceeded)"} for _ in range(4)]
            + falses(5)
            + [{'reject_reason': "non-mandatory-script-verify-flag (Stack size limit exceeded)"} for _ in range(3)]
            + [{'reject_reason': "non-mandatory-script-verify-flag (Stack size limit exceeded)"}]
            + falses(3))

        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"maxstackmemoryusagepolicy": 1}, None, None, None]
        tx_params = {"fee": 600, 'op_codes': [], "tx_size": self.maxtxsize, "script_size": 0}

        self.create_transactions_and_send_with_overridden_config(base_utxos, tx_params,
                                                                 config_overrides_increase, overrides_pertx,
                                                                 invalid_txs=invalid_txs, custom_scripts=True)
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test txsize")
        # +2 in both cases to avoid rounding errors
        tx_params = {"fee": 6000, "tx_size": self.maxtxsize + 2, "script_size": 50}

        invalid_txs = [{'reject_reason': "tx-size"} for _ in
                       range(4)] + falses(5) + [
            {'reject_reason': "tx-size"} for _ in
            range(3)] + [
            {'reject_reason': "tx-size"}] + falses(3)
        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"maxtxsizepolicy": 100000}, None, None, None]
        self.create_transactions_and_send_with_overridden_config(utxos[i_utxo:i_utxo + 16], tx_params,
                                                                 config_overrides_increase, overrides_pertx,
                                                                 invalid_txs=invalid_txs, custom_scripts=True,
                                                                 simple=False)
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test limitancestorcount")
        mining_fee = 1.001
        relayfee = 0.251

        # create oversized primary mempool chains, the last tx in the chain will be over the limit
        outpoints = utxos[i_utxo: i_utxo+16]

        invalid_txs = ([{'reject_reason': "too-long-mempool-chain"} for _ in range(4)]
                       + falses(5)
                       + [{'reject_reason':"too-long-mempool-chain"} for _ in range(3)]
                       + [{'reject_reason': "too-long-mempool-chain"}]
                       + falses(3))
        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"limitancestorcount": 5}, None, None, None]

        txes = []
        for j in range(16):
            for i in range(self.limitancestorcount):
                tx = self.create_transaction(int(ceil(len(ToHex(CTransaction())) * mining_fee * 2)), [OP_TRUE],
                                             [(outpoints[j], 0)], 1, int((outpoints[j]['amount']) * COIN))

                txid = self.nodes[0].sendrawtransaction(tx, False, False)

                outpoints[j] = {'txid': txid, 'vout': 0, 'amount': FromHex(CTransaction(), tx).vout[0].nValue / COIN}
            tx = self.create_transaction(int(ceil(len(ToHex(CTransaction())) * mining_fee * 2)), [OP_TRUE],
                                         [(outpoints[j], 0)], 1, int((outpoints[j]['amount']) * COIN))
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_increase, invalid_txs)

        i_utxo += 16

        outpoints = utxos[i_utxo:i_utxo+16]

        invalid_txs = (falses(4)
                       + [{'reject_reason': "too-long-mempool-chain"} for _ in range(4)]
                       + [{'reject_reason': "too-long-mempool-chain"}]
                       + falses(4)
                       + [{'reject_reason':"too-long-mempool-chain"} for _ in range(3)])

        overrides_pertx = [None for _ in range(8)] + [config_overrides_decrease, None, None, None,
                                                      {"limitancestorcount": 5}, None, None, None]

        # create oversized primary mempool chains, the last tx in the chain will be over the limit
        outpoints = utxos[i_utxo: i_utxo + 16]
        txes = []
        for j in range(16):
            for i in range(self.limitancestorcount-1):
                tx = self.create_transaction(int(ceil(len(ToHex(CTransaction())) * mining_fee * 2)), [OP_TRUE],
                                             [(outpoints[j], 0)], 1, int((outpoints[j]['amount']) * COIN))

                txid = self.nodes[0].sendrawtransaction(tx, False, False)

                outpoints[j] = {'txid': txid, 'vout': 0, 'amount': FromHex(CTransaction(), tx).vout[0].nValue / COIN}
            tx = self.create_transaction(int(ceil(len(ToHex(CTransaction())) * mining_fee * 2)), [OP_TRUE],
                                         [(outpoints[j], 0)], 1, int((outpoints[j]['amount']) * COIN))
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_decrease, invalid_txs)

        i_utxo += 16

        self.nodes[0].generate(1)
        hash_ancestor_block = self.nodes[0].getbestblockhash()
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test limitcpfpgroupmemberscount")
        mining_fee = 1.001
        relayfee = 0.251
        # create oversized secondary mempool chain, the last tx in the chain will be over the limit
        no_outputs = self.limitcpfpgroupmemberscount // 2 + 1
        outpoints = []
        children = []
        txes = []
        invalid_txs = ([{'reject_reason': "too-long-mempool-chain"} for _ in range(4)]
                       + falses(5)
                       + [{'reject_reason': "too-long-mempool-chain"} for _ in range(3)]
                       + [{'reject_reason': "too-long-mempool-chain"}]
                       + falses(3))
        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"limitcpfpgroupmemberscount": 5}, None, None, None]
        txes_fee_too_low = falses(16)
        for j in range(16):
            tx = self.create_transaction(0, [OP_TRUE], [(utxos[i_utxo+j], 0)], no_outputs,
                                         int((utxos[i_utxo+j]['amount']) * COIN), feerate=relayfee)

            txid = self.nodes[0].sendrawtransaction(tx, False, False)

            outpoints.append({'txid': txid, 'vout': 0, 'amount': FromHex(CTransaction(), tx).vout[0].nValue / COIN})
            children.append([])
            for i in range(no_outputs):
                outpoints[j]['vout'] = i
                tx = self.create_transaction(0, [OP_TRUE], [(outpoints[j], i)], 1, int((outpoints[j]['amount']) * COIN),
                                             feerate=relayfee)

                txid = self.nodes[0].sendrawtransaction(tx, False, False)

                children[j].append([{'txid': txid, 'vout': 0, 'amount': FromHex(CTransaction(), tx).vout[0].nValue / COIN}, 0])

            tx = self.create_transaction(0, [OP_TRUE], children[j], 1,
                                         int((children[j][0][0]['amount']) * COIN * no_outputs), feerate=mining_fee*(no_outputs+1))

            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]
            txes_fee_too_low[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_increase, invalid_txs)

        txes_to_check_on_reorg = [str(FromHex(CTransaction(), tx['hex']))[18:82] for tx in txes]
        i_utxo += 16

        # Make sure transactions from the rejected (too long) chains can be included in block so they are cleared from mempool
        # (send them with the looser threshold)
        self.send_and_check(txes[0:4], config_overrides_gen=config_overrides_increase, invalid_txs=txes_fee_too_low[0:4])
        self.send_and_check(txes[9:12], config_overrides_gen=config_overrides_increase, invalid_txs=txes_fee_too_low[9:12])
        txes[12]["config"] = None
        self.send_and_check([txes[12]], config_overrides_gen=config_overrides_increase, invalid_txs=[txes_fee_too_low[12]])

        self.nodes[0].generate(1)
        # store the hash of this block to check on reorg -- children should be rejected
        # parent transactions should get into the mempool and stay there
        hash_cpfp_block = self.nodes[0].getbestblockhash()

        no_outputs = self.limitcpfpgroupmemberscount // 2
        outpoints = []
        children = []
        txes = []

        invalid_txs = (falses(4)
                       + [{'reject_reason': "too-long-mempool-chain"} for _ in range(4)]
                       + [{'reject_reason': "too-long-mempool-chain"}]
                       + falses(4)
                       + [{'reject_reason':"too-long-mempool-chain"} for _ in range(3)])

        overrides_pertx = (
            [None for _ in range(8)]
            + [config_overrides_decrease, None, None, None, {"limitcpfpgroupmemberscount": 10}, None, None, None])

        for j in range(16):
            tx = self.create_transaction(0, [OP_TRUE], [(utxos[i_utxo + j], 0)], no_outputs,
                                         int((utxos[i_utxo + j]['amount']) * COIN), feerate=relayfee)

            txid = self.nodes[0].sendrawtransaction(tx, False, False)

            outpoints.append({'txid': txid, 'vout': 0, 'amount': FromHex(CTransaction(), tx).vout[0].nValue / COIN})

            children.append([])
            for i in range(no_outputs):
                outpoints[j]['vout'] = i
                tx = self.create_transaction(0, [OP_TRUE], [(outpoints[j], i)], 1, int((outpoints[j]['amount']) * COIN),
                                             feerate=relayfee)

                txid = self.nodes[0].sendrawtransaction(tx, False, False)

                children[j].append(
                    [{'txid': txid, 'vout': 0, 'amount': FromHex(CTransaction(), tx).vout[0].nValue / COIN}, 0])

            tx = self.create_transaction(0, [OP_TRUE], children[j], 1,
                                         int((children[j][0][0]['amount']) * COIN * no_outputs), feerate=mining_fee*(no_outputs))

            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]
            txes_fee_too_low[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_decrease, invalid_txs)

        txes[8]["config"] = None
        self.send_and_check(txes[4:9], config_overrides_gen=config_overrides_increase, invalid_txs=txes_fee_too_low[4:9])
        self.send_and_check(txes[13:16], config_overrides_gen=config_overrides_increase, invalid_txs=txes_fee_too_low[13:16])

        i_utxo += 16

        # Make sure transactions from the rejected (too long) chains can be included in block so they are cleared from mempool
        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        # Test acceptnonstdoutputs
        self.log.info("Test acceptnonstdoutputs")
        config_overrides_increase["acceptnonstdoutputs"] = False
        base_utxos = self.prepare_base_txs(utxos[i_utxo:i_utxo+16], target_script_size=self.maxscriptsize + 1)
        tx_params = {"fee": 600, "script_size": self.maxscriptsize, "tx_size": 0}

        invalid_txs = (
            [{'reject_reason': "non-mandatory-script-verify-flag (Script is too big)"} for _ in range(4)]
            + [{'reject_reason': "scriptpubkey"} for _ in range(5)]
            + [{'reject_reason':"non-mandatory-script-verify-flag" " (Script is too big)"} for _ in range(4)]
            + [{'reject_reason': "scriptpubkey"} for _ in range(3)])

        overrides_pertx = (
            [None for _ in range(8)]
            + [config_overrides_increase, None, None, None, {"acceptnonstdoutputs": True}, None, None, None])

        self.create_transactions_and_send_with_overridden_config(base_utxos,
                                                                 tx_params,
                                                                 config_overrides_increase,
                                                                 overrides_pertx,
                                                                 invalid_txs=invalid_txs,
                                                                 custom_scripts=True)
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        config_overrides_increase["acceptnonstdoutputs"] = True

        # Test datacarrier with acceptnonstdoutputs
        self.log.info("Test datacarrier with acceptnonstdoutputs")
        config_overrides_decrease["acceptnonstdoutputs"] = False
        config_overrides_decrease["datacarrier"] = False

        invalid_txs = (falses(4)
                       + [{'reject_reason': "scriptpubkey"} for _ in range(5)]
                       + falses(4) + [{'reject_reason': "scriptpubkey"} for _ in range(3)])

        overrides_pertx = [None for _ in range(8)] + [config_overrides_decrease, None, None, None,
                                                      {"acceptnonstdoutputs": True}, None, None, None]

        txes = []
        for j in range(16):
            tx = self.create_simple_datatx(utxos[i_utxo+j], 500, data_size=1000)
            txes.append({'hex': tx, 'config': overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_decrease, invalid_txs)

        config_overrides_decrease["acceptnonstdoutputs"] = True
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        # Test consolidation tx settings
        self.log.info("Test minconsolidationfactor")
        # Test ratio between size of input script and size of output script
        tx_hex = self.create_and_sign_consolidationtx(self.nodes[0], 1, min_confirmations=1)
        tx = FromHex(CTransaction(), tx_hex)
        tx.rehash()
        sin = len(getInputScriptPubKey(self.nodes[0], tx.vin[0], 0))
        sout = len(tx.vout[0].scriptPubKey)

        enough_inputs = sout * self.minconsolidationfactor // sin
        enough_inputs = max(enough_inputs, 2)
        enough_confirmations = self.minconfconsolidationinput

        invalid_txs = (falses(4)
                       + [{'reject_reason':"mempool min fee not met"} for _ in range(5)]
                       + falses(4)
                       + [{'reject_reason': "mempool min fee not met"} for _ in range(3)])

        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"minconsolidationfactor": 2}, None, None, None]

        # Test that input_sizes <= consolidation_factor * output_size
        # We assume scriptSig ~ 4 * scriptPubKey
        txes = []
        utxos_cons = []
        for j in range(16):
            utxos_cons.append(self.create_utxos_value10000(self.nodes[0], enough_inputs, 0))

        self.nodes[0].generate(enough_confirmations)
        for j in range(16):
            tx = self.create_and_sign_consolidationtx(self.nodes[0],
                                                      in_count=enough_inputs,
                                                      min_confirmations=enough_confirmations,
                                                      utxos=utxos_cons[j])
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_increase, invalid_txs)

        invalid_txs = (
            [{'reject_reason': "mempool min fee not met"} for _ in range(4)]
            + falses(5)
            + [{'reject_reason': "mempool min fee not met"} for _ in range(4)]
            + falses(3))

        overrides_pertx = (
            [None for _ in range(8)]
            + [config_overrides_decrease, None, None, None, {"minconsolidationfactor": 15}, None, None, None])

        # Test that input_sizes <= consolidation_factor * output_size
        # We assume scriptSig ~ 4 * scriptPubKey
        txes = []
        utxos_cons = []
        for j in range(16):
            utxos_cons.append(self.create_utxos_value10000(self.nodes[0], enough_inputs-1, 0))

        self.nodes[0].generate(enough_confirmations)
        for j in range(16):
            tx = self.create_and_sign_consolidationtx(self.nodes[0], in_count=enough_inputs-1,
                                                      min_confirmations=enough_confirmations, utxos=utxos_cons[j])
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_decrease, invalid_txs)

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test minconfconsolidationinput")
        # Test that input_tx has enough confirmations
        invalid_txs = falses(4) + [{'reject_reason': "mempool min fee not met"} for _ in range(5)] + falses(4) + [
            {'reject_reason': "mempool min fee not met"} for _ in range(3)]

        overrides_pertx = [None for _ in range(8)] + [config_overrides_increase, None, None, None,
                                                      {"minconfconsolidationinput": 3}, None, None, None]

        txes = []
        utxos_cons = []
        for j in range(16):
            utxos_cons.append(self.create_utxos_value10000(self.nodes[0], enough_inputs, 0))
        self.nodes[0].generate(enough_confirmations)
        for j in range(16):
            tx = self.create_and_sign_consolidationtx(self.nodes[0], in_count=enough_inputs,
                                                      min_confirmations=enough_confirmations, utxos=utxos_cons[j])
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_increase, invalid_txs)

        invalid_txs = (
            [{'reject_reason': "mempool min fee not met"} for _ in range(4)]
            + falses(5)
            + [{'reject_reason': "mempool min fee not met"} for _ in range(4)]
            + falses(3))

        overrides_pertx = [None for _ in range(8)] + [config_overrides_decrease, None, None, None,
                                                      {"minconfconsolidationinput": 15}, None, None, None]
        txes = []
        utxos_cons = []
        for j in range(16):
            utxos_cons.append(self.create_utxos_value10000(self.nodes[0], enough_inputs, 0))
        self.nodes[0].generate(enough_confirmations-1)
        for j in range(16):
            tx = self.create_and_sign_consolidationtx(self.nodes[0], in_count=enough_inputs,
                                                      min_confirmations=enough_confirmations-1, utxos=utxos_cons[j])
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check_batch(txes, config_overrides_decrease, invalid_txs)

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test maxconsolidationinputscriptsize")

        tx_hex = self.create_and_sign_consolidationtx(self.nodes[0], 1, min_confirmations=1)
        tx = FromHex(CTransaction(), tx_hex)
        tx.rehash()
        sin = len(getInputScriptPubKey(self.nodes[0], tx.vin[0], 0))
        sout = len(tx.vout[0].scriptPubKey)

        enough_inputs = sout * self.minconsolidationfactor // sin
        enough_inputs = max(enough_inputs, 2)
        enough_confirmations = self.minconfconsolidationinput
        i_utxo += 1

        invalid_txs = falses(4) + [{'reject_reason': "mempool min fee not met"} for _ in range(5)] + falses(4) + [
            {'reject_reason': "mempool min fee not met"} for _ in range(3)]
        overrides_pertx = [None for _ in range(8)] + [{"maxconsolidationinputscriptsize": 100}, None, None, None,
                                                      {"maxconsolidationinputscriptsize": 151}, None, None, None]
        txes = []
        utxos_cons = []
        for j in range(16):
            utxos_cons.append(self.create_utxos_value10000(self.nodes[0], enough_inputs, 0))
        self.nodes[0].generate(enough_confirmations)
        for j in range(16):
            tx = self.create_and_sign_consolidationtx(self.nodes[0], in_count=enough_inputs,
                                                      min_confirmations=enough_confirmations, utxos=utxos_cons[j])
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]
        self.send_and_check(txes[0:4], invalid_txs=invalid_txs[0:4])
        self.send_and_check(txes[4:8], config_overrides_gen={"maxconsolidationinputscriptsize": 100},
                            invalid_txs=invalid_txs[4:8])
        self.send_and_check(txes[8:12], invalid_txs=invalid_txs[8:12])
        self.send_and_check(txes[12:16], config_overrides_gen={"maxconsolidationinputscriptsize": 100},
                            invalid_txs=invalid_txs[12:16])
        i_utxo += 16

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test acceptnonstdconsolidationinput")
        script = CScript([b"a" * 1000, b"b", OP_CAT])
        invalid_txs = [{'reject_reason': "mempool min fee not met"} for _ in range(4)] + falses(5) + [
            {'reject_reason': "mempool min fee not met"} for _ in range(4)] + falses(3)
        overrides_pertx = [None for _ in range(8)] + [{"acceptnonstdconsolidationinput": True}, None, None, None,
                                                      {"acceptnonstdconsolidationinput": False}, None, None, None]
        txes = []
        utxos_cons = []
        for j in range(16):
            utxos_cons.append(self.create_utxos_value10000(self.nodes[0], enough_inputs, 0, script=script))
        self.nodes[0].generate(enough_confirmations)
        for j in range(16):
            tx = self.create_and_sign_consolidationtx(self.nodes[0], in_count=enough_inputs,
                                                      min_confirmations=enough_confirmations, utxos=utxos_cons[j])
            txes.append({"hex": tx, "config": overrides_pertx[j]})
            invalid_txs[j]['txid'] = str(FromHex(CTransaction(), tx))[18:82]

        self.send_and_check(txes[0:4], invalid_txs=invalid_txs[0:4])
        self.send_and_check(txes[4:8], config_overrides_gen={"acceptnonstdconsolidationinput": True},
                            invalid_txs=invalid_txs[4:8])
        self.send_and_check(txes[8:12], invalid_txs=invalid_txs[8:12])
        self.send_and_check(txes[12:16], config_overrides_gen={"acceptnonstdconsolidationinput": True},
                            invalid_txs=invalid_txs[12:16])

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test skipscriptflags")
        utxos = [utxo for utxo in self.nodes[0].listunspent() if utxo['confirmations'] > 50]
        i_utxo = 0
        # Create and send first transaction which has multiple elements in output's scriptPubKey
        ftx = self.create_transaction_for_skipscriptflags(1000, [OP_1, OP_1, OP_1], utxos[i_utxo], 0, int(utxos[i_utxo]['amount'] * COIN))
        txid = self.nodes[0].sendrawtransaction(ftx)

        tx_one = self.nodes[0].getrawtransaction(txid, 1)
        # Create transaction that spends output from the transaction send above. It's script does not clear stack.
        # It violates the CLEANSTACK policy flag.
        tx = self.create_transaction_for_skipscriptflags(1000, [OP_1], tx_one, 0, int(tx_one['vout'][0]['value'] * COIN))

        # Send the second transaction without skipflags parameter
        res = self.nodes[0].sendrawtransactions(
            [{'hex': tx, 'allowhighfees': False, 'dontcheckfee': False}])
        assert_equal(res["invalid"][0]["reject_code"], 64)
        assert_equal(res["invalid"][0]["reject_reason"],
                     "non-mandatory-script-verify-flag (Script did not clean its stack)")

        # Send the second transaction with flag that are allowed to be skipped, but does not affect cleanstack
        res = self.nodes[0].sendrawtransactions(
            [{'hex': tx, 'allowhighfees': False, 'dontcheckfee': False, 'config': {
                'skipscriptflags': ["DERSIG", "NULLDUMMY", "MINIMALDATA"]}}])
        assert_equal(res["invalid"][0]["reject_code"], 64)
        assert_equal(res["invalid"][0]["reject_reason"],
                     "non-mandatory-script-verify-flag (Script did not clean its stack)")

        # Send the second transaction with flag that are allowed to be skipped
        res = self.nodes[0].sendrawtransactions(
            [{'hex': tx, 'allowhighfees': False, 'dontcheckfee': False, 'config': {
                'skipscriptflags': ["INVALIDFLAG"]}}])
        assert_equal(res["invalid"][0]["reject_code"], 0)
        assert_equal(res["invalid"][0]["reject_reason"],
                     "Provided flag (INVALIDFLAG) is unknown.")

        # Send with incorrect global flag -> Get JSONRPCError
        assert_raises_rpc_error(-8, "Provided flag (NON_EXISTING_FLAG) is unknown.",
                                self.nodes[0].sendrawtransactions,
                                [{'hex': tx, 'allowhighfees': False, 'dontcheckfee': False}],
                                {'skipscriptflags': ["NON_EXISTING_FLAG"]})

        # Send the transaction with global CLEANSTACK flag, and local config (Local config should be accepted)
        res = self.nodes[0].sendrawtransactions(
            [{'hex': tx, 'allowhighfees': False, 'dontcheckfee': False, 'config': {
                'skipscriptflags': ["DERSIG"]}}], {'skipscriptflags': ["CLEANSTACK"]})
        assert_equal(res["invalid"][0]["reject_code"], 64)
        assert_equal(res["invalid"][0]["reject_reason"],
                     "non-mandatory-script-verify-flag (Script did not clean its stack)")

        # Send transaction with local "CLEANSTACK" flag - cleanstack policy rule should be skipped and transaction sent
        res = self.nodes[0].sendrawtransactions(
            [{'hex': tx, 'allowhighfees': False, 'dontcheckfee': False, 'config': {
                'skipscriptflags': ["CLEANSTACK"]}}])
        assert_equal(res, {})

        # Create another transaction and test with global CLEANSTACK flag
        ftx = self.create_transaction_for_skipscriptflags(1000, [OP_1, OP_1, OP_1], utxos[i_utxo+1], 0, int(utxos[i_utxo+1]['amount'] * COIN))
        txid = self.nodes[0].sendrawtransaction(ftx)

        tx_one = self.nodes[0].getrawtransaction(txid, 1)
        tx = self.create_transaction_for_skipscriptflags(1000, [OP_1], tx_one, 0, int(tx_one['vout'][0]['value'] * COIN))

        res = self.nodes[0].sendrawtransactions(
            [{'hex': tx, 'allowhighfees': False, 'dontcheckfee': False}], {'skipscriptflags': ["CLEANSTACK"]})
        assert_equal(res, {})

        self.nodes[0].generate(1)
        assert (len(self.nodes[0].getrawmempool()) == 0), "Not all transactions were mined"
        self.log.info("PASS\n")

        self.log.info("Test reorg scenario")

        height0 = self.nodes[0].getblockchaininfo()['blocks']
        logfile = open(glob.glob(self.options.tmpdir + "/node0/regtest/bitcoind.log")[0])
        from_line = len(logfile.readlines())
        logfile.close()

        txes_in_blocks = {}
        for h in range(height1, height0+1):
            txes_in_blocks[self.nodes[0].getblockbyheight(h, 1)['hash']] = self.nodes[0].getblockbyheight(h, 1)['tx']

        self.stop_node(0)
        sleep(1)
        self.start_node(1)
        self.nodes[1].generate(height0 - height1 + 288) # to avoid safe mode
        self.extra_args[0][19] = "-minconfconsolidationinput=0"
        self.start_node(0)
        connect_nodes_bi(self.nodes, 0,1)
        sync_blocks(self.nodes, timeout=120)
        self.stop_node(1)

        self.nodes[0].generate(1)

        self.log.info("Checking that transactions with the looser transaction specific config override are now rejected, because they do not conform to policy limit")
        rejects = ["datacarrier-size-exceeded", "non-mandatory-script-verify-flag (Script is too big)",
                   "max-script-num-length-policy-limit-violated (Script number overflow)",
                   "too-long-mempool-chain, too many unconfirmed parents,",
                   "too-long-mempool-chain, too many unconfirmed parents which we are not willing to mine",
                   "non-mandatory-script-verify-flag (Stack size limit exceeded)", "tx-size"]

        for msg in rejects:
            # we get 7 reject msgs per the looser tx override -- for all the transactions that were accepted then,
            # but are now under/over policy limit
            self.log.info("Message: " + msg)
            assert count_in_log(self, msg, "/node0", from_line=from_line) >= 7
            self.log.info("OK\n")

        self.log.info("Message: non-mandatory-script-verify-flag (Script did not clean its stack)")
        assert count_in_log(self, "non-mandatory-script-verify-flag (Script did not clean its stack)", "/node0", from_line=from_line) == 2
        self.log.info("OK\n")

        # Children from the first cpfp test were rejected for constructing too long chains now that the original config is used.
        # Check that their parents were left in mempool
        # orig_block = coinbase + parents + children
        self.log.info("Checking that parents of rejected cpfp transactions were left in mempool")
        assert len(set(txes_in_blocks[hash_cpfp_block]).difference(set(self.nodes[0].getrawmempool()).union(txes_to_check_on_reorg))) == 1
        self.log.info("PASS\n")


if __name__ == '__main__':
    SendrawtransactionsSkipFlags().main()
