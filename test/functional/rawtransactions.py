#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2020 Bitcoin Association
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""rawtranscation RPCs QA test.

# Tests the following RPCs:
#    - createrawtransaction
#    - signrawtransaction
#    - sendrawtransaction
#    - sendrawtransactions
#    - decoderawtransaction
#    - getrawtransaction
"""
from decimal import Decimal

from test_framework.cdefs import ONE_KILOBYTE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import CTransaction, COIN
from test_framework.util import *

from io import BytesIO

# Create one-input, one-output, no-fee transaction:


class RawTransactionsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.relayfee = Decimal(1) * ONE_KILOBYTE / COIN
        self.extra_args = [['-persistmempool=0'],['-persistmempool=0'],['-persistmempool=0'],
                           ['-persistmempool=0',
                            '-maxmempool=300',
                            '-maxmempoolsizedisk=0',
                            f"-minminingtxfee={self.relayfee}",
                            f"-mindebugrejectionfee={self.relayfee}"]]

    def setup_network(self, split=False):
        super().setup_network()
        connect_nodes_bi(self.nodes, 0, 2)

    def make_data_transaction(self, conn, txn):
        send_value = txn['amount']
        inputs = []
        inputs.append({"txid": txn["txid"], "vout": txn["vout"]})
        outputs = {}
        addr = conn.getnewaddress()
        outputs[addr] = satoshi_round(send_value)
        outputs["data"] = bytes_to_hex_str(bytearray(999000))
        raw= conn.createrawtransaction(inputs, outputs)
        return conn.signrawtransaction(raw)["hex"]

    # Get vout id for specific value from given transaction
    def get_vout(self, conn, txid, value):
        return next(i for i, vout in enumerate(conn.getrawtransaction(txid, 1)["vout"]) if vout["value"] == value)

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
            -25, "Missing inputs", self.nodes[2].sendrawtransaction, rawtx['hex']) # allowhighfees=False, dontcheckfee=False
        assert_raises_rpc_error(
            -25, "Missing inputs", self.nodes[2].sendrawtransaction, rawtx['hex'], False, True) # dontcheckfee=True
        assert_raises_rpc_error(
            -25, "Missing inputs", self.nodes[2].sendrawtransaction, rawtx['hex'], True, False) # allowhighfees=True
        assert_raises_rpc_error(
            -25, "Missing inputs", self.nodes[2].sendrawtransaction, rawtx['hex'], True, True) # allowhighfees=True, dontcheckfee=True

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
        # Identify the 30btc output
        nOut = self.get_vout(self.nodes[3], txId, Decimal("30"))
        inputs2 = []
        outputs2 = {}
        inputs2.append({"txid": txId, "vout": nOut})
        outputs2 = {self.nodes[3].getnewaddress(): 30}
        raw_tx2 = self.nodes[3].createrawtransaction(inputs2, outputs2)
        tx_hex2 = self.nodes[3].signrawtransaction(raw_tx2)["hex"]
        assert_raises_rpc_error(
            -26, "mempool min fee not met", self.nodes[3].sendrawtransaction, tx_hex2, False, False
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
        relayfee = self.relayfee
        base_fee = relayfee * 1000
        utxos = create_confirmed_utxos(relayfee, self.nodes[3], 335)
        # fill up mempool
        for j in range(332):
            txn = utxos.pop()
            send_value = txn['amount'] - base_fee
            inputs = []
            inputs.append({"txid": txn["txid"], "vout": txn["vout"]})
            outputs = {}
            addr = self.nodes[3].getnewaddress()
            outputs[addr] = satoshi_round(send_value)
            outputs["data"] = bytes_to_hex_str(bytearray(900000))

            rawTxn = self.nodes[3].createrawtransaction(inputs, outputs)
            signedTxn = self.nodes[3].signrawtransaction(rawTxn)["hex"]
            self.nodes[3].sendrawtransaction(signedTxn, False, False)

        # create new transaction
        mempoolsize = self.nodes[3].getmempoolinfo()['size']
        signedTxn = self.make_data_transaction(self.nodes[3], utxos.pop())
        # without sufficient fee shouldn't get to mempool
        assert_raises_rpc_error(
            -26, "mempool min fee not met", self.nodes[3].sendrawtransaction, signedTxn, False, False
        )
        txid_new = self.nodes[3].sendrawtransaction(signedTxn, False, True)
        mempoolsize_new = self.nodes[3].getmempoolinfo()['size']
        # with 'dontcheckfee' other txn should be evicted
        assert(txid_new in self.nodes[3].getrawmempool())
        assert_equal(mempoolsize_new, mempoolsize)

        #
        # Submit transactions through sendrawtransactions interface.
        #
        # Test insufficient fee.
        txnhex1 = self.make_data_transaction(self.nodes[3], utxos.pop())
        txnhex2 = self.make_data_transaction(self.nodes[3], utxos.pop())
        txid1 = self.nodes[3].decoderawtransaction(txnhex1)["txid"]
        txid2 = self.nodes[3].decoderawtransaction(txnhex2)["txid"]
        # Check fee.
        rejectedTxns = self.nodes[3].sendrawtransactions([{'hex': txnhex1, 'dontcheckfee': False}, {'hex': txnhex2, 'dontcheckfee': False}])
        assert_equal(len(rejectedTxns), 1)
        assert_equal(len(rejectedTxns['invalid']), 2)
        assert_equal(rejectedTxns['invalid'][0]['reject_code'], 66)
        assert_equal(rejectedTxns['invalid'][0]['reject_reason'], "mempool min fee not met")
        mempool = self.nodes[3].getrawmempool()
        assert(txid1 not in mempool)
        assert(txid2 not in mempool)
        # Don't check fee.
        rejectedTxns = self.nodes[3].sendrawtransactions([{'hex': txnhex1, 'dontcheckfee': True}, {'hex': txnhex2, 'dontcheckfee': True}])
        assert_equal(len(rejectedTxns), 0)
        mempool = self.nodes[3].getrawmempool()
        assert(txid1 in mempool)
        assert(txid2 in mempool)
        # Test listunconfirmedancestors option
        # Create two parents and send one child
        parent_tx_1 = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), Decimal("0.1"))
        parent_tx_1_vout = self.get_vout(self.nodes[0], parent_tx_1, Decimal("0.1"))
        parent_tx_2 = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), Decimal("0.2"))
        parent_tx_2_vout = self.get_vout(self.nodes[0], parent_tx_2, Decimal("0.2"))
        inputs = []
        inputs.append({"txid": parent_tx_1, "vout": parent_tx_1_vout})
        inputs.append({"txid": parent_tx_2, "vout": parent_tx_2_vout})
        outputs = {}
        outputs[self.nodes[0].getnewaddress()] = Decimal("0.09998")
        outputs[self.nodes[0].getnewaddress()] = Decimal("0.2")
        child_tx = self.nodes[0].signrawtransaction(self.nodes[0].createrawtransaction(inputs, outputs))["hex"]
        # sendrawtransactions with listunconfirmedancestors set to true for specified transactions should:
        # - contain "unconfirmed" array of elements with:
        #   - "txid" of the specified transaction
        #   - "ancestors" as an array of transaction's unconfirmed ancestors each containing its txid and inputs
        unconfirmed = self.nodes[0].sendrawtransactions([{'hex': child_tx, 'listunconfirmedancestors': True}])
        wait_until(lambda: unconfirmed["unconfirmed"][0]["txid"] in self.nodes[0].getrawmempool())
        assert_equal(len(unconfirmed), 1)
        assert_equal(len(unconfirmed["unconfirmed"]), 1)
        assert_equal(len(unconfirmed["unconfirmed"][0]["ancestors"]), 2)
        ancestors_txids = []
        for ancestor in unconfirmed["unconfirmed"][0]["ancestors"]:
            ancestors_txids.append(ancestor["txid"])
            assert_equal(len(ancestor["vin"]), 1)
            vin = self.nodes[0].getrawtransaction(ancestor["txid"], 1)["vin"]
            assert_equal(vin[0]["txid"], ancestor["vin"][0]["txid"])
            assert_equal(vin[0]["vout"], ancestor["vin"][0]["vout"])
        assert(parent_tx_1 in ancestors_txids)
        assert(parent_tx_2 in ancestors_txids)


if __name__ == '__main__':
    RawTransactionsTest().main()
