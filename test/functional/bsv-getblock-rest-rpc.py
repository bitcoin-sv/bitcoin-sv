#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test for getting block with REST and RPC call with different options.
Check REST call /rest/block/BLOCKHASH for BIN, HEX and JSON formats.
Content-length should be set for BIN and HEX response, because we know the length.
Content-length is never set for JSON, because we do not know the length of JSON response.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestInstance
from test_framework.blocktools import *
from test_framework.util import assert_equal, assert_greater_than, json
from test_framework.cdefs import ONE_MEGABYTE, ONE_GIGABYTE
from test_framework.mininode import ToHex
import http.client
import urllib.parse

def http_get_call(host, port, path):
    conn = http.client.HTTPConnection(host, port)
    conn.request('GET', path)

    return conn.getresponse()

def checkJsonBlock(json_obj, showTxDetails, hash):
    assert_equal(json_obj['hash'], hash)
    assert_equal("hash" in json_obj["tx"][0], showTxDetails)

class BSVGetBlock(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True        
        self.FORMAT_SEPARATOR = "."

    def getBlock(self, block, block_size, form):
        url = urllib.parse.urlparse(self.nodes[0].url)

        # /rest/block/BLOCKHASH call
        response = http_get_call(
            url.hostname, url.port, '/rest/block/' + block.hash + self.FORMAT_SEPARATOR + form)

        assert_equal(response.status, 200)

        if form == "json":
            checkJsonBlock(json.loads(response.read().decode('utf-8')), True, block.hash)
            assert_equal(response.getheader('content-length'), None)

            responseNoTx = http_get_call(
                url.hostname, url.port, '/rest/block/notxdetails/' + block.hash + self.FORMAT_SEPARATOR + form)

            checkJsonBlock(json.loads(responseNoTx.read().decode('utf-8')), False, block.hash)
            assert_equal(responseNoTx.getheader('content-length'), None)
        elif form == "bin":
            assert_equal(response.getheader('content-length'), str(block_size))
            assert_equal(block.serialize(), response.read())
        elif form == "hex":
            assert_equal(response.getheader('content-length'), str(block_size*2))
            assert_equal(ToHex(block), response.read().decode('utf-8'))
            
    def run_test(self):
        self.test.run()

    def get_tests(self):
        node = self.nodes[0]
        self.chain.set_genesis_hash( int(node.getbestblockhash(), 16) )

        # shorthand for functions
        block = self.chain.next_block

        # Create a new block
        block(0)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(99):
            block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        block_size = 100000
        block = block(1, spend=self.chain.get_spendable_output(), block_size=block_size)
        yield self.accepted()

        #rest json
        self.getBlock(block, block_size, "json")

        #rest binary
        self.getBlock(block, block_size, "bin")

        #rest hex
        self.getBlock(block, block_size, "hex")

if __name__ == '__main__':
    BSVGetBlock().main()
