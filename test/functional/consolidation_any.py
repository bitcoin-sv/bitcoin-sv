#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Verify that consolidation transactions are free uless they do not meet below conditions.

Definition of a consolidation transaction.
====================================================
      A consolidation transaction is one that reduces the number of UTXO's by a margin that
      is more valuable for the nework than the current fee for that transaction. Additionally
      free consolidation transactions shall not be suitable for Denial of Service attack.
      The profitability of DoS attacks is reduced by increasing the consolidation factor

Minimal requirements for a consolidation transactions
====================================================

    - Every utxo used in the transaction needs to have been confirmed.
    - The number of transaction inputs must exceed the number of transaction outputs.
    - The cumulated number of input script sizes must exceed the number of cumulated
      output script sizes by a configurable factor (default is 20)

Remarks
====================================================

    - A consolidation transaction is not guaranteed to be mined in the next potential 
      block of a miner because it may still be rejected by a full mempool.
    - If this test fails, it does not prove something went wrong. It could mean
      that new conditions of other projects overrule the consolidation transaction
      rules

Implementation Details
====================================================
      This test creates "spendable by anyone" scripts to easely tweak the
      script sizes
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.script import CScript, OP_NOP, OP_DROP, OP_TRUE, SIGHASH_FORKID, SIGHASH_ANYONECANPAY, SIGHASH_NONE
from test_framework.util import assert_raises_rpc_error, satoshi_round, assert_equal, bytes_to_hex_str
from test_framework.mininode import ToHex, FromHex, CTransaction, CTxOut, CTxIn, COutPoint, uint256_from_str, hex_str_to_bytes

