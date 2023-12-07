#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import threading
import json
import http.client as httplib
from functools import partial
from http.server import HTTPServer
from ds_callback_service.CallbackService import CallbackService, RECEIVE, STATUS, RESPONSE_TIME, FLAG, reset_proofs
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import p2p_port, check_for_log_msg, assert_equal
from test_framework.mininode import *
from test_framework.script import *
import os
import platform
import subprocess
import time

'''
Test standard double-spend notification paths:

1) Double-spend enabled transactions being double-spent, both when the enabled transaction is
   seen first or second.
2) Check no notifications are attempted for various error scenarios such as:
    a) Missing callback message.
    b) Bad callback message.
    c) Input out of range.
    d) Bad IP addresses.
    e) Bad protocol version.
3) Double-spend enabled transactions where the double-spending transaction is invalid.
4) Double-spend enabled transactions where the double-spending transaction takes too long to
   validate.
5) Check multiple notification endpoints all get notified.
6) Check if a notification endpoint declines the proof we don't send it.
7) Check IPv6 support for endpoints.
'''

# ::1 as network-order bytes
LOCAL_HOST_IPV6 = 0x00000000000000000000000000000001
# 127.0.0.1 as network-order bytes
LOCAL_HOST_IP = 0x7F000001
# 127.0.0.2 as network-order bytes
WRONG_IP1 = 0x7F000002
# 127.0.0.3 as network-order bytes
SKIP_IP = 0x7F000003
# 127.0.0.4 as network-order bytes
WRONG_IP2 = 0x7F000004


