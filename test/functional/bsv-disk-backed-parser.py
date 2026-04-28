#!/usr/bin/env python3
# Copyright (c) 2026 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test the disk-backed message parser feature.

The disk-backed parser spills large P2P message payloads to temporary files on
disk when their size exceeds the -maxreceivebuffer threshold, instead of
buffering them entirely in memory.

This test:
    - Configures a node with a small -maxreceivebuffer (10 KB).
    - Sends a P2P transaction whose serialised size is below the threshold and
      verifies it is processed without triggering disk-backed storage.
    - Sends a P2P transaction whose serialised size is above the threshold and
      verifies it is processed correctly via the disk-backed parser.
    - Checks log messages to confirm which path was taken.
    - Validates that temporary files are cleaned up after message processing.
"""

import os

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import msg_tx, CTxOut
from test_framework.util import wait_until, check_for_log_msg
from test_framework.script import CScript, OP_TRUE, OP_FALSE, OP_RETURN, OP_DROP, OP_CHECKSIG, SignatureHash, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.key import CECKey
from test_framework.blocktools import create_block, create_coinbase, create_tx, msg_block


# Threshold in kilobytes (the -maxreceivebuffer value).  With the default
# multiplier of ONE_KILOBYTE (1000) this gives a 10000 byte threshold.
MAX_RECV_BUFFER_KB = 10


class DiskBackedParserTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.locking_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])

        self.nodeArgs = ['-genesisactivationheight=1',
                         '-maxreceivebuffer=%d' % MAX_RECV_BUFFER_KB,
                         '-debug=net']

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def make_coinbase(self, conn):
        """Mine a block with a coinbase paying to our key so we can spend it."""
        tip = conn.rpc.getblock(conn.rpc.getbestblockhash())
        coinbase_tx = create_coinbase(tip["height"] + 1, self.coinbase_pubkey)
        coinbase_tx.rehash()
        block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + 1)
        block.solve()
        conn.send_message(msg_block(block))
        wait_until(lambda: conn.rpc.getbestblockhash() == block.hash, timeout=int(30 * self.options.timeoutfactor))
        return coinbase_tx

    def sign_tx(self, tx, spend_tx, n):
        sighash = SignatureHash(spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript(
            [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    def make_tx(self, spend_tx, data_size):
        """Create a transaction spending *spend_tx* with an OP_RETURN output of
        *data_size* bytes so that the serialised P2P message exceeds (or stays
        below) the threshold as required."""
        value = spend_tx.vout[0].nValue - 10000  # leave room for fee
        tx = create_tx(spend_tx, 0, value, script=CScript([OP_DROP, OP_TRUE]))
        tx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN, bytearray([0x00] * data_size)])))
        self.sign_tx(tx, spend_tx, 0)
        tx.rehash()
        return tx

    def p2p_tmp_dir(self):
        return os.path.join(self.nodes[0].datadir, "regtest", "p2p_recv_tmp")

    def tmp_file_count(self):
        d = self.p2p_tmp_dir()
        if not os.path.isdir(d):
            return 0
        return len(os.listdir(d))

    # ------------------------------------------------------------------
    # Test body
    # ------------------------------------------------------------------

    def run_test(self):
        # Get out of IBD
        self.nodes[0].generate(1)

        # Stop node so we can restart it with our P2P connection
        self.stop_node(0)

        with self.run_node_with_connections("Disk-backed parser test", 0, self.nodeArgs, 1) as (conn,):

            assert conn.connected

            # Create a spendable coinbase and mature it
            coinbase_tx = self.make_coinbase(conn)
            self.nodes[0].generate(100)

            # Confirm the p2p_recv_tmp directory exists (created at startup)
            assert os.path.isdir(self.p2p_tmp_dir()), "p2p_recv_tmp directory should exist after node start"

            # ----------------------------------------------------------
            # 1. Small transaction – should stay in memory (no disk file)
            # ----------------------------------------------------------
            self.log.info("Sending small transaction (below threshold)")
            small_tx = self.make_tx(coinbase_tx, data_size=100)  # ~300 bytes total
            conn.send_message(msg_tx(small_tx))
            wait_until(lambda: small_tx.hash in conn.rpc.getrawmempool(), timeout=int(30 * self.options.timeoutfactor))
            self.log.info("Small transaction accepted into mempool")

            # No disk-backed parser log line should have appeared for this txn
            assert not check_for_log_msg(self, "Creating disk-backed parser", "/node0"), "Small message should NOT trigger disk-backed parser"

            # No temporary files should be present
            assert self.tmp_file_count() == 0, "No temp files expected after small message"

            # ----------------------------------------------------------
            # 2. Large transaction – should be written to disk
            # ----------------------------------------------------------
            self.log.info("Sending large transaction (above threshold)")
            # 20 000 bytes of OP_RETURN data – well above the 10,000 byte threshold
            large_tx = self.make_tx(small_tx, data_size=20000)
            conn.send_message(msg_tx(large_tx))
            wait_until(lambda: large_tx.hash in conn.rpc.getrawmempool(), timeout=int(30 * self.options.timeoutfactor))
            self.log.info("Large transaction accepted into mempool")

            # The disk-backed parser log line should now be present
            wait_until(lambda: check_for_log_msg(self, "Creating disk-backed parser", "/node0"), timeout=int(10 * self.options.timeoutfactor))
            self.log.info("Confirmed disk-backed parser was used for large message")

            # Disk write completion log should also be present
            wait_until(lambda: check_for_log_msg(self, "Disk message write complete", "/node0"), timeout=int(10 * self.options.timeoutfactor))
            self.log.info("Confirmed disk write completed")

            # ----------------------------------------------------------
            # 3. Validate file cleanup – temp files removed after use
            # ----------------------------------------------------------
            # After the message has been fully processed the parser (and its
            # RAII unique_path wrapper) should have been destroyed, removing
            # the temporary file.
            wait_until(lambda: self.tmp_file_count() == 0, timeout=int(10 * self.options.timeoutfactor))
            self.log.info("Temporary files cleaned up after processing")

        # After the node shuts down the directory-level cleanup should fire
        assert not os.path.isdir(self.p2p_tmp_dir()), "p2p_recv_tmp directory should be removed on shutdown"
        self.log.info("p2p_recv_tmp directory removed on shutdown")


if __name__ == '__main__':
    DiskBackedParserTest().main()
