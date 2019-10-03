#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class RpcEstimateFeeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        # Extra args for each node.
        self.extra_args = [[], ["-minrelaytxfee=0.005"]]
        # Set up clean chain
        self.setup_clean_chain = True

    def run_test(self):
        for i in range(3):
            self.nodes[0].generate(1)
            # estimatefee is 0.00001 by default, regardless of block contents
            assert_equal(self.nodes[0].estimatefee(), Decimal('0.00001'))
            # estimatefee may be different for nodes that set it in their config
            assert_equal(self.nodes[1].estimatefee(), Decimal('0.005'))

if __name__ == '__main__':
    RpcEstimateFeeTest().main()