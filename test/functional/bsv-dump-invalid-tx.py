#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

import json
from time import sleep
import os
import shutil

from test_framework.blocktools import create_block, serialize_script_num, create_coinbase, create_transaction
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, ToHex, ser_string, COIN
from test_framework.script import CScript, OP_FALSE, OP_DROP, OP_HASH160, hash160, OP_EQUAL
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import (wait_until, assert_raises_rpc_error, bytes_to_hex_str, check_zmq_test_requirements,
                                 zmq_port)


def create_invalid_coinbase(height, outputValue=50):
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff),
                              ser_string(serialize_script_num(height)), 0xffffffff))
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = outputValue * COIN
    halvings = int(height / 150)  # regtest
    coinbaseoutput.nValue >>= halvings
    coinbaseoutput.scriptPubKey = CScript([OP_FALSE])
    coinbase.vout = [coinbaseoutput]
    coinbase.calc_sha256()
    return coinbase


def new_block(connection, txs=[], valid_coinbase=False, wait_for_confirmation=True):
    tip = connection.rpc.getblock(connection.rpc.getbestblockhash())

    coinbase_tx = create_coinbase(tip["height"] + 1) if valid_coinbase else create_invalid_coinbase(tip["height"] + 1)
    coinbase_tx.rehash()

    block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
    block.vtx.extend(txs)
    block.hashMerkleRoot = block.calc_merkle_root()
    block.solve()

    connection.send_message(msg_block(block))
    if wait_for_confirmation:
        wait_until(lambda: connection.rpc.getbestblockhash() == block.hash, timeout=10)

    return coinbase_tx, block


def make_invalid_tx(tx_to_spend, output_ndx):
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, output_ndx), b"", 0xffffffff))
    tx.vout.append(CTxOut(tx_to_spend.vout[0].nValue - 2000, CScript([OP_FALSE])))
    tx.rehash()
    return tx


def make_invalid_p2sh_tx(tx_to_spend, output_ndx):
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, output_ndx), b"", 0xffffffff))
    tx.vout.append(CTxOut(tx_to_spend.vout[0].nValue - 2000, CScript([OP_HASH160, hash160(b'123'), OP_EQUAL])))
    tx.rehash()
    return tx


def make_large_invalid_tx(tx_to_spend, output_ndx):
    tx = CTransaction()
    tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, output_ndx), b"", 0xffffffff))
    tx.vout.append(CTxOut(tx_to_spend.vout[0].nValue - 2000000, CScript([bytes(1000000), OP_DROP, OP_FALSE])))
    tx.rehash()
    return tx