# Returns True if host (str) responds to a ping6 request.
def ping6(host):
    # option for the number of packets
    param = '-n' if platform.system().lower()=='windows' else '-c'

    # building the command. Ex: "ping -c 1 google.com"
    command = ['ping6', param, '1', host]

    try:
        success = subprocess.call(command) == 0
    except Exception as e:
        logger.info("Pinging IPv6 address not possible on this environment: {} \n".format(e))
        return False

    return success


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
                            '-dsendpointskiplist=127.0.0.3,::3',
                            '-dsendpointmaxcount=3',
                            '-whitelist=127.0.0.1',
                            '-genesisactivationheight=1',
                            '-maxscriptsizepolicy=0',
                            '-maxscriptnumlengthpolicy=250000',
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
        tx_hex = self.nodes[0].signrawtransaction(ToHex(tx))["hex"]
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
            # inputs 0 and 2 are valid, input 9 is out of range
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0,2,9]).serialize()]))
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
        wait_until(lambda: check_for_log_msg(self, "Sending query to 127.0.0.1 for double-spend enabled txn {}".format(tx1.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Got 200 response from endpoint 127.0.0.1", "/node0"))
        wait_until(lambda: self.check_tx_received(tx1.hash))

        # again spend the same output as tx1 and tx2 (double spend) --> callback service is not notified twice
        vin = [
            CTxIn(COutPoint(int(utxo[0]["txid"], 16), utxo[0]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "Already notified about txn {}".format(tx1.hash), "/node0"))

        # tx4 is not dsnt enabled, and gets accepted to the mempool
        vin = [
            CTxIn(COutPoint(int(utxo[2]["txid"], 16), utxo[2]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx4 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx4.hash in self.nodes[0].getrawmempool())

        # tx5 is dsnt enabled and double spends tx4. In this case because the txn in the mempool is not
        # dsnt enabled, then the callback service specified by tx5 will be notified about tx4.
        # Also test notification enabled output in position other than 0.
        vin = [
            CTxIn(COutPoint(int(utxo[2]["txid"], 16), utxo[2]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE])),
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx5 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx5.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Txn {} is DS notification enabled on output 1".format(tx5.hash), "/node0"))
        wait_until(lambda: self.check_tx_received(tx5.hash))

        # tx6 is dsnt enabled and double spends tx1. Both the txn in the mempool and the double-spend are dsnt
        # enabled, so in this case just the callback service specified by tx1 will get notified about tx6
        # (first seen rule).
        reset_proofs()
        self.check_tx_not_received(tx1.hash)
        vin = [
            CTxIn(COutPoint(int(utxo[3]["txid"], 16), utxo[3]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx6 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx6.hash), "/node0"))
        wait_until(lambda: self.check_tx_received(tx1.hash))

        # tx7 is dsnt-enabled on multiple outputs
        vin = [
            CTxIn(COutPoint(int(utxo[4]["txid"], 16), utxo[4]["vout"]), CScript([OP_FALSE]), 0xffffffff),
            CTxIn(COutPoint(int(utxo[5]["txid"], 16), utxo[5]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()])),
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [1]).serialize()]))
        ]
        tx7 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx7.hash in self.nodes[0].getrawmempool())

        # tx8 spends the same outputs as tx7 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo[4]["txid"], 16), utxo[4]["vout"]), CScript([OP_FALSE]), 0xffffffff),
            CTxIn(COutPoint(int(utxo[5]["txid"], 16), utxo[5]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx8 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx8.hash), "/node0"))
        wait_until(lambda: self.check_tx_received(tx7.hash))
        wait_until(lambda: check_for_log_msg(self, "Txn {} is DS notification enabled on output 0".format(tx7.hash), "/node0"))
        assert(not check_for_log_msg(self, "Txn {} is DS notification enabled on output 1".format(tx7.hash), "/node0"))

    # Check double-spend of mempool txn
    def check_ds_mempool_txn(self, utxo):

        # tx1
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(10000, CScript([OP_TRUE]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # tx2 is dsnt-enabled & spends tx1 while still in the mempool
        vin = [
            CTxIn(COutPoint(tx1.sha256, 0), CScript([OP_DROP, OP_TRUE]), 0xffffffff)
        ]
        vout = [
            CTxOut(100, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx2.hash in self.nodes[0].getrawmempool())

        # tx3 spends the same output as tx2 (double spend)
        vin = [
            CTxIn(COutPoint(tx1.sha256, 0), CScript([OP_DROP, OP_TRUE]), 0xffffffff)
        ]
        vout = [
            CTxOut(100, CScript([OP_TRUE]))
        ]
        tx3 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx3.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Verifying script for txn {}".format(tx3.hash), "/node0"))
        wait_until(lambda: self.check_tx_received(tx2.hash))

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
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
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

        self.check_tx_not_received(tx1.hash)

        # Check that another correct double-spend for tx1 does trigger a notification
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: self.check_tx_received(tx1.hash))

    # Test that notifying callback server does not work for if double spend transaction validation timed-out.
    # Also test setting dsnotifylevel
    def check_long_lasting_transactions(self):

        assert(not check_for_log_msg(self, "Script verification for double-spend cancelled", "/node0"))

        # Create funding transactions that will provide funds for other transcations
        ftx = CTransaction()
        ftx.vout.append(CTxOut(1000000, CScript([bytearray([42] * 250000), bytearray([42] * 200 * 1000), OP_MUL, OP_DROP, OP_TRUE])))
        ftxHex = self.nodes[0].fundrawtransaction(ToHex(ftx),{'changePosition' : len(ftx.vout)})['hex']
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
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        self.stop_node(0)
        # Restart bitcoind with parameters that reduce transaction validation time. Also set dsnotifylevel to 1, which means nonstandard transaction will not even validate.
        self.start_node(0, extra_args=['-dsendpointport=8080', '-banscore=100000', '-genesisactivationheight=1', '-maxscriptsizepolicy=0', '-maxscriptnumlengthpolicy=250000',"-maxnonstdtxvalidationduration=11", "-dsnotifylevel=1"])

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
        self.start_node(0, extra_args=['-dsendpointport=8080', '-banscore=100000', '-genesisactivationheight=1', '-maxscriptsizepolicy=0', '-maxscriptnumlengthpolicy=250000',"-maxnonstdtxvalidationduration=11", "-dsnotifylevel=2"])

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
            CTxIn(COutPoint(int(utxo[0]["txid"], 16), utxo[0]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP,WRONG_IP1,SKIP_IP,WRONG_IP2], [0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # tx2 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo[0]["txid"], 16), utxo[0]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Submitted proof ok to 127.0.0.1 for double-spend enabled txn {}".format(tx1.hash), "/node0"))
        if(os.name == "nt"):
            wait_until(lambda: check_for_log_msg(self, "Error sending notification to endpoint 127.0.0.2", "/node0"))
        else:
            wait_until(lambda: check_for_log_msg(self, "Timeout sending slow-queue notification to endpoint 127.0.0.2", "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Skipping notification to endpoint in skiplist 127.0.0.3", "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Maximum number of notification endpoints reached", "/node0"))

        # tx3 has duplicate endpoint IPs
        vin = [
            CTxIn(COutPoint(int(utxo[1]["txid"], 16), utxo[1]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP,LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx3 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx3.hash in self.nodes[0].getrawmempool())

        # tx4 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo[1]["txid"], 16), utxo[1]["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        tx4 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx4.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Submitted proof ok to 127.0.0.1 for double-spend enabled txn {}".format(tx3.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Skipping notification to duplicate endpoint 127.0.0.1", "/node0"))

    # Test that proof is not sent if callback server does not want it.
    def check_ds_enabled_no_proof(self, utxo):

        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # spend the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff)
        ]
        vout = [
            CTxOut(25, CScript([OP_TRUE]))
        ]
        self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "Endpoint 127.0.0.1 doesn't want proof", "/node0"))
        self.check_tx_not_received(tx1.hash)

    def check_ipv6(self, utxo):

        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(129, [LOCAL_HOST_IPV6],[0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        tx1.rehash()
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # spend the same output as tx1 (double spend)
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

    def check_doublespend_queue_size(self, utxo):
        self.stop_node(0)
        # Restart bitcoind with low limit on double-spend queue length
        self.start_node(0, extra_args=['-dsendpointport=8080',
                                       '-whitelist=127.0.0.1',
                                       '-genesisactivationheight=1',
                                       '-maxscriptsizepolicy=0',
                                       '-maxnonstdtxvalidationduration=15000',
                                       '-maxtxnvalidatorasynctasksrunduration=15001',
                                       '-dsattemptqueuemaxmemory=1KB',
                                       '-dsnotifylevel=2'])
        self.createConnection()

        assert(not check_for_log_msg(self, "Dropping new double-spend because the queue is full", "/node0"))

        # tx1 is dsnt-enabled
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            CTxOut(25, CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))
        ]
        tx1 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: tx1.hash in self.nodes[0].getrawmempool())

        # tx2 spends the same output as tx1 (double spend)
        vin = [
            CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]), CScript([OP_FALSE]), 0xffffffff),
        ]
        vout = [
            # Larger size than the queue is configured for
            CTxOut(25, CScript([OP_TRUE] * 1100))
        ]
        tx2 = self.create_and_send_transaction(vin, vout)
        wait_until(lambda: check_for_log_msg(self, "txn= {} rejected txn-mempool-conflict".format(tx2.hash), "/node0"))
        wait_until(lambda: check_for_log_msg(self, "Dropping new double-spend because the queue is full", "/node0"))

    def run_test(self):

        # Turn on CallbackService.
        handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServer(('localhost', 8080), handler)
        self.start_server()
        self.conn = httplib.HTTPConnection(self.callback_serviceIPv4)

        self.nodes[0].generate(120)
        utxo = self.nodes[0].listunspent()

        self.createConnection()

        self.check_ds_enabled(utxo[:6])
        self.check_ds_mempool_txn(utxo[16])

        # missing callback message
        self.check_ds_not_enabled(utxo[7], CScript([OP_FALSE, OP_RETURN]))
        # badly formatted callback messages
        self.check_ds_not_enabled(utxo[8], CScript([OP_FALSE, OP_RETURN, 0x746e7364]))
        self.check_ds_not_enabled(utxo[9], CScript([OP_FALSE, OP_RETURN, 0x746e7364, 0x01FFFFFF]))
        # input out of range
        self.check_ds_not_enabled(utxo[10], CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [LOCAL_HOST_IP], [6]).serialize()]))
        # wrong ip
        self.check_ds_not_enabled(utxo[11], CScript([OP_FALSE, OP_RETURN, 0x746e7364, CallbackMessage(1, [WRONG_IP1], [0]).serialize()]))
        # missing protocol id
        self.check_ds_not_enabled(utxo[12], CScript([OP_FALSE, OP_RETURN, CallbackMessage(1, [LOCAL_HOST_IP], [0]).serialize()]))

        self.check_invalid_transactions(utxo[13])

        self.check_multiple_callback_services(utxo[14:16])

        self.check_long_lasting_transactions()

        self.kill_server()

        # Turn on CallbackService.
        handler = partial(CallbackService, RECEIVE.NO, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.YES)
        self.server = HTTPServer(('localhost', 8080), handler)
        self.start_server()
        self.conn = httplib.HTTPConnection(self.callback_serviceIPv4)

        # Refetch the available utxos because we've restarted the node during the last test
        utxo = self.nodes[0].listunspent()
        self.check_ds_enabled_no_proof(utxo[0])

        # limited double-spend queue size
        self.check_doublespend_queue_size(utxo[1])

        self.kill_server()

        if ping6("::1"):
            # Turn on CallbackService that runs on IPv6 address.
            handler = partial(CallbackService, RECEIVE.YES, STATUS.SUCCESS, RESPONSE_TIME.FAST, FLAG.YES)
            self.server = HTTPServerV6(('::', 8080), handler)
            self.start_server()
            self.conn = httplib.HTTPConnection(self.callback_serviceIPv6)

            self.check_ipv6(utxo[2])

            self.kill_server()
        else:
            logger.info("IPv6 loopback not enabled: test with IPv6 address in CallbackMessage skipped.\n")


if __name__ == '__main__':
    DoubleSpendReport().main()