class ConsolidationP2PKHTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.COIN = 100000000
        self.utxo_test_sats = 10000
        self.utxo_test_bsvs = satoshi_round(self.utxo_test_sats / self.COIN)
        self.blockmintxfee_sats = 500
        self.blockmintxfee_bsvs = satoshi_round(self.blockmintxfee_sats / self.COIN)
        self.mintxrelayfee_sats = 250
        self.mintxrelayfee_bsvs = satoshi_round(self.mintxrelayfee_sats / self.COIN)
        self.extra_args = [[
            "-whitelist=127.0.0.1",
            "-mintxrelayfee={}".format(self.mintxrelayfee_sats),
            "-minblocktxfee={}".format(self.blockmintxfee_sats),
            "-mintxconsolidationfactor=2",
            "-acceptnonstdtxn=1",
            "-relaypriority=1"
            "-txindex=1"
            ],[
            "-whitelist=127.0.0.1",
            "-mintxrelayfee={}".format(self.mintxrelayfee_sats),
            "-minblocktxfee={}".format(self.blockmintxfee_sats),
            #"-mintxconsolidationfactor=4",  #testing default setting, which is 20
            "-acceptnonstdtxn=1",
            "-relaypriority=1"
            "-txindex=1"
        ]]

    def create_utxos_value10000(self, node, utxo_count, min_confirmations):

        utxos = []
        addr = node.getnewaddress()
        for i in range (utxo_count):
            txid = node.sendtoaddress(addr, self.utxo_test_bsvs)
            tx = FromHex(CTransaction(), node.getrawtransaction(txid))
            tx.rehash()
            utxos.append(tx)

        node.generate(1)


        # Convert those utxos to new utxo's that anyone can spend
        tx = CTransaction()
        scriptPubKey = CScript([OP_DROP, OP_TRUE])
        fee = (107 + 25) * utxo_count + 200
        for u in utxos:

            for i in range(len(u.vout)):
                uu = u.vout[i]
                if uu.nValue <=  self.utxo_test_sats and uu.nValue > self.utxo_test_sats // 2:
                    tx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(u.hash)[::-1]), i), b''))
                    break

            amount = self.utxo_test_sats
            if len(tx.vin) == utxo_count:
                amount = amount - fee
                tx.vout.append(CTxOut(amount, scriptPubKey))
                break
            else:
                tx.vout.append(CTxOut(amount, scriptPubKey))


        # sign and send transaction
        txHex = node.signrawtransaction(ToHex(tx))['hex']
        tx = FromHex(CTransaction(), txHex)
        tx.rehash()
        node.sendrawtransaction(ToHex(tx))

        if min_confirmations > 0:
            node.generate(min_confirmations)

        return tx

    def create_and_sign_tx(self, node, in_count, out_count, in_size, out_size, min_confirmations):

        utx = self.create_utxos_value10000 (node, in_count, min_confirmations)
        sum_values_sats = 0

        check_size = 0

        assert (len(utx.vout) == in_count)
        tx = CTransaction()
        for i in range(in_count):
            u = utx.vout[i]
            sum_values_sats = sum_values_sats + u.nValue
            tx.vin.append(CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(utx.hash)[::-1]), i), b''))

            flags = bytes(bytearray([SIGHASH_NONE | SIGHASH_ANYONECANPAY | SIGHASH_FORKID]))
            adjust = len(bytes_to_hex_str(flags)) + 1 # plus one for one OP_HOP

            while True:
                scriptSig = CScript([bytes(bytearray([OP_NOP]) * ((in_size // in_count) - adjust)) + flags])
                tx.vin[-1].scriptSig = scriptSig

                if i == in_count - 1:

                    if check_size + len(scriptSig) > in_size:
                        adjust = adjust + 1
                        continue
                    elif check_size + len(scriptSig) < in_size:
                        adjust = adjust - 1
                        continue

                check_size = check_size + len(scriptSig)
                break

        assert (len(tx.vin) == in_count)
        assert (check_size == in_size)

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


        output_counts = [1,4,5]
        single_output_script_sizes = [25,50]
        for node in self.nodes:
            node.generate(300)
            for output_count in output_counts:
                for single_output_script_size in single_output_script_sizes:
                    self.consolidation_factor = int(node.getnetworkinfo()['mintxconsolidationfactor'])
                    print ("consolidation factor", self.consolidation_factor)
                    print ("single_output_script_size", single_output_script_size)
                    print ("output_count", output_count)

                    enough_inputs = output_count + 1
                    cumulated_outputsize = single_output_script_size * output_count
                    enough_cumulated_inputsize = cumulated_outputsize * self.consolidation_factor + 1
                    enough_confirmations = 1

                    output_count = 1
                    enough_inputs = output_count + 1
                    cumulated_outputsize = single_output_script_size * output_count
                    enough_cumulated_inputsize = cumulated_outputsize * self.consolidation_factor + 1
                    enough_confirmations = 1

                    # FAILING CONDITION: input_sizes <= consolidation_factor * output_size
                    # We assume scriptSig ~ 4 * scriptPubKey
                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count = enough_inputs - 1,
                                                     out_count = output_count,
                                                     in_size=enough_cumulated_inputsize,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations = enough_confirmations
                                                     )

                    assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                    print ("test 1: PASS")

                    # FAILING CONDITION: no confirmed inputs
                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count=enough_inputs,
                                                     out_count=output_count,
                                                     in_size=enough_cumulated_inputsize,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations=enough_confirmations - 1
                                                     )

                    assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                    print("test 2: PASS")

                    # FAILING CONDITION: no confirmed inputs
                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count=enough_inputs,
                                                     out_count=output_count,
                                                     in_size=enough_cumulated_inputsize - 1,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations=enough_confirmations
                                                     )

                    assert_raises_rpc_error(-26, "66: insufficient priority", node.sendrawtransaction, tx_hex)
                    print("test 3: PASS")

                    # ALL CONDITIONS MET: must succeed
                    tx_hex = self.create_and_sign_tx(node,
                                                     in_count=enough_inputs,
                                                     out_count=output_count,
                                                     in_size=enough_cumulated_inputsize,
                                                     out_size = cumulated_outputsize,
                                                     min_confirmations=enough_confirmations
                                                     )
                    txid = node.sendrawtransaction(tx_hex)
                    node.generate(1)
                    tx = node.getrawtransaction(txid, 1)
                    confirmations = tx.get('confirmations', 0)
                    assert_equal (confirmations, 1)
                    print ("test 4: PASS")

if __name__ == '__main__':
    ConsolidationP2PKHTest().main()
