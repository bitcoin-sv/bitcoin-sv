#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mining RPCs

- getblocktemplate proposal mode
- submitblock"""

from binascii import b2a_hex
import copy

from test_framework.blocktools import create_coinbase
from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import CBlock, CTransaction
from test_framework.util import *
from io import BytesIO

def b2x(b):
    return b2a_hex(b).decode('ascii')  # b2a - binary 2 ascii


def assert_template(node, block, expect, rehash=True):
    if rehash:
        block.hashMerkleRoot = block.calc_merkle_root()
    print("Calling getminingcandidate()")
    rsp = node.getminingcandidate()
    #print("block=", block)
    #print("rps=", block)
    #print("expected=", expect)
    assert_equal(rsp, expect)

# This merely calls getminingcandidate. It does not test submitminingsolution.
# It is not really an adequate test for these new features.

class MiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = False

    def run_test(self):
        node = self.nodes[0]

        tmpl = node.getminingcandidate()
        self.log.info("getminingcandidate: Test capability advertised")
        #print("----------------------------------------------------------")
        #print(tmpl)
        #print("----------------------------------------------------------")
        assert 'id' in tmpl
        assert 'coinbase' in tmpl
        assert 'prevhash' in tmpl
        assert 'version' in tmpl
        assert 'nBits' in tmpl
        assert 'time' in tmpl

if __name__ == '__main__':
    MiningTest().main()
