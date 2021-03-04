#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import threading, json
import http.client as httplib
from functools import partial
from http.server import HTTPServer
from ds_callback_service.CallbackService import CallbackService, RECEIVE, STATUS, RESPONSE_TIME, FLAG
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import p2p_port, check_for_log_msg, assert_equal
from test_framework.cdefs import DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS
from test_framework.mininode import *
from test_framework.script import *

import time

LOCAL_HOST_IPV6 = 0x00000000000000000000000000000001
# 127.0.0.1 as network-order bytes
LOCAL_HOST_IP = 0x7F000001
# 127.0.0.2 as network-order bytes
WRONG_IP = 0x7F000002

class HTTPServerV6(HTTPServer):
        address_family = socket.AF_INET6

class DoubleSpendReport(BitcoinTestFramework):

    def __del__(self):
        self.kill_server()

    def set_test_params(self):
        self.num_nodes = 1
        self.callback_serviceIPv4 = "localhost:8080"
        self.callback_serviceIPv6 = "[::]:8080"
        self.extra_args = [['-dsendpointport=8080',
                            '-banscore=100000',
                            '-genesisactivationheight=1',
                            '-maxscriptsizepolicy=0',
                            "-maxnonstdtxvalidationduration=15000",
                            "-maxtxnvalidatorasynctasksrunduration=15001",
                            "-dsnotifylevel=2"]]

    def start_server(self):
        self.serverThread = threading.Thread(target=self.server.serve_forever)
        self.serverThread.deamon = True
        self.serverThread.start()

    def kill_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.serverThread.join()

    def createConnection(self):
        self.node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], self.node0)
        self.node0.add_connection(connection)

        NetworkThread().start()
        self.node0.wait_for_verack()

    def create_and_send_transaction(self, inputs, outputs):
        tx = CTransaction()
        tx.vin = inputs
        tx.vout = outputs
        tx_hex =  self.nodes[0].signrawtransaction(ToHex(tx))["hex"]
        tx = FromHex(CTransaction(), tx_hex)

        self.node0.send_message(msg_tx(tx))

        return tx

    def check_tx_received(self, tx_hash):
        self.conn.request(
            'GET',
            "/dsnt/1/received"
        )

        queried = self.conn.getresponse()
        assert_equal(queried.status, 200)
        return tx_hash in json.loads(queried.read())

    def check_tx_not_received(self, tx_hash):
        time.sleep(3)
        self.conn.request(
            'GET',
            "/dsnt/1/received"
        )

        queried = self.conn.getresponse()
        assert_equal(queried.status, 200)
        return tx_hash not in json.loads(queried.read())

    # Test that notifying callback server about dsnt-enabled transaction works.
    # Test there are not duplicate notifications for the same transaction.
    def check_ds_enabled(self, utxo):

        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo[0]["txid"], 16), utxo[0]["vout"]), CScript([OP_FALSE]), 0xffffffff),
            CTxIn(COutPoint(int(utxo[1]["txid"], 16), utxo[1]["vout"]), CScript([OP_FALSE]), 0xffffffff),
            CTxIn(COutPoint(int(utxo[3]["txid"], 16), utxo[3]["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            # inputs 0 and 3 are valid, input 9 is out of range
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1,1,[LOCAL_HOST_IP], [0,3,9]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())


        # tx2 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo[0]["txid"], 16), utxo[0]["vout"]), CScript([OP_FALSE]), 0xffffffff),
            CTxIn(COutPoint(int(utxo[2]["txid"], 16), utxo[2]["vout"]), CScript([OP_FALSE]), 0xffffffff),
            CTxIn(COutPoint(int(utxo[3]["txid"], 16), utxo[3]["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Script verification for double-spend passed", "/node0"))
        wait_until(lambda: self.check_tx_received(tx1.hash))

        # tx3 spends the same output as tx1 and tx2 (double spend) --> callback service is not notified twice
        vin = [
            CTxIn(COutPoint(int(utxo[0]["txid"], 16), utxo[0]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx3 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "Already notified about txn {}".format(tx1.hash), "/node0"))


    # Test that notifying callback server does not work for NOT dsnt-enabled transactions.
    # Pass in different output scripts with invalid/missing CallbackMessage.
    def check_ds_not_enabled(self, utxo, output):

        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, output)
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)

        # double spend has been detected
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))

        self.check_tx_not_received(tx1.hash)

    # Test that notifying callback server does not work for if double spend transaction has invalid script.
    def check_invalid_transactions(self, utxo):

        assert(not check_for_log_msg(self, "Script verification for double-spend failed", "/node0"))

        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, 1, [LOCAL_HOST_IP], [0,3]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # Create tx2 manually: signrawtransaction RPC call rewrites input script.
        # Let's tweak input script to be invalid (use non push data). Double spend will still be detected and the script validated.
        tx2 = CTransaction()
        tx2.vin.append(CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_ADD]), 0xffffffff))
        tx2.vout.append(CTxOut(25, CScript([OP_FALSE])))
        tx2.calc_sha256()
        self.node0.send_message(msg_tx(tx2))

        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Script verification for double-spend failed", "/node0"))

        self.check_tx_not_received(tx2.hash)

    # Test that notifying callback server does not work for if double spend transaction validation timed-out.
    # Also test setting dsnotifylevel
    def check_long_lasting_transactions(self):

        assert(not check_for_log_msg(self, "Script verification for double-spend cancelled", "/node0"))

        # Create funding transactions that will provide funds for other transcations
        ftx = CTransaction()
        ftx.vout.append(CTxOut(1000000, CScript([bytearray([42] * DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS), bytearray([42] * 200 * 1000), OP_MUL, OP_DROP, OP_TRUE])))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{ 'changePosition' : len(ftx.vout)})['hex']
        ftxHex = self.nodes[0].signrawtransaction(ftxHex)['hex']
        ftx = FromHex(CTransaction(), ftxHex)
        ftx.rehash()
        self.node0.send_message(msg_tx(ftx))
        wait_until(lambda: ftx.hash in self.nodes[0].getrawmempool())
        self.nodes[0].generate(1)

        # Create transaction that depends on funding transactions that has just been submitted
        vin = [
            CTxIn(COutPoint(ftx.sha256, 0), b'')
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1,1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        self.stop_node(0)
        # Restart bitcoind with parameters that reduce transaction validation time. Also set dsnotifylevel to 1, which means nonstandard transaction will not even validate.
        self.start_node(0, extra_args=['-dsendpointport=8080', '-banscore=100000', '-genesisactivationheight=1', '-maxscriptsizepolicy=0', "-maxnonstdtxvalidationduration=11", "-dsnotifylevel=1"])

        self.createConnection()
        # Create double spend of tx1
        vin = [
            CTxIn(COutPoint(ftx.sha256, 0), b'')
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Ignoring txn {} conflicting input {} because it is non-standard".format(tx2.hash, 0), "/node0"))

        self.stop_node(0)
        # Restart bitcoind with parameters that reduce transaction validation time. Also set dsnotifylevel to 2, which means nonstandard transaction will validate.
        self.start_node(0, extra_args=['-dsendpointport=8080', '-banscore=100000', '-genesisactivationheight=1', '-maxscriptsizepolicy=0', "-maxnonstdtxvalidationduration=11", "-dsnotifylevel=2"])

        self.createConnection()
        vin = [
            CTxIn(COutPoint(ftx.sha256, 0), b'')
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx3 = self.create_and_send_transaction(vin, vout)
        
        wait_until(lambda: check_for_log_msg(self, "Script verification for double-spend was cancelled", "/node0"))

        # Wait for the callback service to process requests
        self.check_tx_not_received(tx3.hash)

    def check_multiple_callback_services(self, utxo):

        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1,2,[LOCAL_HOST_IP, WRONG_IP], [0,3]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())


        # tx2 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Submitted proof ok to 127.0.0.1", "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Error sending slow-queue notification to endpoint 127.0.0.2", "/node0"), timeout=70)


    # Test that proof is not sent if callback server does not want it.
    def check_ds_enabled_no_proof(self, utxo):

        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1,1,[LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())


        # tx2 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "Endpoint doesn't want proof", "/node0"))
        self.check_tx_not_received(tx1.hash)

    def check_ipv6(self, utxo):

        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(129,1,[LOCAL_HOST_IPV6],[0,3]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        tx1.rehash()
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())


        # tx2 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Submitted proof ok to ::1", "/node0"))
        wait_until(lambda: self.check_tx_received(tx1.hash))

    def run_test(self):

        # Turn on CallbackService.
        handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServer(('localhost', 8080), handler)
        self.start_server()
        self.conn = httplib.HTTPConnection(self.callback_serviceIPv4)

        self.nodes[0].generate(110)
        utxo = self.nodes[0].listunspent()

        self.createConnection()

        self.check_ds_enabled(utxo[:4])

        # missing callback message
        self.check_ds_not_enabled(utxo[4], CScript([OP_FALSE, OP_RETURN]))
        # input out of range
        self.check_ds_not_enabled(utxo[5], CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1,1,[LOCAL_HOST_IP], [6]).serialize()]))
        # wrong ip
        self.check_ds_not_enabled(utxo[6], CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1,1,[WRONG_IP], [0]).serialize()]))
        # missing protocol id
        self.check_ds_not_enabled(utxo[7], CScript([OP_FALSE, OP_RETURN, CallbackMessage(1,1,[LOCAL_HOST_IP], [0]).serialize()]))

        self.check_invalid_transactions(utxo[8])

        self.check_multiple_callback_services(utxo[9])
        
        self.check_long_lasting_transactions()

        self.kill_server()

        # Turn on CallbackService.
        handler = partial(CallbackService, RECEIVE.NO, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServer(('localhost', 8080), handler)
        self.start_server()
        self.conn = httplib.HTTPConnection(self.callback_serviceIPv4)

        self.check_ds_enabled_no_proof(utxo[10])

        self.kill_server()

        # Turn on CallbackService that runs on IPv6 address.
        handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServerV6(('::', 8080), handler)
        self.start_server()
        self.conn = httplib.HTTPConnection(self.callback_serviceIPv6)

        self.check_ipv6(utxo[11])

        self.kill_server()

if __name__ == '__main__':
    DoubleSpendReport().main()
