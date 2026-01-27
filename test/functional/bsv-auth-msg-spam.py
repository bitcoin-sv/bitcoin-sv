#!/usr/bin/env python3
# Copyright (c) 2025 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Test that the node bans a peer that sends too many authch or authresp messages.
'''

from test_framework.mininode import msg_authch, msg_authresp
from test_framework.test_framework import BitcoinTestFramework


class AuthMsgSpam(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = False

    def run_test(self):

        # Stop node so we can restart it with our connections
        self.stop_node(0)

        with self.run_node_with_connections("AuthMsgSpam", 0, ['-minerid=1'], number_of_connections=1) as (conn,):

            # Send enough authch messages to get disconnected/banned
            try:
                for i in range(100):
                    conn.send_message(msg_authch())
            except Exception:
                pass

            # Wait for disconnection
            conn.cb.wait_for_disconnect()

        # Reconnect
        with self.run_node_with_connections("AuthMsgSpam", 0, ['-minerid=1'], number_of_connections=1) as (conn,):

            # Send enough authresp messages to get disconnected/banned
            try:
                for i in range(100):
                    conn.send_message(msg_authresp())
            except Exception:
                pass

            # Wait for disconnection
            conn.cb.wait_for_disconnect()


if __name__ == '__main__':
    AuthMsgSpam().main()