class InvalidTx(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):

        # Check that bitcoin has been built with ZMQ enabled and we have python zmq package installed.
        check_zmq_test_requirements(self.options.configfile,
                                    SkipTest("bitcoind has not been built with zmq enabled."))
        # import zmq when we know we have the requirements for test with zmq.
        import zmq

        self.zmqContext = zmq.Context()
        self.zmqSubSocket = self.zmqContext.socket(zmq.SUB)
        self.zmqSubSocket.set(zmq.RCVTIMEO, 60000)
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"invalidtx")
        self.address = f"tcp://127.0.0.1:{zmq_port(0)}"

        self.add_nodes(self.num_nodes)

    def assert_number_of_files(self, number):
        invalidtxsfolder = os.path.join(self.nodes[0].datadir, "regtest", "invalidtxs")

        wait_until(lambda: os.path.isdir(invalidtxsfolder),
                   check_interval=0.5,
                   timeout=30,
                   label=f"waiting for folder to be created")
        wait_until(lambda: len([name for name in os.listdir(invalidtxsfolder)]) == number,
                   check_interval=0.5,
                   timeout=30,
                   label=f"waiting for invalid transaction be written")
        return [name for name in os.listdir(invalidtxsfolder)]

    def assert_number_of_files_with_substring_in_name(self, number, name_substring):
        invalidtxsfolder = os.path.join(self.nodes[0].datadir, "regtest", "invalidtxs")
        wait_until(lambda: os.path.isdir(invalidtxsfolder),
                   check_interval=0.5,
                   timeout=30,
                   label=f"waiting for folder to be created")
        wait_until(lambda: len([name for name in os.listdir(invalidtxsfolder) if name_substring in name]) == number,
                   check_interval=0.5,
                   timeout=30,
                   label=f"waiting for invalid transaction be written name_substring:{name_substring}, number:{number}")
        return [name for name in os.listdir(invalidtxsfolder) if name_substring in name]

    def check_stored_tx_file(self, filename, *args, **kwargs):
        with open(os.path.join(self.nodes[0].datadir, "regtest", "invalidtxs", filename)) as json_file:
            data = json.load(json_file)
            return self.check_message(data, *args, **kwargs)

    def check_message(self, data, tx, block=None, has_hex=None, rejectionFlags=None, rejectionReason=None, source=None, collidedTx=None, **kwargs):
        assert {"fromBlock", "txid", "isInvalid", "isValidationError", "isMissingInputs", "isDoubleSpendDetected",
                "isMempoolConflictDetected", "isNonFinal", "isValidationTimeoutExceeded",
                "isStandardTx", "rejectionCode", "rejectionReason", "rejectionTime"}.issubset(set(data.keys()))

        assert data["txid"] == tx.hash

        if data["fromBlock"]:
            assert {"origins", "blockhash", "blocktime", "blockheight"}.issubset(set(data.keys()))
            if block is not None:
                assert isinstance(data["origins"], list)
                if source is not None:
                    assert any(s["source"] == source for s in data["origins"])
                assert data["blockhash"] == block.hash
                assert data["blocktime"] == block.nTime

        else:
            assert "source" in data.keys()
            if source is not None:
                assert data["source"] == source

        self.log.info(f"{data}")
        assert "collidedWith" in data

        if collidedTx is not None:
            assert len(data["collidedWith"]) > 0
            assert data["collidedWith"][0]["txid"] == collidedTx.hash
            assert "size" in data["collidedWith"][0]
        else:
            assert len(data["collidedWith"]) == 0

        if has_hex is not None:
            if has_hex:
                assert "hex" in data
                assert data["hex"] == bytes_to_hex_str(tx.serialize()), f'{data["hex"]} != {bytes_to_hex_str(tx.serialize())}'
            else:
                assert "hex" not in data

            if collidedTx is not None:
                if has_hex:
                    assert "hex" in data["collidedWith"][0]
                    collidedHex = data["collidedWith"][0]["hex"]
                    assert collidedHex == bytes_to_hex_str(collidedTx.serialize()), f'{collidedHex} != {bytes_to_hex_str(collidedTx.serialize())}'
                else:
                    assert "hex" not in data["collidedWith"][0]

        if rejectionFlags is not None:
            for flag in ["isInvalid", "isValidationError", "isMissingInputs", "isDoubleSpendDetected", "isMempoolConflictDetected",
                         "isNonFinal", "isValidationTimeoutExceeded", "isStandardTx"]:
                expected_flag_val = flag in rejectionFlags
                assert data[flag] == expected_flag_val, f"Flag {flag} expected value is {expected_flag_val}, got {data[flag]}"

        if rejectionReason is not None:
            assert rejectionReason in data["rejectionReason"], f"Rejection reason should contain '{rejectionReason}', got '{data['rejectionReason']}'"

        return data

    def run_test(self):

        invalid_coinbases = []
        valid_coinbases = []
        with self.run_node_with_connections("Preparaton", 0, ["-genesisactivationheight=1"], 1) as (conn,):
            for _ in range(5):
                coinbase, _ = new_block(conn, valid_coinbase=False, wait_for_confirmation=True)
                invalid_coinbases.append(coinbase)
            for _ in range(5):
                coinbase, _ = new_block(conn, valid_coinbase=True, wait_for_confirmation=True)
                valid_coinbases.append(coinbase)
            new_block(conn)
            conn.rpc.generate(100)

        invalidtxsfolder = os.path.join(self.nodes[0].datadir, "regtest", "invalidtxs")

        with self.run_node_with_connections("Scenario 1: txs with failing scripts passed through: block, p2p and rpc (script validation executed)",
                                            0,
                                            ["-genesisactivationheight=1",
                                             "-banscore=100000",
                                             "-invalidtxsink=ZMQ",
                                             "-invalidtxsink=FILE",
                                             f"-zmqpubinvalidtx={self.address}"],
                                            1) as (conn,):
            self.zmqSubSocket.connect(self.address)
            invalid_tx1 = make_invalid_tx(invalid_coinbases[0], 0)
            _, block = new_block(conn, [invalid_tx1], wait_for_confirmation=False)
            conn.cb.sync_with_ping()
            conn.send_message(msg_tx(invalid_tx1))
            conn.cb.sync_with_ping()
            assert_raises_rpc_error(-26, "mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack element)",
                                    conn.rpc.sendrawtransaction, ToHex(invalid_tx1))

            self.assert_number_of_files(3)
            filenames = self.assert_number_of_files_with_substring_in_name(3, invalid_tx1.hash) #all three should have txid in the filename

            for fn, source, has_hex in zip(sorted(filenames), ["ProcessBlockMessage", "p2p", "rpc"], [True, False, False]):
                self.check_stored_tx_file(filename=fn, tx=invalid_tx1, block=block, rejectionFlags=["isInvalid"], source=source, has_hex=has_hex,
                                          rejectionReason="Script evaluated without error but finished with a false/empty top stack element")

            for _ in range(3): # three ZMQ messages should arrive
                msg = self.zmqSubSocket.recv_multipart()
                assert msg[0] == b"invalidtx"
                data = json.loads(msg[1])
                self.check_message(data, tx=invalid_tx1, block=block, rejectionFlags=["isInvalid"], has_hex=True,
                                   rejectionReason="Script evaluated without error but finished with a false/empty top stack element")

        with self.run_node_with_connections("Scenario 2: invalid txs passed through block, p2p, rpc (no script validation)",
                                            0,
                                            ["-genesisactivationheight=1",
                                             "-banscore=100000",
                                             "-invalidtxsink=FILE",],
                                            1) as (conn,):
            invalid_tx2 = make_invalid_p2sh_tx(invalid_coinbases[0], 0)
            _, block = new_block(conn, [invalid_tx2], wait_for_confirmation=False)
            conn.send_message(msg_tx(invalid_tx2))
            assert_raises_rpc_error(-26, "bad-txns-vout-p2sh",
                                    conn.rpc.sendrawtransaction, ToHex(invalid_tx2))

            self.assert_number_of_files(6) #three from previous run, three from this
            filenames =self.assert_number_of_files_with_substring_in_name(3, invalid_tx2.hash)  # three from this run should have txid

            for fn in filenames:
                self.check_stored_tx_file(filename=fn, tx=invalid_tx2, block=block, rejectionFlags=["isInvalid"],
                                          rejectionReason="bad-txns-vout-p2sh")

            conn.rpc.clearinvalidtransactions()
            self.assert_number_of_files(0)

        with self.run_node_with_connections("Scenario 3: double spend and mempool conflict",
                                            0,
                                            ["-genesisactivationheight=1",
                                             "-banscore=100000",
                                             "-invalidtxsink=FILE",],
                                            1) as (conn,):

            valid_tx_1 = create_transaction(valid_coinbases[0], 0, CScript(), valid_coinbases[0].vout[0].nValue - 200)
            valid_tx_2 = create_transaction(valid_coinbases[1], 0, CScript(), valid_coinbases[0].vout[0].nValue - 200)

            _, block = new_block(conn, [valid_tx_1], wait_for_confirmation=True)
            conn.rpc.sendrawtransaction(ToHex(valid_tx_2))

            doublespend_tx = create_transaction(valid_coinbases[0], 0, CScript(), valid_coinbases[0].vout[0].nValue - 201)
            mempool_conflict_tx = create_transaction(valid_coinbases[1], 0, CScript(), valid_coinbases[0].vout[0].nValue - 201)

            conn.send_message(msg_tx(doublespend_tx))
            conn.send_message(msg_tx(mempool_conflict_tx))

            filenames_double_spend = self.assert_number_of_files_with_substring_in_name(1, doublespend_tx.hash)
            self.check_stored_tx_file(filename=filenames_double_spend[0], tx=doublespend_tx, block=None,
                                      rejectionFlags=["isInvalid", "isMissingInputs"], rejectionReason="")
            filenames_mempool_conflict = self.assert_number_of_files_with_substring_in_name(1, mempool_conflict_tx.hash)
            self.check_stored_tx_file(filename=filenames_mempool_conflict[0], tx=mempool_conflict_tx, block=None,
                                      rejectionFlags=["isInvalid", "isMempoolConflictDetected"], rejectionReason="txn-mempool-conflict",
                                      collidedTx=valid_tx_2, has_hex=True)

            freed_size = conn.rpc.clearinvalidtransactions()
            assert freed_size > 0, "Freed size must be greater than zero"
            self.assert_number_of_files(0)
            freed_size = conn.rpc.clearinvalidtransactions()
            assert freed_size == 0, "Nothing to free."

        with self.run_node_with_connections("Scenario 4: Limiting file size and putting three large (1MB each) transactions, "
                                            "only first two txs are saved through file sink, last is ignored, "
                                            "maximal zmq message size is limited, messages should be smaller",
                                            0,
                                            ["-genesisactivationheight=1",
                                             "-banscore=100000",
                                             "-invalidtxsink=FILE",
                                             "-invalidtxfilemaxdiskusage=5",
                                             "-invalidtxfileevictionpolicy=IGNORE_NEW",
                                             "-invalidtxsink=ZMQ",
                                             f"-zmqpubinvalidtx={self.address}",
                                             "-invalidtxzmqmaxmessagesize=1",
                                             ],
                                            1) as (conn,):
            self.zmqSubSocket.connect(self.address)
            freed_size = conn.rpc.clearinvalidtransactions()
            assert freed_size == 0, "Freed size must zero, dumped transactions are deleted in last test."

            invalid_tx1 = make_large_invalid_tx(invalid_coinbases[0], 0)
            conn.send_message(msg_tx(invalid_tx1))
            self.assert_number_of_files_with_substring_in_name(1, invalid_tx1.hash)

            invalid_tx2 = make_large_invalid_tx(invalid_coinbases[1], 0)
            conn.send_message(msg_tx(invalid_tx2))
            self.assert_number_of_files_with_substring_in_name(1, invalid_tx2.hash)

            invalid_tx3 = make_large_invalid_tx(invalid_coinbases[2], 0)
            conn.send_message(msg_tx(invalid_tx3))

            # each transaction is a bit over 1MB, that means its hex written to the file is bit over 2MB
            # max disk usage is set to 5 MB so we have space for only two transactions
            # eviction policy is set to IGNORE_NEW so last transaction will not be saved
            self.assert_number_of_files_with_substring_in_name(1, invalid_tx1.hash)
            self.assert_number_of_files_with_substring_in_name(1, invalid_tx2.hash)
            self.assert_number_of_files_with_substring_in_name(0, invalid_tx3.hash)

            for _ in range(3): # three ZMQ messages should arrive
                msg = self.zmqSubSocket.recv_multipart()
                # maximal zmq tx size is set 1MB so messages will not contain tx hex (tx hex > 2MB)
                assert len(msg[1]) < ONE_MEGABYTE, "messages should be smaller than one megabyte"

        with self.run_node_with_connections("Scenario 5: Limiting file size and putting three large (1MB each) transactions, "
                                            "first is evicted and other two are saved through file sink, "
                                            "maximal zmq message size is unlimited",
                                            0,
                                            ["-genesisactivationheight=1",
                                             "-banscore=100000",
                                             "-invalidtxsink=FILE",
                                             "-invalidtxfilemaxdiskusage=5",
                                             "-invalidtxfileevictionpolicy=DELETE_OLD",
                                             "-invalidtxsink=ZMQ",
                                             f"-zmqpubinvalidtx={self.address}",
                                             "-invalidtxzmqmaxmessagesize=0",
                                             ],
                                            1) as (conn,):
            self.zmqSubSocket.connect(self.address)
            freed_size = conn.rpc.clearinvalidtransactions()
            assert freed_size > 0, "Freed size must be larger than zero."

            invalid_tx1 = make_large_invalid_tx(invalid_coinbases[0], 0)
            conn.send_message(msg_tx(invalid_tx1))
            names = self.assert_number_of_files_with_substring_in_name(1, invalid_tx1.hash)
            sleep(1)

            invalid_tx2 = make_large_invalid_tx(invalid_coinbases[1], 0)
            conn.send_message(msg_tx(invalid_tx2))
            names = self.assert_number_of_files_with_substring_in_name(1, invalid_tx2.hash)
            sleep(1)

            invalid_tx3 = make_large_invalid_tx(invalid_coinbases[2], 0)
            conn.send_message(msg_tx(invalid_tx3))
            names = self.assert_number_of_files_with_substring_in_name(1, invalid_tx3.hash)
            sleep(1)

            # each transaction is a bit over 1MB, that means its hex written to the file is bit over 2MB
            # max disk usage is set to 5 MB so we have space for only two transactions
            # eviction policy is set to DELETE_OLD so first (oldest) transaction will be deleted
            self.assert_number_of_files_with_substring_in_name(0, invalid_tx1.hash)
            self.assert_number_of_files_with_substring_in_name(1, invalid_tx2.hash)
            self.assert_number_of_files_with_substring_in_name(1, invalid_tx3.hash)

            for _ in range(3): # three ZMQ messages should arrive
                msg = self.zmqSubSocket.recv_multipart()
                # maximal zmq tx size is set to msximal value so messages will contain tx hex (tx hex > 2MB)
                assert len(msg[1]) > 2 * ONE_MEGABYTE

        with self.run_node_with_connections("Scenario 6: ",
                                            0,
                                            ["-genesisactivationheight=1",
                                             "-banscore=100000",
                                             "-invalidtxsink=FILE",],
                                            2) as (conn1, conn2):

            conn.rpc.clearinvalidtransactions()

            invalid_tx1 = make_invalid_p2sh_tx(invalid_coinbases[0], 0)
            invalid_tx2 = make_invalid_p2sh_tx(invalid_coinbases[1], 0)
            _, block = new_block(conn1, [invalid_tx1, invalid_tx2], wait_for_confirmation=False)

            # altdough we have sent two invalid transactions in the same block only one is reported
            # because block validation stops as soon as one invalid transaction is found
            self.assert_number_of_files(1)

            invalid_tx3 = make_invalid_p2sh_tx(invalid_coinbases[2], 0)
            invalid_tx4 = make_invalid_p2sh_tx(invalid_coinbases[3], 0)
            conn1.send_message(msg_tx(invalid_tx3))
            conn2.send_message(msg_tx(invalid_tx4))

            fnames1 = self.assert_number_of_files_with_substring_in_name(1, invalid_tx3.hash)
            fnames2 = self.assert_number_of_files_with_substring_in_name(1, invalid_tx4.hash)

            data1 = self.check_stored_tx_file(fnames1[0], invalid_tx3)
            data2 = self.check_stored_tx_file(fnames2[0], invalid_tx4)

            # transactions are sent from different connections so they must have different addresses
            assert data1["address"] != data2["address"]

        shutil.rmtree(invalidtxsfolder)

        with self.run_node_with_connections("Scenario 7: Sink is not specified, folder should not be created",
                                            0,
                                            ["-genesisactivationheight=1",
                                             "-banscore=100000"],
                                            1) as (conn,):
            invalid_tx1 = make_large_invalid_tx(invalid_coinbases[1], 0)
            new_block(conn, [invalid_tx1], wait_for_confirmation=False)
            sleep(1)
            assert not os.path.isdir(invalidtxsfolder)


if __name__ == '__main__':
    InvalidTx().main()
