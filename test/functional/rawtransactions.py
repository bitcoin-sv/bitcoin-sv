#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""rawtranscation RPCs QA test.

# Tests the following RPCs:
#    - createrawtransaction
#    - signrawtransaction
#    - sendrawtransaction
#    - decoderawtransaction
#    - getrawtransaction
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import CTransaction
from test_framework.util import *

from io import BytesIO

# Create one-input, one-output, no-fee transaction:


class RawTransactionsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [[],[],[],['-maxmempool=1']]

    def setup_network(self, split=False):
        super().setup_network()
        connect_nodes_bi(self.nodes, 0, 2)

    def run_test(self):
        # prepare some coins for multiple *rawtransaction commands
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[0].generate(101)
        self.sync_all()
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 1.5)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 1.0)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 5.0)
        self.sync_all()
        self.nodes[0].generate(5)
        self.sync_all()

        #
        # sendrawtransaction with missing input #
        #
        inputs = [
            {'txid': "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout': 1}]
        # won't exists
        outputs = {self.nodes[0].getnewaddress(): 4.998}
        rawtx = self.nodes[2].createrawtransaction(inputs, outputs)
        rawtx = self.nodes[2].signrawtransaction(rawtx)

        # This will raise an exception since there are missing inputs
        assert_raises_rpc_error(
            -25, "Missing inputs", self.nodes[2].sendrawtransaction, rawtx['hex'])

        #
        # RAW TX MULTISIG TESTS #
        #
        # 2of2 test
        addr1 = self.nodes[2].getnewaddress()
        addr2 = self.nodes[2].getnewaddress()

        addr1Obj = self.nodes[2].validateaddress(addr1)
        addr2Obj = self.nodes[2].validateaddress(addr2)

        mSigObj = self.nodes[2].addmultisigaddress(
            2, [addr1Obj['pubkey'], addr2Obj['pubkey']])
        mSigObjValid = self.nodes[2].validateaddress(mSigObj)

        # use balance deltas instead of absolute values
        bal = self.nodes[2].getbalance()

        # send 1.2 BTC to msig adr
        txId = self.nodes[0].sendtoaddress(mSigObj, 1.2)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        # node2 has both keys of the 2of2 ms addr., tx should affect the
        # balance
        assert_equal(self.nodes[2].getbalance(), bal + Decimal('1.20000000'))

        # 2of3 test from different nodes
        bal = self.nodes[2].getbalance()
        addr1 = self.nodes[1].getnewaddress()
        addr2 = self.nodes[2].getnewaddress()
        addr3 = self.nodes[2].getnewaddress()

        addr1Obj = self.nodes[1].validateaddress(addr1)
        addr2Obj = self.nodes[2].validateaddress(addr2)
        addr3Obj = self.nodes[2].validateaddress(addr3)

        mSigObj = self.nodes[2].addmultisigaddress(
            2, [addr1Obj['pubkey'], addr2Obj['pubkey'], addr3Obj['pubkey']])
        mSigObjValid = self.nodes[2].validateaddress(mSigObj)

        txId = self.nodes[0].sendtoaddress(mSigObj, 2.2)
        decTx = self.nodes[0].gettransaction(txId)
        rawTx = self.nodes[0].decoderawtransaction(decTx['hex'])
        sPK = rawTx['vout'][0]['scriptPubKey']['hex']
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # THIS IS A INCOMPLETE FEATURE
        # NODE2 HAS TWO OF THREE KEY AND THE FUNDS SHOULD BE SPENDABLE AND
        # COUNT AT BALANCE CALCULATION
        # for now, assume the funds of a 2of3 multisig tx are not marked as
        # spendable
        assert_equal(self.nodes[2].getbalance(), bal)

        txDetails = self.nodes[0].gettransaction(txId, True)
        rawTx = self.nodes[0].decoderawtransaction(txDetails['hex'])
        vout = False
        for outpoint in rawTx['vout']:
            if outpoint['value'] == Decimal('2.20000000'):
                vout = outpoint
                break

        bal = self.nodes[0].getbalance()
        inputs = [{
            "txid": txId,
            "vout": vout['n'],
            "scriptPubKey": vout['scriptPubKey']['hex'],
            "amount": vout['value'],
        }]
        outputs = {self.nodes[0].getnewaddress(): 2.19}
        rawTx = self.nodes[2].createrawtransaction(inputs, outputs)
        rawTxPartialSigned = self.nodes[1].signrawtransaction(rawTx, inputs)
        # node1 only has one key, can't comp. sign the tx
        assert_equal(rawTxPartialSigned['complete'], False)

        rawTxSigned = self.nodes[2].signrawtransaction(rawTx, inputs)
        # node2 can sign the tx compl., own two of three keys
        assert_equal(rawTxSigned['complete'], True)
        self.nodes[2].sendrawtransaction(rawTxSigned['hex'])
        rawTx = self.nodes[0].decoderawtransaction(rawTxSigned['hex'])
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].getbalance(), bal + Decimal(
            '50.00000000') + Decimal('2.19000000'))  # block reward + tx

        # getrawtransaction tests
        # 1. valid parameters - only supply txid
        txHash = rawTx["hash"]
        assert_equal(
            self.nodes[0].getrawtransaction(txHash), rawTxSigned['hex'])

        # 2. valid parameters - supply txid and 0 for non-verbose
        assert_equal(
            self.nodes[0].getrawtransaction(txHash, 0), rawTxSigned['hex'])

        # 3. valid parameters - supply txid and False for non-verbose
        assert_equal(self.nodes[0].getrawtransaction(
            txHash, False), rawTxSigned['hex'])

        # 4. valid parameters - supply txid and 1 for verbose.
        # We only check the "hex" field of the output so we don't need to
        # update this test every time the output format changes.
        assert_equal(self.nodes[0].getrawtransaction(
            txHash, 1)["hex"], rawTxSigned['hex'])

        # 5. valid parameters - supply txid and True for non-verbose
        assert_equal(self.nodes[0].getrawtransaction(
            txHash, True)["hex"], rawTxSigned['hex'])

        # 6. invalid parameters - supply txid and string "Flase"
        assert_raises_rpc_error(
            -3, "Invalid type", self.nodes[0].getrawtransaction, txHash, "False")

        # 7. invalid parameters - supply txid and empty array
        assert_raises_rpc_error(
            -3, "Invalid type", self.nodes[0].getrawtransaction, txHash, [])

        # 8. invalid parameters - supply txid and empty dict
        assert_raises_rpc_error(
            -3, "Invalid type", self.nodes[0].getrawtransaction, txHash, {})

        inputs = [
            {'txid': "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout': 1, 'sequence': 1000}]
        outputs = {self.nodes[0].getnewaddress(): 1}
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        decrawtx = self.nodes[0].decoderawtransaction(rawtx)
        assert_equal(decrawtx['vin'][0]['sequence'], 1000)

        # 9. invalid parameters - sequence number out of range
        inputs = [
            {'txid': "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout': 1, 'sequence': -1}]
        outputs = {self.nodes[0].getnewaddress(): 1}
        assert_raises_rpc_error(
            -8, 'Invalid parameter, sequence number is out of range',
            self.nodes[0].createrawtransaction, inputs, outputs)

        # 10. invalid parameters - sequence number out of range
        inputs = [
            {'txid': "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout': 1, 'sequence': 4294967296}]
        outputs = {self.nodes[0].getnewaddress(): 1}
        assert_raises_rpc_error(
            -8, 'Invalid parameter, sequence number is out of range',
            self.nodes[0].createrawtransaction, inputs, outputs)

        inputs = [
            {'txid': "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout': 1, 'sequence': 4294967294}]
        outputs = {self.nodes[0].getnewaddress(): 1}
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        decrawtx = self.nodes[0].decoderawtransaction(rawtx)
        assert_equal(decrawtx['vin'][0]['sequence'], 4294967294)

        # 11. check if getrawtransaction with verbose 'True' returns blockheight
        assert isinstance(self.nodes[0].getrawtransaction(txHash, True)['blockheight'], int)
        txList = self.nodes[0].getblockbyheight(self.nodes[0].getrawtransaction(txHash, True)['blockheight'])['tx']
        assert txHash in txList

        # tests with transactions containing data
        # 1. sending ffffffff, we get 006a04ffffffff
        # 00(OP_FALSE) 6a(OP_RETURN) 04(size of data, 4 bytes in this case) ffffffff(data)
        addr = self.nodes[0].getnewaddress()
        txid = self.nodes[0].sendtoaddress(addr, 2.0)
        inputs = [{
            "txid": txid,
            "vout": 0,
        }]
        outputs = {
                self.nodes[0].getnewaddress(): 0.5,
                "data": 'ffffffff'
            }
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(rawtx))
        tx.deserialize(f)
        assert_equal(tx.vout[1].scriptPubKey.hex(), "006a04ffffffff")

        # 2. sending ffffffff00000000, we get 006a08ffffffff00000000
        # 00(OP_FALSE) 6a(OP_RETURN) 08(size of data, 8 bytes in this case) ffffffff00000000(data)
        addr = self.nodes[0].getnewaddress()
        txid = self.nodes[0].sendtoaddress(addr, 2.0)
        inputs = [{
            "txid": txid,
            "vout": 0,
        }]
        outputs = {
            self.nodes[0].getnewaddress(): 0.5,
            "data": 'ffffffff00000000'
        }
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx = CTransaction()
        f = BytesIO(hex_str_to_bytes(rawtx))
        tx.deserialize(f)
        assert_equal(tx.vout[1].scriptPubKey.hex(), "006a08ffffffff00000000")

        #
        # Submit transaction without checking fee 1/2 #
        #
        self.nodes[3].generate(101)
        self.sync_all()
        txId = self.nodes[3].sendtoaddress(self.nodes[3].getnewaddress(), 30)
        rawtx = self.nodes[3].getrawtransaction(txId,1)
        # Identify the 30btc output
        nOut = next(i for i, vout in enumerate(rawtx["vout"]) if vout["value"] == Decimal("30"))
        inputs2 = []
        outputs2 = {}
        inputs2.append({"txid": txId, "vout": nOut})
        outputs2 = {self.nodes[3].getnewaddress(): 30}
        raw_tx2 = self.nodes[3].createrawtransaction(inputs2, outputs2)
        tx_hex2 = self.nodes[3].signrawtransaction(raw_tx2)["hex"]
        assert_raises_rpc_error(
            -26, "insufficient priority", self.nodes[3].sendrawtransaction, tx_hex2, False, False
        )
        txid2 = self.nodes[3].sendrawtransaction(tx_hex2, False, True)
        mempool = self.nodes[3].getrawmempool(False)
        assert(txid2 in mempool)

        self.nodes[3].generate(1)
        self.sync_all()
        assert_equal(self.nodes[3].gettransaction(txid2)["txid"], txid2)

        
        #
        # Submit transaction without checking fee 2/2 #
        #
        txouts = gen_return_txouts()
        relayfee = self.nodes[3].getnetworkinfo()['relayfee']

        utxos = create_confirmed_utxos(relayfee, self.nodes[3], 14)
        us0 = utxos.pop()

        base_fee = relayfee * 100
        create_lots_of_big_transactions(self.nodes[3], txouts, utxos, 13, base_fee)

        inputs = [{"txid": us0["txid"], "vout": us0["vout"]}]
        outputs = {}
        outputs = {self.nodes[3].getnewaddress(): us0["amount"]}
        rawtx = self.nodes[3].createrawtransaction(inputs, outputs)
        newtx = rawtx[0:92]
        newtx = newtx + txouts
        newtx = newtx + rawtx[94:]
        signresult = self.nodes[3].signrawtransaction(newtx, None, None, "NONE|FORKID")
        mempoolsize = self.nodes[3].getmempoolinfo()['size']
        assert_raises_rpc_error(
            -26, "insufficient priority", self.nodes[3].sendrawtransaction, signresult["hex"], False, False
        )
        txid_new = self.nodes[3].sendrawtransaction(signresult["hex"], False, True)
        mempoolsize_new = self.nodes[3].getmempoolinfo()['size']
        assert(txid_new in self.nodes[3].getrawmempool())
        assert_equal(mempoolsize_new, mempoolsize)


if __name__ == '__main__':
    RawTransactionsTest().main()
