#!/usr/bin/env python3
# Copyright (c) 2026 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test that a malformed MinerInfo height field (int32 overflow) in a
miner-info transaction is handled by the node when the block is
disconnected during a reorg.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    disconnect_nodes_bi,
    connect_nodes_bi,
    wait_until,
)
from test_framework.script import CScript, OP_0, OP_RETURN, OP_TRUE
from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import (
    ToHex,
    CTxIn,
    CTxOut,
    COutPoint,
    CTransaction,
    ser_uint256,
)
import json


MINERINFO_PROTOCOL_ID = bytearray([0x60, 0x1d, 0xfa, 0xce])


class MinerInfoInvalidHeightTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-disablesafemode=1'],
            ['-disablesafemode=1'],
        ]

    def _create_malformed_minerinfo_block(self, node, bad_height, funding_tx):
        tip = node.getblock(node.getbestblockhash())
        height = tip["height"] + 1

        doc_json = json.dumps({
            "version": "0.3",
            "height": bad_height,
            "minerId": "02" + "aa" * 32,
            "prevMinerId": "02" + "aa" * 32,
            "prevMinerIdSig": "3044" + "02" * 62 + "aa" * 2,
            "revocationKey": "02" + "bb" * 32,
            "prevRevocationKey": "02" + "bb" * 32,
            "prevRevocationKeySig": "3044" + "02" * 62 + "bb" * 2,
        }, separators=(',', ':')).encode('utf8')

        dummy_sig = b'\x00' * 72

        # Miner-info transaction: carries the malformed JSON document.
        mi_tx = CTransaction()
        mi_tx.vin.append(CTxIn(COutPoint(funding_tx.sha256, 0), b'', 0xffffffff))
        mi_tx.vout.append(CTxOut(0, CScript([
            OP_0, OP_RETURN, MINERINFO_PROTOCOL_ID,
            bytes([0x00]), doc_json, dummy_sig,
        ])))
        mi_tx.vout.append(CTxOut(int(49.999 * 100_000_000), CScript([OP_TRUE])))
        mi_tx.calc_sha256()

        # Coinbase with MinerInfo reference pointing to mi_tx.
        coinbase_tx = create_coinbase(height)
        coinbase_tx.vout.append(CTxOut(0, CScript([
            OP_0, OP_RETURN, MINERINFO_PROTOCOL_ID,
            bytes([0x00]),
            ser_uint256(mi_tx.sha256),  # miner-info txid
            b'\x00' * 32,              # mmr_pbh_hash
            dummy_sig,                  # block-bind signature
        ])))
        coinbase_tx.rehash()

        block = create_block(
            int(tip["hash"], 16), coinbase_tx, tip["time"] + 1
        )
        block.vtx.append(mi_tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        return block

    def _reorg_with_malformed_height(self, bad_height, funding_tx):
        """Submit a block with a bad MinerInfo height, then force a reorg
        that disconnects it.  The node must survive and resync."""
        self.log.info("Testing MinerInfo with bad height: %s", bad_height)

        disconnect_nodes_bi(self.nodes, 0, 1)

        tip_height = self.nodes[0].getblockcount()

        block = self._create_malformed_minerinfo_block(
            self.nodes[0], bad_height, funding_tx
        )
        self.nodes[0].submitblock(ToHex(block))
        assert_equal(self.nodes[0].getblockcount(), tip_height + 1)

        self.nodes[1].generate(2)
        assert_equal(self.nodes[1].getblockcount(), tip_height + 2)

        connect_nodes_bi(self.nodes, 0, 1)
        wait_until(
            lambda: self.nodes[0].getblockcount() == tip_height + 2,
            timeout=30,
        )

        assert_equal(
            self.nodes[0].getbestblockhash(),
            self.nodes[1].getbestblockhash(),
        )

    def run_test(self):
        node = self.nodes[0]

        # Mine a funding block with an OP_TRUE coinbase output, then
        # mature it so the miner-info tx can spend it.
        node.generate(1)
        self.sync_all()

        tip = node.getblock(node.getbestblockhash())
        funding_coinbase = create_coinbase(2)
        funding_block = create_block(
            int(tip["hash"], 16), funding_coinbase, tip["time"] + 1
        )
        funding_block.solve()
        node.submitblock(ToHex(funding_block))

        node.generate(100)
        self.sync_all()

        self._reorg_with_malformed_height(2147483648, funding_coinbase)


if __name__ == '__main__':
    MinerInfoInvalidHeightTest().main()
