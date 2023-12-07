#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""Base class for RPC testing."""

from collections import deque
from enum import Enum
import logging
import optparse
import os
import pdb
import shutil
import sys
import tempfile
import time
import traceback
import contextlib
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.mininode import NetworkThread, StopNetworkThread
from .associations import Association, AssociationCB

from .authproxy import JSONRPCException
from . import coverage
from .test_node import TestNode, TestNode_process_list, BITCOIND_PROC_WAIT_TIMEOUT
from .util import (
    MAX_NODES,
    PortSeed,
    assert_equal,
    check_json_precision,
    connect_nodes_bi,
    connect_nodes,
    disconnect_nodes_bi,
    disconnect_nodes,
    initialize_datadir,
    log_filename,
    p2p_port,
    set_node_times,
    sync_blocks,
    sync_mempools,
    wait_until
)

from test_framework.blocktools import *


class TestStatus(Enum):
    PASSED = 1
    FAILED = 2
    SKIPPED = 3


TEST_EXIT_PASSED = 0
TEST_EXIT_FAILED = 1
TEST_EXIT_SKIPPED = 77


class BitcoinTestFramework():
    """Base class for a bitcoin test script.

    Individual bitcoin test scripts should subclass this class and override the set_test_params() and run_test() methods.

    Individual tests can also override the following methods to customize the test setup:

    - add_options()
    - setup_chain()
    - setup_network()
    - setup_nodes()

    The __init__() and main() methods should not be overridden.

    This class also contains various public and private helper methods."""

    def __init__(self):
        """Sets test framework defaults. Do not override this method. Instead, override the set_test_params() method"""
        self.setup_clean_chain = False
        self.nodes = []
        self.mocktime = 0
        self.runNodesWithRequiredParams = True
        self.bitcoind_proc_wait_timeout = BITCOIND_PROC_WAIT_TIMEOUT
        self.set_test_params()

        assert hasattr(
            self, "num_nodes"), "Test must set self.num_nodes in set_test_params()"

    def main(self):
        """Main function. This should not be overridden by the subclass test scripts."""

        parser = optparse.OptionParser(usage="%prog [options]")
        parser.add_option("--nocleanup", dest="nocleanup", default=False, action="store_true",
                          help="Leave bitcoinds and test.* datadir on exit or error")
        parser.add_option("--noshutdown", dest="noshutdown", default=False, action="store_true",
                          help="Don't stop bitcoinds after the test execution")
        parser.add_option("--srcdir", dest="srcdir", default=os.path.normpath(os.path.dirname(os.path.realpath(__file__)) + "/../../../src"),
                          help="Source directory containing bitcoind/bitcoin-cli (default: %default)")
        parser.add_option("--cachedir", dest="cachedir", default=os.path.normpath(os.path.dirname(os.path.realpath(__file__)) + "/../../cache"),
                          help="Directory for caching pregenerated datadirs")
        parser.add_option("--tmpdir", dest="tmpdir",
                          help="Root directory for datadirs")
        parser.add_option("-l", "--loglevel", dest="loglevel", default="INFO",
                          help="log events at this level and higher to the console. Can be set to DEBUG, INFO, WARNING, ERROR or CRITICAL. Passing --loglevel DEBUG will output all logs to console. Note that logs at all levels are always written to the test_framework.log file in the temporary test directory.")
        parser.add_option("--tracerpc", dest="trace_rpc", default=False, action="store_true",
                          help="Print out all RPC calls as they are made")
        parser.add_option("--portseed", dest="port_seed", default=os.getpid(), type='int',
                          help="The seed to use for assigning port numbers (default: current process id)")
        parser.add_option("--coveragedir", dest="coveragedir",
                          help="Write tested RPC commands into this directory")
        parser.add_option("--configfile", dest="configfile",
                          help="Location of the test framework config file")
        parser.add_option("--pdbonfailure", dest="pdbonfailure", default=False, action="store_true",
                          help="Attach a python debugger if test fails")
        parser.add_option("--waitforpid", dest="waitforpid", default=False, action="store_true",
                          help="Display the bitcoind pid and wait for the user before proceeding. Useful for (eg) attaching a debugger to bitcoind.")
        parser.add_option("--timeoutfactor", dest="timeoutfactor", default=1, type='float',
                          help="Multiply timeouts in specific tests with this factor to enable successful tests in slower environments.")
        self.add_options(parser)
        (self.options, self.args) = parser.parse_args()

        PortSeed.n = self.options.port_seed

        os.environ['PATH'] = self.options.srcdir + os.pathsep + os.environ['PATH']

        check_json_precision()

        self.options.cachedir = os.path.abspath(self.options.cachedir)

        # Set up temp directory and start logging
        if self.options.tmpdir:
            self.options.tmpdir = os.path.abspath(self.options.tmpdir)
            os.makedirs(self.options.tmpdir, exist_ok=False)
        else:
            self.options.tmpdir = tempfile.mkdtemp(prefix="test")
        self._start_logging()

        success = TestStatus.FAILED

        try:
            self.setup_chain()
            self.setup_network()
            self.run_test()
            success = TestStatus.PASSED
        except JSONRPCException as e:
            self.log.exception("JSONRPC error")
        except SkipTest as e:
            self.log.warning("Test Skipped: %s" % e.message)
            success = TestStatus.SKIPPED
        except AssertionError as e:
            self.log.exception("Assertion failed")
        except KeyError as e:
            self.log.exception("Key error")
        except Exception as e:
            self.log.exception("Unexpected exception caught during testing")
        except KeyboardInterrupt as e:
            self.log.warning("Exiting after keyboard interrupt")

        if success == TestStatus.FAILED and self.options.pdbonfailure:
            print("Testcase failed. Attaching python debugger. Enter ? for help")
            pdb.set_trace()

        # Make the NetworkThread stop if it is still running so that it does not prevent test script from exiting.
        StopNetworkThread()

        if not self.options.noshutdown:
            self.log.info("Stopping nodes")
            if self.nodes:
                self.stop_nodes()
        else:
            self.log.info(
                "Note: bitcoinds were not stopped and may still be running")
            # Remove all node processes from list of running external processes
            # so that they will not be killed when Python exists.
            TestNode_process_list.clear()

        if len(TestNode_process_list)>0:
            self.log.warning("%i process(es) started by test are still running and will be killed." % len(TestNode_process_list))
            if success != TestStatus.FAILED:
                self.log.error("Because not all started processes were properly stopped, test is considered to have failed!")
                success = TestStatus.FAILED

        if not self.options.nocleanup and not self.options.noshutdown and success != TestStatus.FAILED:
            self.log.info("Cleaning up {} on exit".format(self.options.tmpdir))
            cleanup_tree_on_exit = True
        else:
            self.log.warning("Not cleaning up dir %s" % self.options.tmpdir)
            cleanup_tree_on_exit = False
            if os.getenv("PYTHON_DEBUG", ""):
                # Dump the end of the debug logs, to aid in debugging rare
                # travis failures.
                import glob
                filenames = [os.path.join(self.options.tmpdir, "test_framework.log")]
                filenames += glob.glob(os.path.join(self.options.tmpdir,
                                       "node*", "regtest", "bitcoind.log"))
                MAX_LINES_TO_PRINT = 1000
                for fn in filenames:
                    try:
                        with open(fn, 'r') as f:
                            print("From", fn, ":")
                            print("".join(deque(f, MAX_LINES_TO_PRINT)))
                    except OSError:
                        print("Opening file %s failed." % fn)
                        traceback.print_exc()

        if success == TestStatus.PASSED:
            self.log.info("Tests successful")
            exit_code = TEST_EXIT_PASSED
        elif success == TestStatus.SKIPPED:
            self.log.info("Test skipped")
            exit_code = TEST_EXIT_SKIPPED
        else:
            self.log.error("Test failed. Test logging available at %s/test_framework.log", self.options.tmpdir)
            self.log.error("Hint: Call {} '{}' to consolidate all logs".format(os.path.normpath(os.path.dirname(os.path.realpath(__file__)) + "/../combine_logs.py"), self.options.tmpdir))
            exit_code = TEST_EXIT_FAILED
        logging.shutdown()
        if cleanup_tree_on_exit:
            shutil.rmtree(self.options.tmpdir)
        sys.exit(exit_code)

    # Methods to override in subclass test scripts.
    def set_test_params(self):
        """Tests must this method to change default values for number of nodes, topology, etc"""
        raise NotImplementedError

    def add_options(self, parser):
        """Override this method to add command-line options to the test"""
        pass

    def setup_chain(self):
        """Override this method to customize blockchain setup"""
        self.log.info("Initializing test directory " + self.options.tmpdir)
        if self.setup_clean_chain:
            self._initialize_chain_clean()
        else:
            self._initialize_chain()

    def setup_network(self):
        """Override this method to customize test network topology"""
        self.setup_nodes()

        # Connect the nodes as a "chain".  This allows us
        # to split the network between nodes 1 and 2 to get
        # two halves that can work on competing chains.
        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_all()

    def setup_nodes(self):
        """Override this method to customize test node setup"""
        extra_args = None
        if hasattr(self, "extra_args"):
            extra_args = self.extra_args
        self.add_nodes(self.num_nodes, extra_args)
        self.start_nodes()

    def run_test(self):
        """Tests must override this method to define test logic"""
        raise NotImplementedError

    # Public helper methods. These can be accessed by the subclass test scripts.

    def add_nodes(self, num_nodes, extra_args=None, rpchost=None, timewait=None, binaries=None):
        """Instantiate TestNode objects"""

        if extra_args is None:
            extra_args = [[]] * num_nodes
        if binaries is None:
            binaries = [None] * num_nodes
        assert_equal(len(extra_args), num_nodes)
        assert_equal(len(binaries), num_nodes)
        for i in range(num_nodes):
            self.add_node(i, extra_args[i], rpchost, timewait, binaries[i])

    def add_node(self, i, extra_args, rpchost=None, timewait=None, binary=None, init_data_dir=False):
        self.nodes.append(TestNode(i, self.options.tmpdir, extra_args, rpchost, timewait, binary, stderr=None,
                                   mocktime=self.mocktime, coverage_dir=self.options.coveragedir))
        if init_data_dir:
            initialize_datadir(self.options.tmpdir, i)

    def start_node(self, i, extra_args=None, stderr=None):
        """Start a bitcoind"""

        node = self.nodes[i]

        node.start(self.runNodesWithRequiredParams, extra_args, stderr)
        node.wait_for_rpc_connection()
        wait_until(lambda: node.rpc.getinfo()["initcomplete"])

        if self.options.coveragedir is not None:
            coverage.write_all_rpc_commands(self.options.coveragedir, node.rpc)

    def start_nodes(self, extra_args=None):
        """Start multiple bitcoinds"""

        if extra_args is None:
            extra_args = [None] * self.num_nodes
        assert_equal(len(extra_args), self.num_nodes)
        try:
            for i, node in enumerate(self.nodes):
                node.start(self.runNodesWithRequiredParams, extra_args[i])
            for i, node in enumerate(self.nodes):
                node.wait_for_rpc_connection()
                wait_until(lambda: node.rpc.getinfo()["initcomplete"])
                if(self.options.waitforpid):
                    print('Node {} started, pid is {}'.format(i, node.process.pid))
                    print('Do what you need (eg; gdb ./bitcoind {}) and then press <return> to continue...'.format(node.process.pid))
                    input()
        except:
            # If one node failed to start, stop the others
            self.stop_nodes()
            raise

        if self.options.coveragedir is not None:
            for node in self.nodes:
                coverage.write_all_rpc_commands(
                    self.options.coveragedir, node.rpc)

    # This method runs and stops bitcoind node with index 'node_index'.
    # It also creates (and handles closing of) 'number_of_connections' connections to bitcoind node with index 'node_index'.
    @contextlib.contextmanager
    def run_node_with_connections(self, title, node_index, args, number_of_connections, connArgs=[{}], cb_class=NodeConnCB, ip='127.0.0.1', wait_for_verack=True):
        logger.debug("setup %s", title)

        self.start_node(node_index, args)

        connections = []
        connectionCbs = []
        for i in range(number_of_connections):
            connCb = cb_class()
            connectionCbs.append(connCb)

            thisConnArgs = {}
            if len(connArgs) > i:
                thisConnArgs = connArgs[i]

            connection = NodeConn(ip, p2p_port(node_index), self.nodes[node_index], connCb, **thisConnArgs)
            connections.append(connection)
            connCb.add_connection(connection)

        thr = NetworkThread()
        thr.start()
        if wait_for_verack:
            for connCb in connectionCbs:
                connCb.wait_for_verack()

        logger.debug("before %s", title)
        yield tuple(connections)
        logger.debug("after %s", title)

        for connection in connections:
            connection.close()
        del connections
        # once all connection.close() are complete, NetworkThread run loop completes and thr.join() returns success
        thr.join()
        self.stop_node(node_index)
        logger.debug("finished %s", title)

    # this method creates following network graph
    #
    #      NodeConnCB
    #          |
    #          v
    #      self.node[0] ---> self.node[1]  ---> ... ---> self.node[n]
    #
    @contextlib.contextmanager
    def run_all_nodes_connected(self, title=None, args=None, ip='127.0.0.1', strSubVer=None, wait_for_verack=True, p2pConnections=[0], cb_class=NodeConnCB):
        if not title:
            title = "None"
        logger.debug("setup %s", title)

        if not args:
            args = [[]] * self.num_nodes
        else:
            assert(len(args) == self.num_nodes)

        self.start_nodes(args)

        connections = []
        connCb = None
        if p2pConnections:
            connCb = cb_class()  # one mininode connection  to node 0
        for i in p2pConnections:
            connection = NodeConn(ip, p2p_port(i), self.nodes[i], connCb, strSubVer=strSubVer)
            connections.append(connection)
            connCb.add_connection(connection)

        thr = NetworkThread()
        thr.start()
        if wait_for_verack and connCb:
            connCb.wait_for_verack()

        logger.debug("before %s", title)

        for i in range(self.num_nodes - 1):
            connect_nodes(self.nodes, i, i + 1)

        yield tuple(connections)
        logger.debug("after %s", title)

        for i in range(self.num_nodes - 1):
            disconnect_nodes(self.nodes[i], i + 1)

        for connection in connections:
            connection.close()
        del connections
        # once all connection.close() are complete, NetworkThread run loop completes and thr.join() returns success
        thr.join()
        self.stop_nodes()
        logger.debug("finished %s", title)

    # This method runs and stops bitcoind node with index 'node_index'.
    # It also creates (and handles closing of) some number of associations (desribed by their stream policies)
    # to bitcoind node with index 'node_index'.
    @contextlib.contextmanager
    def run_node_with_associations(self, title, node_index, args, stream_policies, cb_class=AssociationCB, ip='127.0.0.1', connArgs={}):
        logger.debug("setup %s", title)

        self.start_node(node_index, args)

        # Create associations and their connections to the node
        associations = []
        for stream_policy in stream_policies:
            association = Association(stream_policy, cb_class)
            association.create_streams(self.nodes[node_index], ip, connArgs)
            associations.append(association)

        # Start network handling thread
        thr = NetworkThread()
        thr.start()

        # Allow associations to exchange their setup messages and fully initialise
        for association in associations:
            association.setup()
        wait_until(lambda: len(self.nodes[node_index].getpeerinfo()) == len(associations))

        # Test can now proceed
        logger.debug("before %s", title)
        yield tuple(associations)
        logger.debug("after %s", title)

        # Shutdown associations and their connections
        for association in associations:
            association.close_streams()
        del associations

        # Once all connections are closed, NetworkThread run loop completes and thr.join() returns success
        thr.join()
        self.stop_node(node_index)
        logger.debug("finished %s", title)

    def stop_node(self, i):
        """Stop a bitcoind test node"""
        self.nodes[i].stop_node()
        self.nodes[i].wait_until_stopped(timeout=self.bitcoind_proc_wait_timeout)

    def stop_nodes(self):
        """Stop multiple bitcoind test nodes"""
        for node in self.nodes:
            # Issue RPC to stop nodes
            node.stop_node()

        for node in self.nodes:
            # Wait for nodes to stop
            node.wait_until_stopped(timeout=self.bitcoind_proc_wait_timeout)

    def restart_node(self, i, extra_args=None):
        """Stop and start a test node"""
        self.stop_node(i)
        self.start_node(i, extra_args)

    def assert_start_raises_init_error(self, i, extra_args=None, expected_msg=None):
        with tempfile.SpooledTemporaryFile(max_size=2**16) as log_stderr:
            try:
                self.start_node(i, extra_args, stderr=log_stderr)
                self.stop_node(i)
            except Exception as e:
                self.wait_for_node_exit(i,1) # wait until process properly terminates and resources are cleaned up
                assert 'bitcoind exited' in str(e)  # node must have shutdown
                if expected_msg is not None:
                    log_stderr.seek(0)
                    stderr = log_stderr.read().decode('utf-8')
                    if expected_msg not in stderr:
                        raise AssertionError(
                            "Expected error \"" + expected_msg + "\" not found in:\n" + stderr)
            else:
                if expected_msg is None:
                    assert_msg = "bitcoind should have exited with an error"
                else:
                    assert_msg = "bitcoind should have exited with expected error " + expected_msg
                raise AssertionError(assert_msg)

    def wait_for_node_exit(self, i, timeout):
        self.nodes[i].wait_for_exit(timeout)

    def split_network(self):
        """
        Split the network of four nodes into nodes 0/1 and 2/3.
        """
        disconnect_nodes_bi(self.nodes, 1, 2)
        self.sync_all([self.nodes[:2], self.nodes[2:]])

    def join_network(self):
        """
        Join the (previously split) network halves together.
        """
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_all()

    def sync_all(self, node_groups=None, timeout=60):
        if not node_groups:
            node_groups = [self.nodes]

        for group in node_groups:
            sync_blocks(group, timeout=timeout)
            sync_mempools(group, timeout=timeout)

    def enable_mocktime(self, mocktime):
        """Enable mocktime for the script.

        mocktime may be needed for scripts that use the cached version of the
        blockchain.  If the cached version of the blockchain is used without
        mocktime then the mempools will not sync due to IBD.
        """
        if self.mocktime == 0:
            self.mocktime = mocktime
        else:
            self.log.warning("mocktime overriden by test.")

    def disable_mocktime(self):
        self.mocktime = 0

    # Private helper methods. These should not be accessed by the subclass test scripts.

    def _start_logging(self):
        # Add logger and logging handlers
        self.log = logging.getLogger('TestFramework')
        self.log.setLevel(logging.DEBUG)
        # Create file handler to log all messages
        fh = logging.FileHandler(self.options.tmpdir + '/test_framework.log')
        fh.setLevel(logging.DEBUG)
        # Create console handler to log messages to stderr. By default this
        # logs only error messages, but can be configured with --loglevel.
        ch = logging.StreamHandler(sys.stdout)
        # User can provide log level as a number or string (eg DEBUG). loglevel
        # was caught as a string, so try to convert it to an int
        ll = int(self.options.loglevel) if self.options.loglevel.isdigit(
        ) else self.options.loglevel.upper()
        ch.setLevel(ll)
        # Format logs the same as bitcoind's bitcoind.log with microprecision (so log files can be concatenated and sorted)
        formatter = logging.Formatter(
            fmt='%(asctime)s.%(msecs)03d000 %(name)s (%(levelname)s): %(message)s', datefmt='%Y-%m-%d %H:%M:%S')
        formatter.converter = time.gmtime
        fh.setFormatter(formatter)
        ch.setFormatter(formatter)
        # add the handlers to the logger
        self.log.addHandler(fh)
        self.log.addHandler(ch)

        if self.options.trace_rpc:
            rpc_logger = logging.getLogger("BitcoinRPC")
            rpc_logger.setLevel(logging.DEBUG)
            rpc_handler = logging.StreamHandler(sys.stdout)
            rpc_handler.setLevel(logging.DEBUG)
            rpc_logger.addHandler(rpc_handler)

    def _initialize_chain(self):
        """Initialize a pre-mined blockchain for use by the test.

        Create a cache of a 200-block-long chain (with wallet) for MAX_NODES
        Afterward, create num_nodes copies from the cache."""

        assert self.num_nodes <= MAX_NODES
        create_cache = False
        for i in range(MAX_NODES):
            if not os.path.isdir(os.path.join(self.options.cachedir, 'node' + str(i))):
                create_cache = True
                break

        if create_cache:
            self.log.debug("Creating data directories from cached datadir")

            # find and delete old cache directories if any exist
            for i in range(MAX_NODES):
                if os.path.isdir(os.path.join(self.options.cachedir, "node" + str(i))):
                    shutil.rmtree(os.path.join(
                        self.options.cachedir, "node" + str(i)))

            # Create cache directories, run bitcoinds:
            for i in range(MAX_NODES):
                datadir = initialize_datadir(self.options.cachedir, i)
                args = [os.getenv("BITCOIND", "bitcoind"), "-server",
                        "-keypool=1", "-datadir=" + datadir, "-discover=0"]
                if i > 0:
                    args.append("-connect=127.0.0.1:" + str(p2p_port(0)))
                self.nodes.append(TestNode(i, self.options.cachedir, extra_args=[
                ], rpchost=None, timewait=None, binary=None, stderr=None, mocktime=self.mocktime, coverage_dir=None))
                self.nodes[i].args = args
                self.start_node(i)

            # Wait for RPC connections to be ready
            for node in self.nodes:
                node.wait_for_rpc_connection()

            # Create a 200-block-long chain; each of the 4 first nodes
            # gets 25 mature blocks and 25 immature.
            # Note: To preserve compatibility with older versions of
            # initialize_chain, only 4 nodes will generate coins.
            #
            # blocks are created with timestamps 10 minutes apart
            # starting from 2010 minutes in the past
            self.enable_mocktime(int(time.time()) - (201 * 10 * 60))
            for i in range(2):
                for peer in range(4):
                    for j in range(25):
                        set_node_times(self.nodes, self.mocktime)
                        self.nodes[peer].generate(1)
                        self.mocktime += 10 * 60
                    # Must sync before next peer starts generating blocks
                    sync_blocks(self.nodes)

            # Shut them down, and clean up cache directories:
            self.stop_nodes()
            self.nodes = []
            self.disable_mocktime()
            for i in range(MAX_NODES):
                os.remove(log_filename(self.options.cachedir, i, "bitcoind.log"))
                os.remove(log_filename(self.options.cachedir, i, "db.log"))
                os.remove(log_filename(self.options.cachedir, i, "peers.dat"))

        for i in range(self.num_nodes):
            from_dir = os.path.join(self.options.cachedir, "node" + str(i))
            to_dir = os.path.join(self.options.tmpdir, "node" + str(i))
            shutil.copytree(from_dir, to_dir)
            # Overwrite port/rpcport in bitcoin.conf
            initialize_datadir(self.options.tmpdir, i)

    def _initialize_chain_clean(self):
        """Initialize empty blockchain for use by the test.

        Create an empty blockchain and num_nodes wallets.
        Useful if a test case wants complete control over initialization."""
        for i in range(self.num_nodes):
            initialize_datadir(self.options.tmpdir, i)


