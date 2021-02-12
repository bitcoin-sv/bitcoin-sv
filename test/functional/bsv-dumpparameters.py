#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test rpc method dumpparameters. Rpc method dumpparameters returns non-sensitive parameters
set by switches and config file (also includes force set parameters).
Test non-sensitive parameters are also dumped to log file at node startup.

Verify sensitive parameters (rpcuser, rpcpassword, rpcauth) are not dumped to log file and are excluded
from the response.
"""
import glob
import os
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import check_for_log_msg, wait_until


class BSVdumpparameters(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-excessiveblocksize=300MB"]]

    def setup_chain(self):
        super().setup_chain()

        # Append rpcauth to bitcoin.conf before initialization
        with open(os.path.join(self.options.tmpdir + "/node0", "bitcoin.conf"), 'a', encoding='utf8') as f:
            f.write("rpcauth=user:salt\n")
            f.write("rpcuser=user\n")
            f.write("rpcpassword=password\n")

    def run_test(self):
        response = self.nodes[0].cli("-rpcuser=user", "-rpcpassword=password", "-rpcauth=user:salt").dumpparameters()

        assert 'debug' in response
        assert 'regtest=1' in response
        assert 'excessiveblocksize=300MB' in response
        assert 'rpcuser=user' not in response
        assert 'rpcpassword=password' not in response
        assert 'rpcauth=user:salt' not in response

        # checked only non sensitive paremters are written to log at node startup
        def wait_for_log():
            nonsensitive_parameters = ["debug", "regtest=1", "excessiveblocksize=300MB"]
            sensitive_parameters = ["rpcuser=user", "rpcpassword=password", "rpcauth=user:salt"]

            for nonsensitive in nonsensitive_parameters:
                if not check_for_log_msg(self, "[main] " + nonsensitive, "/node0"):
                    return False

            for sensitive in sensitive_parameters:
                if check_for_log_msg(self, "[main] " + sensitive, "/node0"):
                    return False
            return True

        wait_until(wait_for_log, check_interval=0.5)


if __name__ == '__main__':
    BSVdumpparameters().main()
