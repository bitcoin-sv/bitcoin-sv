#!/usr/bin/env python3
# Copyright (c) 2021 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test rpc method dumpparameters. Rpc method dumpparameters returns non-sensitive parameters
set by switches and config file (also includes force set parameters).

Verify sensitive parameters (rpcuser, rpcpassword, rpcauth) are excluded from the response.
"""
import os
from test_framework.test_framework import BitcoinTestFramework


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


if __name__ == '__main__':
    BSVdumpparameters().main()