class ComparisonTestFramework(BitcoinTestFramework):
    """Test framework for doing p2p comparison testing

    Sets up some bitcoind binaries:
    - 1 binary: test binary
    - 2 binaries: 1 test binary, 1 ref binary
    - n>2 binaries: 1 test binary, n-1 ref binaries"""

    def __init__(self, destAddress = '127.0.0.1'):
        super(ComparisonTestFramework,self).__init__()
        self.chain = ChainManager()
        self.destAddr = destAddress
        self._network_thread = None
        if not hasattr(self, "testbinary"):
            self.testbinary = [os.getenv("BITCOIND", "bitcoind")]
        if not hasattr(self, "refbinary"):
            self.refbinary = [os.getenv("BITCOIND", "bitcoind")]

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def add_options(self, parser):
        parser.add_option("--testbinary", dest="testbinary", help="bitcoind binary to test")
        parser.add_option("--refbinary", dest="refbinary", help="bitcoind binary to use for reference nodes (if any)")

    def setup_network(self):
        extra_args = [['-whitelist=127.0.0.1']] * self.num_nodes
        if hasattr(self, "extra_args"):
            extra_args = self.extra_args
        if self.options.testbinary:
            self.testbinary = [self.options.testbinary]
        if self.options.refbinary:
            self.refbinary = [self.options.refbinary]
        binaries = [self.testbinary] + [self.refbinary] * (self.num_nodes - 1)
        self.add_nodes(self.num_nodes, extra_args, binaries=binaries)
        self.start_nodes()
        self.init_network()

    def restart_network(self, timeout=None):
        self.test.clear_all_connections()
        # If we had a network thread from eariler, make sure it's finished before reconnecting
        if self._network_thread is not None:
            self._network_thread.join(timeout)
        # Reconnect
        self.test.add_all_connections(self.nodes)
        self._network_thread = NetworkThread()
        self._network_thread.start()

    def init_network(self):
        # Start creating test manager which help to manage test cases
        self.test = TestManager(self, self.options.tmpdir)
        self.test.destAddr = self.destAddr
        # (Re)start network
        self.restart_network()

    # returns a test case that asserts that the current tip was accepted
    def accepted(self, sync_timeout=300):
        return TestInstance([[self.chain.tip, True]], sync_timeout=sync_timeout)

    # returns a test case that asserts that the current tip was rejected
    def rejected(self, reject=None):
        if reject is None:
            return TestInstance([[self.chain.tip, False]])
        else:
            return TestInstance([[self.chain.tip, reject]])

    def check_mempool(self, rpc, should_be_in_mempool, timeout=20):
        wait_until(lambda: {t.hash for t in should_be_in_mempool}.issubset(set(rpc.getrawmempool())), timeout=timeout)


class SkipTest(Exception):
    """This exception is raised to skip a test"""

    def __init__(self, message):
        self.message = message
