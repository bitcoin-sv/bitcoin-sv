#!/usr/bin/env python3
# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import P2PEventHandler, COIN
from test_framework.util import wait_until

"""
Test that during IBD a node sends a feefilter to its peers to rate limit the
number of txn INVs it will receive, and on exit from IBD sends another
feefilter to reset the limit.
"""


class MyConnCB(P2PEventHandler):

    def __init__(self):
        super().__init__()
        self.last_feerate = None

    def on_feefilter(self, conn, message):
        super().on_feefilter(conn, message)
        self.last_feerate = message.feerate


class FeeFilterIBD(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):

        # Stop node so we can restart it with our connections
        self.stop_node(0)

        with self.run_node_with_connections("Test feefilter and IBD", 0, [], number_of_connections=1,
                                            cb_class=MyConnCB) as (conn,):

            # Node is currently in IBD.
            # Exact fee filter node will send depends on several things, so just
            # check it's at least 1 cent
            wait_until(lambda: conn.transport.cb.last_feerate is not None
                       and conn.transport.cb.last_feerate >= COIN / 100)
            ibd_feerate = conn.transport.cb.last_feerate

            # Get node out of IBD
            self.nodes[0].generate(1)

            # Node will now send out its real feerate, which will be lower than the IBD rate
            wait_until(lambda: conn.transport.cb.last_feerate < ibd_feerate)


if __name__ == '__main__':
    FeeFilterIBD().main()
