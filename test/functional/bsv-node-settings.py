#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test RPC method getsettings that returns expected values that are used when constructing/accepting a block
or a transaction.
"""

from decimal import Decimal
from itertools import chain
from collections import defaultdict
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, check_for_log_msg, wait_until


def scale_params(*params, scale):
    if isinstance(params, str):
        params = params.split()
    return ((param, scale) for param in params)


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
        # check we received expected fields (parameter settings)
        optional = set(['minconsolidationinputmaturity', 'minconfconsolidationinput'])
        expected_settings = set(parameters).union(optional)
        actual_settings = set(node_settings)
        unexpected_settings = actual_settings - expected_settings
        assert_equal(unexpected_settings, set())
        missing_settings = expected_settings - actual_settings
        assert_equal(missing_settings, set())

        # check that set policy parameters return expected values

        param_scales = defaultdict(lambda: 1, chain(
            scale_params("maxorphantxsize", "maxmempool", "maxmempoolsizedisk", scale=1000000)))
        for param in parameters:
            expected = (param, parameters[param] * param_scales[param])
            actual = (param, node_settings[param])
            assert_equal(expected, actual)

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
                       'maxstackmemoryusageconsensus': 9223372036854775807,
                       'maxorphantxsize': 100,
                       'limitancestorcount': 25,
                       'limitcpfpgroupmemberscount': 13,
                       'maxmempool': 432,
                       'mempoolmaxpercentcpfp': 42,
                       'maxmempoolsizedisk': 4321,
                       'acceptnonstdoutputs': 1,
                       'datacarrier': 1,
                       'minminingtxfee': Decimal('0.00000500'),
                       'maxstdtxvalidationduration': 10,
                       'maxnonstdtxvalidationduration': 1000,
                       'maxtxchainvalidationbudget': 0,
                       'validationclockcpu': 0,
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
                       'maxstackmemoryusageconsensus': 92233720,
                       'maxorphantxsize': 1000,
                       'limitancestorcount': 40,
                       'limitcpfpgroupmemberscount': 31,
                       'maxmempool': 423,
                       'mempoolmaxpercentcpfp': 24,
                       'maxmempoolsizedisk': 4312,
                       'acceptnonstdoutputs': 0,
                       'datacarrier': 0,
                       'minminingtxfee': Decimal('0.00000250'),
                       'maxstdtxvalidationduration': 3,
                       'maxnonstdtxvalidationduration': 90,
                       'maxtxchainvalidationbudget': 50,
                       'validationclockcpu': 1,
                       'minconsolidationfactor': 25,
                       'maxconsolidationinputscriptsize': 250,
                       #'minconfconsolidationinput': 4, for convenience we test deprecated switch -minconsolidationinputmaturity
                       # by removing -minconfconsolidationinput this time.
                       'minconsolidationinputmaturity': 11,
                       'acceptnonstdconsolidationinput': 1}

        # test rpc method getsettings with different command line parameters
        self.test_getsettings(parameters1)
        self.test_getsettings(parameters2)

        # verify the warning messages of -minrelayfee, -dustrelayfee and -dustlimitfactor are deprecated on log file
        self.restart_node(0, extra_args=['-minrelaytxfee=0', '-dustrelayfee=0', '-dustlimitfactor=0','-blockmintxfee=0.000005'])
        wait_until(lambda: check_for_log_msg(self, "-minrelaytxfee", "/node0"))
        wait_until(lambda: check_for_log_msg(self, "-dustrelayfee", "/node0"))
        wait_until(lambda: check_for_log_msg(self, "-dustlimitfactor", "/node0"))
        wait_until(lambda: check_for_log_msg(self, "-blockmintxfee", "/node0"))


if __name__ == '__main__':
    BSVNodeSettings().main()
