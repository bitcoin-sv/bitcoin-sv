#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test GetArgAsBytes human readable unit parser with -maxmempool parameter.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

import os


class GetArgAsBytesTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_chain(self):
        super().setup_chain()
        with open(os.path.join(self.options.tmpdir + "/node0", "bitcoin.conf"), 'a', encoding='utf8') as f:
            f.write("maxmempool=500MB\n")

    def run_test(self):
        # Test that parameters are also settable via the bitcoin.conf file.
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 500000000)

        # No unit (-maxmempool parameter is in mega bytes by default)
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=300"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 300000000)

        # B
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=300000000B"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 300000000)

        # kB
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=300000kB"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 300000000)

        # MB
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=300MB"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 300000000)

        # GB
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=0.3GB"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 300000000)

        # KiB
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=300000KiB"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 307200000)

        # MiB
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=300MiB"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 314572800)

        # GiB
        self.stop_node(0)
        self.start_node(0, extra_args=["-maxmempool=0.3GiB"])
        assert_equal(self.nodes[0].getmempoolinfo()["maxmempool"], 322122547)


if __name__ == '__main__':
    GetArgAsBytesTest().main()
