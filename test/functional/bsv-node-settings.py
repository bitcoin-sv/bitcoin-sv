#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test RPC method getsettings that returns expected values that are used when constructing/accepting a block
or a transaction.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class BSVNodeSettings(BitcoinTestFramework):

    def set_test_params(self):

        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[]]

    def test_getsettings(self, parameters):

        node_params = []
        for param in parameters:
            node_params.append(f"-{param}={parameters[param]}")

        self.stop_node(0)
        self.start_node(0, node_params)

        node_settings = self.nodes[0].getsettings()
        # check we received expected number of fields (parameter settings)
        # we substract 1 because we cannot use -minconfconsolidationinput and deprecated switch -minconsolidationinputmaturity
        # at same time
        assert_equal(len(node_settings)-1, len(parameters))

        # check that set policy parameters return expected values
        for param in parameters:
            if param == "limitdescendantsize" or param == "limitancestorsize":
                # above paremeters are set as multiple of 1000 e.g. 1000*n
                assert_equal(node_settings[param], parameters[param] * 1000)
            elif param == "maxorphantxsize":
                # maxorphantxsize is set in megabytes but we receive size in bytes
                assert_equal(node_settings[param], parameters[param] * 1000000)
            else:
                assert_equal(node_settings[param], parameters[param])

    def run_test(self):

        parameters1 = {'excessiveblocksize': 9223372036854775807,
                       'blockmaxsize': 9223372036854775807,
                       'maxtxsizepolicy': 10000000,
                       'datacarriersize': 4294967295,
                       'maxscriptsizepolicy': 10000,
                       'maxopsperscriptpolicy': 2000000000,
                       'maxscriptnumlengthpolicy': 250000,
                       'maxpubkeyspermultisigpolicy': 4294967295,
                       'maxtxsigopscountspolicy': 4294967295,
                       'maxstackmemoryusagepolicy': 100000000,
                       'maxorphantxsize': 100,
                       'limitdescendantsize': 25000000000,
                       'limitdescendantcount': 25,
                       'limitancestorsize': 25000000000,
                       'limitancestorcount': 25,
                       'acceptnonstdoutputs': 1,
                       'datacarrier': 1,
                       'minrelaytxfee': Decimal('0.00000250'),
                       'dustrelayfee': Decimal('0.00000250'),
                       'blockmintxfee': Decimal('0.00000500'),
                       'maxstdtxvalidationduration': 10,
                       'maxnonstdtxvalidationduration': 1000,
                       'minconsolidationfactor': 20,
                       'maxconsolidationinputscriptsize': 150,
                       'minconfconsolidationinput': 10,
                       #'minconsolidationinputmaturity': 6, not setting this switch because we cannot
                       # use both -minconfconsolidationinput and -minconsolidationinputmaturity (deprecated) at the same time
                       'acceptnonstdconsolidationinput': 0}

        parameters2 = {'excessiveblocksize': 92233720,
                       'blockmaxsize': 92233720,
                       'maxtxsizepolicy': 100000,
                       'datacarriersize': 42949,
                       'maxscriptsizepolicy': 1000,
                       'maxopsperscriptpolicy': 200000,
                       'maxscriptnumlengthpolicy': 25000,
                       'maxpubkeyspermultisigpolicy': 42949,
                       'maxtxsigopscountspolicy': 42949,
                       'maxstackmemoryusagepolicy': 100000,
                       'maxorphantxsize': 1000,
                       'limitdescendantsize': 25000000,
                       'limitdescendantcount': 40,
                       'limitancestorsize': 30000000000,
                       'limitancestorcount': 40,
                       'acceptnonstdoutputs': 0,
                       'datacarrier': 0,
                       'minrelaytxfee': Decimal('0.00000150'),
                       'dustrelayfee': Decimal('0.00000150'),
                       'blockmintxfee': Decimal('0.00000250'),
                       'maxstdtxvalidationduration': 100,
                       'maxnonstdtxvalidationduration': 2000,
                       'minconsolidationfactor': 25,
                       'maxconsolidationinputscriptsize': 250,
                       #'minconfconsolidationinput': 4, for convenience we test deprecated switch -minconsolidationinputmaturity
                       # by removing -minconfconsolidationinput this time.
                       'minconsolidationinputmaturity': 11,
                       'acceptnonstdconsolidationinput': 1}

        # test rpc method getsettings with different command line parameters
        self.test_getsettings(parameters1)
        self.test_getsettings(parameters2)


if __name__ == '__main__':
    BSVNodeSettings().main()
