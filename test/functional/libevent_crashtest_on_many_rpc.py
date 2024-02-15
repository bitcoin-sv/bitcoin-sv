#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Test many rpc calls against bitcoind to reveal a
# crash regression introduced by libevent-2.1.7

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

import http.client
import urllib.parse
import requests
import json


class RpcFloddingTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):

        #
        # low-level check for http persistent connection
        #
        url = urllib.parse.urlparse(self.nodes[0].url)
        authpair = url.username + ':' + url.password
        headers = {"Authorization": "Basic " + str_to_b64str(authpair)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        out1 = conn.getresponse().read()
        assert(b'"error":null' in out1)
        assert(conn.sock != None)
        # according to http/1.1 connection must still be open!

        # communication will crash in following while loop if libevent-2.1.7 is used. 2.1.11 will not crash
        # looping with 100 will sometimes succeed and sometimes fail with libevent-2.1.7 hence using 2000
        for _ in range(2000):
            r = requests.get("http://" + url.hostname + ":" + str(url.port), auth=(url.username, url.password), data=json.dumps({}))


if __name__ == '__main__':
    RpcFloddingTest().main()
