#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Verify that consolidation transactions pass if they do not pay any fee and also
test if they are rejected if not fulfilling all criteria for a consolidation transaction
This test creates "spendable by anyone" scripts to easely tweak the script sizes
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.script import CScript, OP_NOP, OP_DROP, OP_2DROP, OP_TRUE, SIGHASH_FORKID, SIGHASH_ANYONECANPAY, SIGHASH_NONE
from test_framework.util import assert_raises_rpc_error, satoshi_round, assert_equal, bytes_to_hex_str
from test_framework.mininode import ToHex, FromHex, CTransaction, CTxOut, CTxIn, COutPoint, uint256_from_str, hex_str_to_bytes, COIN
from decimal import Decimal

class ConsolidationP2PKHTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.utxo_test_sats = 100000
        self.utxo_test_bsvs = satoshi_round(self.utxo_test_sats / COIN)
        self.blockmintxfee_sats = 500
        self.minrelaytxfee_sats = 250
        self.extra_args = [[
            "-whitelist=127.0.0.1",
            "-minrelaytxfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
            "-blockmintxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
            "-minconsolidationfactor=2",
            "-maxconsolidationinputscriptsize=151",
            "-minconfconsolidationinput=5",
            "-acceptnonstdtxn=1",
            "-acceptnonstdconsolidationinput=1"
            ],[
            "-whitelist=127.0.0.1",
            "-minrelaytxfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
            "-blockmintxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
            "-minconsolidationfactor=10",
            "-acceptnonstdtxn=1",
            "-acceptnonstdconsolidationinput=1"
            ],[
            "-whitelist=127.0.0.1",
            "-minrelaytxfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
            "-blockmintxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
            "-minconsolidationfactor=0",  #disables consolidation factor
            "-acceptnonstdtxn=1",
            "-acceptnonstdconsolidationinput=1"
            ],[
            "-whitelist=127.0.0.1",
            "-minrelaytxfee={}".format(Decimal(self.minrelaytxfee_sats)/COIN),
            "-blockmintxfee={}".format(Decimal(self.blockmintxfee_sats)/COIN),
            "-minconsolidationfactor=10",
            "-acceptnonstdtxn=1",
            "-acceptnonstdconsolidationinput=0" # default, disable non std inputs
        ]]

    def test_extra_args_values (self):
        # Check that all exra args are read correction
        network_info = self.nodes[0].getnetworkinfo()
        assert(int(network_info['minconsolidationfactor']) == 2)
        assert(int(network_info['maxconsolidationinputscriptsize']) == 151)
        assert(int(network_info['minconfconsolidationinput']) == 5)
        assert(network_info['acceptnonstdconsolidationinput'] is True)

    def create_utxos_value100000(self, node, utxo_count, utxo_size, min_confirmations):

        utxos = []
        addr = node.getnewaddress()

        # create some confirmed UTXOs
        for i in range (utxo_count):
            txid = node.sendtoaddress(addr, self.utxo_test_bsvs)
            tx = FromHex(CTransaction(), node.getrawtransaction(txid))
            tx.rehash()
            utxos.append(tx)

        node.generate(1)

        # Convert those utxos to new UTXO's in one single transaction that anyone can spend
        tx = None
        fee = 0

        for _ in range(2): # firs iteration is to calculate the fee
            tx = CTransaction()
            amount = self.utxo_test_sats
            check_size = 0
            for u in utxos:

                # Each UTXO will have two outputs, one for change and another one
                # amounting to roughly 10000 satoshis
                for i in range(len(u.vout)):
                    uu = u.vout[i]
                    if uu.nValue <=  self.utxo_test_sats and uu.nValue > self.utxo_test_sats // 2:
                        tx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(u.hash)[::-1]), i), b''))
                        break

                adjust = 2
                scriptPubKey = CScript([OP_DROP] + ([OP_NOP] * ((utxo_size // utxo_count) - adjust)) + [OP_TRUE])

                if len(tx.vin) == utxo_count:
                    amount = amount - fee

                    while True:
                        scriptPubKey = CScript([OP_DROP] + ([OP_NOP] * ((utxo_size // utxo_count) - adjust)) + [OP_TRUE])

                        if check_size + len(scriptPubKey) > utxo_size:
                            adjust = adjust + 1
                            continue
                        elif check_size + len(scriptPubKey) < utxo_size:
                            adjust = adjust - 1
                            continue
                        break

                check_size = check_size + len(scriptPubKey)
                tx.vout.append(CTxOut(amount, scriptPubKey))

            assert (len(tx.vout) == utxo_count)
            assert (check_size == utxo_size)

            # sign and send transaction
            txHex = node.signrawtransaction(ToHex(tx))['hex']
            tx = FromHex(CTransaction(), txHex)
            tx.rehash()
            tx_size = len(ToHex(tx))
            fee = int(self.blockmintxfee_sats * tx_size / 1000)

        node.sendrawtransaction(ToHex(tx))

        if min_confirmations > 0:
            node.generate(min_confirmations)

        return tx

    def create_and_sign_tx(self, node, in_count, out_count, in_size, out_size, min_confirmations, spam):

        utx = self.create_utxos_value100000 (node, in_count, in_size, min_confirmations)
        sum_values_sats = 0
        assert (len(utx.vout) == in_count)
        tx = CTransaction()
        for i in range(in_count):
            u = utx.vout[i]
            sum_values_sats = sum_values_sats + u.nValue
            tx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(utx.hash)[::-1]), i), b''))

            flags = bytes(bytearray([SIGHASH_NONE | SIGHASH_ANYONECANPAY | SIGHASH_FORKID]))
            adjust = len(bytes_to_hex_str(flags)) + 1 # plus one for one OP_HOP

            while True:
                scriptSig = CScript([bytes(bytearray([OP_NOP]) * (spam - adjust)) + flags])
                tx.vin[-1].scriptSig = scriptSig

                if len(scriptSig) > spam:
                    adjust = adjust + 1
                    continue
                elif len(scriptSig) < spam:
                    adjust = adjust - 1
                    continue

                break

        assert (len(tx.vin) == in_count)

        x = out_size // out_count
        x_rest = out_size % out_count

        check_size = 0

        for i in range(out_count):

            if i == out_count - 1:
                x = x_rest + x

            scriptPubKey = CScript([OP_NOP] * (x - 1) + [OP_TRUE])
            check_size = check_size + len(scriptPubKey)

            amount = sum_values_sats // (out_count - i)
            tx.vout.append(CTxOut(amount, scriptPubKey))
            sum_values_sats = sum_values_sats - amount


        assert (check_size == out_size)
        tx.rehash()
        return ToHex(tx)

    # send 2 p2pkh transactions that must fail
    # and one that must succeed
    def run_test(self):

        self.test_extra_args_values()
        output_counts = [1, 5]
        single_output_script_sizes = [25,50]
        for node in self.nodes:
            node.generate(200)
            for output_count in output_counts:
                for single_output_script_size in single_output_script_sizes:
                    network_info = node.getnetworkinfo()
                    self.consolidation_factor = int(network_info['minconsolidationfactor'])
                    self.scriptSigSpam = int(network_info['maxconsolidationinputscriptsize'])
                    self.minConfirmations = int(network_info['minconfconsolidationinput'])
                    self.acceptNonStandardInputs = network_info['acceptnonstdconsolidationinput']
                    self.log.info ("consolidation factor: {}".format(self.consolidation_factor))
                    self.log.info ("scriptSig limit: {}".format( self.scriptSigSpam))
                    self.log.info("minimum input confirmations: {}".format( self.minConfirmations))
                    self.log.info("accept non-std consolidation inputs: {}".format( self.acceptNonStandardInputs))
                    self.log.info ("output_count: {}".format( output_count))
                    self.log.info ("single_output_script_size: {}".format( single_output_script_size))

                    enough_inputs = output_count * self.consolidation_factor
                    not_spam = self.scriptSigSpam
                    cumulated_outputsize = single_output_script_size * output_count
                    enough_cumulated_inputsize = cumulated_outputsize * self.consolidation_factor
                    enough_confirmations = self.minConfirmations

                    # if the minconsolidationfactor is 0 then consolidation transactions are disabled and
                    # below transactions should fail
                    # if the acceptnonstdconsolidationnput is set to 0 then following transactions should
                    # also fail because our inputs are not standard in this test.

                    # should fail. Our minimum/maximum calculations above fail for this factor
                    # hence we need to build the parameters differently. e.g. instead of
                    # insize becomes enough cumulated inputsize
                    # we do
                    # insize becomes cumulated outputsize times two
                    if self.consolidation_factor == 0:
                        tx_hex = self.create_and_sign_tx(node,
                                                         in_count = output_count * 2,
                                                         out_count = output_count,
                                                         in_size=cumulated_outputsize * 2,
                                                         out_size = cumulated_outputsize,
                                                         min_confirmations = enough_confirmations,
                                                         spam = not_spam
                                                         )

                        assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                        self.log.info ("test 1 - failing cnonsolidation transaction not disabled: PASS")
                        break

                    if self.acceptNonStandardInputs is False:
                        tx_hex = self.create_and_sign_tx(node,
                                                         in_count = output_count * 2,
                                                         out_count = output_count,
                                                         in_size=cumulated_outputsize * 2,
                                                         out_size = cumulated_outputsize,
                                                         min_confirmations = enough_confirmations,
                                                         spam = not_spam
                                                         )

                        assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                        self.log.info ("test 2 - failing cnonsolidation transaction may not have std inputs: PASS")
                        break

                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count = enough_inputs - 1,
                                                     out_count = output_count,
                                                     in_size=enough_cumulated_inputsize,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations = enough_confirmations,
                                                     spam = not_spam
                                                     )

                    assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                    self.log.info ("test 1 - failing input_sizes > consolidation_factor * output_size: PASS")

                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count=enough_inputs,
                                                     out_count=output_count,
                                                     in_size=enough_cumulated_inputsize,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations=enough_confirmations - 1,
                                                     spam = not_spam
                                                     )

                    assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                    self.log.info("test 2 - failing all inputs min conf > min conf: PASS")

                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count=enough_inputs,
                                                     out_count=output_count,
                                                     in_size=enough_cumulated_inputsize - 1,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations=enough_confirmations,
                                                     spam = not_spam
                                                     )

                    assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                    self.log.info("test 3 - faling cumulated input sizes > consolidation factor times output sizes: PASS")

                    # FAILING CONDITION: no confirmed inputs
                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count=enough_inputs,
                                                     out_count=output_count,
                                                     in_size=enough_cumulated_inputsize,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations=enough_confirmations,
                                                     spam = not_spam + 1
                                                     )

                    assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                    self.log.info("test 4 - failed scriptSig's of transaction < spam limit: PASS")

                    # ALL CONDITIONS MET: must succeed
                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count=enough_inputs,
                                                     out_count=output_count,
                                                     in_size=enough_cumulated_inputsize,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations=enough_confirmations,
                                                     spam = not_spam
                                                     )
                    txid = node.sendrawtransaction(tx_hex)
                    node.generate(1)
                    tx = node.getrawtransaction(txid, 1)
                    confirmations = tx.get('confirmations', 0)
                    assert_equal (confirmations, 1)
                    self.log.info("test 5 - all consolidation conditions met:PASS")

if __name__ == '__main__':
    ConsolidationP2PKHTest().main()
