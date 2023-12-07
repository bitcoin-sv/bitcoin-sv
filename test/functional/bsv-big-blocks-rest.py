#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Test that rest call for /rest/block/BLOCKHASH works for bigger blocks.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.comptool import TestInstance
from test_framework.util import assert_equal, assert_greater_than, json
from test_framework.blocktools import prepare_init_chain
from test_framework.cdefs import ONE_MEGABYTE
import http.client
import urllib.parse


def http_get_call(host, port, path):
    conn = http.client.HTTPConnection(host, port)
    conn.request('GET', path)

    return conn.getresponse()


class BSVBigBlockRestCall(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.FORMAT_SEPARATOR = "."
        self.extra_args = [['-whitelist=127.0.0.1', '-rpcservertimeout=1000', '-blocksizeactivationtime=10']]

    def getLargeBlockWithoutFailing(self, block_hash, block_size):
        url = urllib.parse.urlparse(self.nodes[0].url)

        # /rest/block/BLOCKHASH call
        response = http_get_call(
            url.hostname, url.port, '/rest/block/' + block_hash + self.FORMAT_SEPARATOR + 'json')

        assert_equal(response.status, 200)

        json_string = response.read().decode('utf-8')
        json_obj = json.loads(json_string)
        assert_equal(json_obj['hash'], block_hash)

    def run_test(self):
        self.test.run()

    def get_tests(self):
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        # shorthand for functions
        block = self.chain.next_block

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 99, 0)

        yield test

        blockSize = 200 * ONE_MEGABYTE
        maxBlock = block(1, spend=self.chain.get_spendable_output(), block_size=blockSize)
        yield self.accepted()

        # getting big block with REST should not fail
        self.getLargeBlockWithoutFailing(maxBlock.hash, blockSize)


if __name__ == '__main__':
    BSVBigBlockRestCall().main()
