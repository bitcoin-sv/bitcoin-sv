#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test for getting block with REST and RPC call with different options.

Check REST call /rest/block/BLOCKHASH for BIN, HEX and JSON formats.
Content-length should be set for BIN and HEX response, because we know the length.
Check RPC call for verbosity levels 0 (HEX), 1 (JSON without transactions details) and 2 (JSON with transaction details).
Content-length is never set for JSON, because we do not know the length of JSON response.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestInstance
from test_framework.blocktools import *
from test_framework.util import assert_equal, assert_greater_than, json, assert_raises_process_error
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
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        # shorthand for functions
        block = self.chain.next_block
        block(0)
        yield self.accepted()

        test, _, _ = prepare_init_chain(self.chain, 99, 0)

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

        #rpc hex
        assert_equal(ToHex(block), self.nodes[0].getblock(block.hash, 0))

        #rpc json with no tx details
        checkJsonBlock(self.nodes[0].getblock(block.hash, 1), False, block.hash)

        #rpc json with tx details
        checkJsonBlock(self.nodes[0].getblock(block.hash, 2), True, block.hash)

        #check help
        getblockHelpMessage = "getblock \"blockhash\" ( verbosity )"
        assert_equal(getblockHelpMessage in self.nodes[0].help("getblock"), True)

        #check help when wrong parameters are used
        assert_raises_rpc_error(-1, getblockHelpMessage, self.nodes[0].getblock, "a", "b", "c")

        #check getblock errors still work
        assert_raises_rpc_error(
            -5, "Block not found", self.nodes[0].getblock, "somehash")

        #check errors still work
        batch = self.nodes[0].batch([self.nodes[0].getblock.get_request(block.hash),
                                     self.nodes[0].getblock.get_request("somehash"),
                                     self.nodes[0].getblockcount.get_request(),
                                     self.nodes[0].undefinedmethod.get_request()])

        checkJsonBlock(batch[0]["result"], False, block.hash)
        assert_equal(batch[0]["error"], None)
        assert_equal(batch[1]["result"], None)
        assert_equal(batch[1]["error"]["message"], "Block not found")
        assert_equal(batch[2]["error"], None)
        assert_equal(batch[3]["result"], None)
        assert_equal(batch[3]["error"]["message"], "Method not found")


if __name__ == '__main__':
    BSVGetBlock().main()
