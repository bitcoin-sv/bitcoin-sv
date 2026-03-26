#!/usr/bin/env python3
# Copyright (c) 2026 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test that a malformed MinerID height field in a coinbase document is
handled by the node when the block is disconnected during a reorg.

Two variants are tested:
  - height as a non-numeric string
  - height as an int32-overflowing number
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    disconnect_nodes_bi,
    connect_nodes_bi,
    wait_until,
)
from test_framework.script import CScript, OP_0, OP_RETURN
from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import ToHex, CTxOut
import json


class MinerIdInvalidHeightTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-disablesafemode=1'],
            ['-disablesafemode=1'],
        ]

    def _create_malformed_minerid_block(self, node, bad_height):
        tip = node.getblock(node.getbestblockhash())
        height = tip["height"] + 1

        coinbase_tx = create_coinbase(height)

        doc = {
            "version": "0.1",
            "height": bad_height,
            "prevMinerId": "02" + "00" * 32,
            "prevMinerIdSig": "00" * 64,
            "minerId": "02" + "00" * 32,
            "vctx": {"txId": "00" * 32, "vout": 0},
        }
        doc_json = json.dumps(doc, separators=(',', ':')).encode('utf8')
        dummy_sig = b'\x00' * 72

        protocol_id = bytearray([0xac, 0x1e, 0xed, 0x88])
        minerid_script = CScript(
            [OP_0, OP_RETURN, protocol_id, doc_json, dummy_sig]
        )

        coinbase_tx.vout.append(CTxOut(0, minerid_script))
        coinbase_tx.rehash()

        block = create_block(
            int(tip["hash"], 16), coinbase_tx, tip["time"] + 1
        )
        block.solve()
        return block

    def _reorg_with_malformed_height(self, bad_height):
        """Submit a block with a bad MinerID height, then force a reorg that
        disconnects it.  The node must survive and resync."""
        self.log.info("Testing with bad height: %s", bad_height)

        disconnect_nodes_bi(self.nodes, 0, 1)

        tip_height = self.nodes[0].getblockcount()

        malformed_block = self._create_malformed_minerid_block(self.nodes[0], bad_height)
        self.nodes[0].submitblock(ToHex(malformed_block))
        assert_equal(self.nodes[0].getblockcount(), tip_height + 1)

        self.nodes[1].generate(2)
        assert_equal(self.nodes[1].getblockcount(), tip_height + 2)

        connect_nodes_bi(self.nodes, 0, 1)
        wait_until(lambda: self.nodes[0].getblockcount() == tip_height + 2, timeout=30)

        assert_equal(self.nodes[0].getbestblockhash(),
                     self.nodes[1].getbestblockhash())

    def run_test(self):
        self.nodes[0].generate(101)
        self.sync_all()

        self._reorg_with_malformed_height("INVALID_NOT_A_NUMBER")
        self._reorg_with_malformed_height(2147483648)


if __name__ == '__main__':
    MinerIdInvalidHeightTest().main()
