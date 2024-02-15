#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""Run regression test suite.

This module calls down into individual test cases via subprocess. It will
forward all unrecognized arguments onto the individual test scripts.

For a description of arguments recognized by test scripts, see
`test/functional/test_framework/test_framework.py:BitcoinTestFramework.main`.

"""

import argparse
import configparser
import datetime
import os
import time
import shutil
import signal
import sys
import subprocess
import tempfile
import re
import logging
import xml.etree.ElementTree as ET
import json

# Formatting. Default colors to empty strings.
BOLD, BLUE, RED, GREY = ("", ""), ("", ""), ("", ""), ("", "")

TICK = "P "
CROSS = "x "
CIRCLE = "o "

if os.name == 'posix':
    # primitive formatting on supported
    # terminal via ANSI escape sequences:
    BOLD = ('\033[0m', '\033[1m')
    BLUE = ('\033[0m', '\033[0;34m')
    RED = ('\033[0m', '\033[0;31m')
    GREY = ('\033[0m', '\033[1;30m')

TEST_EXIT_PASSED = 0
TEST_EXIT_SKIPPED = 77

NON_SCRIPTS = [
    # These are python files that live in the functional tests directory, but are not test scripts.
    "combine_logs.py",
    "create_cache.py",
    "test_runner.py",
    "bsv_pbv_common.py"
]

LARGE_BLOCK_TESTS = [
    # Tests for block files larger than 4GB.
    # This tests take really long time to execute or require a great deal of memory so they
    # are excluded by default.
    # Use --large-block-tests command line parameter to run them.
    "bsv-genesis-large-blockfile-io.py",
    "bsv-genesis-large-blockfile-reindex.py",
    "bsv-genesis-large-blockfile-max-32-bit.py",
    "bsv-large-blocks-txindex-data.py",
    "bsv-4gb-plus-block.py"
]

# This is a list of tests that should not run in parallel.
# These tests are executed one by one when all others have finished
# Such tests may fall in one of the following categories:
# - Test is resource intensive and utilizes many CPU cores and use a lot o RAM
#   (these tests may cause others to fail)
# - Test relies on time measurements and/or compares times of multiple operations
# - A test is vulnerable to CPU/RAM availability fluctuations

SOLO_TESTS = {
    "bsv-rpc-verifyscript.py",
    "wallet-encryption.py",
    "bsv-ptv-txn-chains.py",
    "mining_api.py",
    "bsv-pbv-withsigops.py",
    "bsv-broadcast_delay.py",
    "bsv-dsreport.py",
    "bsv-callback-service.py",
    "bsv-dsattack-with-mocked-dsdetector.py",
    "bsv-p2p-dsdetected.py",
    "bsv-safe-mode.py",
    "bsv-safe-mode-reorg-notification.py"
}

ENVIRONMENT_TYPE = {
    1 : "Release build",
    2 : "Release build with sanitizers enabled",
    3 : "Debug build",
    4 : "Debug build with sanitizers enabled"
}

# collection of timeout factors for time-sensitive tests:
# test_name : factor_release_build, factor_debug_build, factor_release_with_sanitizers, factor_debug_with_sanitizers
# factor for release build is always 1; it is still present in this map for consistency
TIMEOUT_FACTOR_FOR_TESTS = {
    "bsv-block-propagation-priority.py" : [1,2,2,3],
    "bsv-consolidation-feefilter.py" : [1,4,4,5],
    "bsv-genesis-general.py" : [1,2,2,3],
    "bsv-mempool-eviction.py" : [1,1,3,5],
    "bsv-4gb-plus-block.py" : [1,2,2,3],
    "bsv-block-stalling-test.py" : [1,2,2,3]
}

# This tests can be only run by explicitly specifying them on command line.
# This is usefull for tests that take really long time to execute.
EXCLUDED_TESTS = ["libevent_crashtest_on_many_rpc.py"]

TEST_PARAMS = {
    # Some test can be run with additional parameters.
    # When a test is listed here, then it will be run without parameters
    # as well as with additional parameters listed here.
    # This:
    #    example "testName" : [["--param1", "--param2"] , ["--param3"]]
    # will run the test 3 times:
    #    testName
    #    testName --param1 --param2
    #    testname --param3
    "txn_doublespend.py": [["--mineblock"]],
    "txn_clone.py": [["--mineblock"]],
    # Test with blocks larger than preferredBlockfileSize.
    "bsv-128Mb-blocks.py": [["--excessiveblocksize=130000000"]],

    # Run  automatic block size validation size with default activation time as well as overriden time
    "bsv-block-size-activation-generated-default.py": [["--blocksizeactivationtime={}".format(int(time.time()) + 24 * 60 * 60)]],
    "bsv-block-size-activation-default.py": [["--blocksizeactivationtime={}".format(int(time.time()) + 24 * 60 * 60)]]
}

TESTS_WITH_DISABLED_STDERROR_CHECK = ["bsv-callback-service.py", "bsv-dsreport.py", "bsv-ds-bad-callback-service.py"]

# Used to limit the number of tests, when list of tests is not provided on command line
# When --extended is specified, we run all tests, otherwise
# we only run a test if its execution time in seconds does not exceed EXTENDED_CUTOFF
EXTENDED_CUTOFF = 40

running_jobs = []


def on_ci():
    return os.getenv('TRAVIS') == 'true' or os.getenv('TEAMCITY_VERSION') != None


def main():
    # Read config generated by configure.
    config = configparser.ConfigParser()
    configfile = os.path.join(os.path.abspath(
        os.path.dirname(__file__)), "..", "config.ini")
    config.read_file(open(configfile))

    src_dir = config["environment"]["SRCDIR"]
    build_dir = config["environment"]["BUILDDIR"]
    tests_dir = os.path.join(src_dir, 'test', 'functional')

    # Parse arguments and pass through unrecognised args
    parser = argparse.ArgumentParser(add_help=False,
                                     usage='%(prog)s [test_runner.py options] [script options] [scripts]',
                                     description=__doc__,
                                     epilog='''
    Help text and arguments for individual test script:''',
                                     formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument('--coverage', action='store_true',
                        help='generate a basic coverage report for the RPC interface')
    parser.add_argument('--exclude', '-x',
                        help='specify a comma-seperated-list of scripts to exclude. Do not include the .py extension in the name.')
    parser.add_argument('--run-solo',
                        help='specify a comma-seperated-list of scripts to execute non-parallel. Do not include the .py extension in the name.')
    parser.add_argument('--extended', action='store_true',
                        help='run the extended test suite in addition to the basic tests')
    parser.add_argument('--list-tests', action='store_true',
                        help='just show the tests that would be run')
    parser.add_argument('--help', '-h', '-?',
                        action='store_true', help='print help text and exit')
    parser.add_argument('--jobs', '-j', type=int, default=4,
                        help='how many test scripts to run in parallel. Default=4.')
    parser.add_argument('--keepcache', '-k', action='store_true',
                        help='the default behavior is to flush the cache directory on startup. --keepcache retains the cache from the previous testrun.')
    parser.add_argument('--quiet', '-q', action='store_true',
                        help='only print results summary and failure logs')
    parser.add_argument('--failfast', '-f', action='store_true',
                        help='Exit on first failing test')
    parser.add_argument('--tmpdirprefix', '-t',
                        default=tempfile.gettempdir(), help="Root directory for datadirs")
    parser.add_argument('--junitouput', '-ju',
                        default=os.path.join(build_dir, 'junit_results.xml'), help="file that will store JUnit formated test results.")
    parser.add_argument('--buildconfig', '-b',
                        default="", help="Optional name of directory that contains binary and is located inside build directory. Used on Windows where "
                        "the build directory can contain outputs for multiple configurations. Example: -b RelWithDebInfo.")
    parser.add_argument('--watch', type=str,
                        default=None, help="Showing specified file in the console and monitoring its changes, usefull "
                                           "for live viewing of the log files. If it is of the form 'nodeX' where X is integer, "
                                           "it will show bitcoind.log file of the specified node")
    parser.add_argument('--large-block-tests', action='store_true', help="Runs large block file tests.")
    parser.add_argument('--output-type', type=int, default=2, help="Output type: 2 - Automatic detection. 0 - Primitive output suited for CI. 1 - Advanced suited for console.")
    parser.add_argument('--timeout-factors', type=str, default="1", help="Purpose of this flag is to adjust timeouts in specific tests as needed by test environment (e.g. debug builds need longer timeouts)."
                                                                         " There are two possible inputs for this argument: You can choose environment type and default timeout factors will be set:"
                                                                         " 1: Release build. (default) 2: Debug build. 3: Release build with sanitizers. 4: Debug build with sanitizers.\n"
                                                                         "If these factors do not work for you, you can pass them directly (in JSON): "
                                                                         " Example: --timeout-factors={{\"bsv-genesis-general.py\" : 2, \"bsv-mempool-eviction.py\" : 3 , ...}}."
                                                                         " You must pass timeout factors for all the following tests (if you are running them): {}."
                                                                         .format([test for test in TIMEOUT_FACTOR_FOR_TESTS.keys()]))
    args, unknown_args = parser.parse_known_args()

    # Output type. Default is 2: automatic detection
    if args.output_type == 2:
        if os.name == 'nt':
            console = False
        else:
            console = sys.stdout.isatty()
    else:
        console = args.output_type==1

    # Create a set to store arguments and create the passon string
    tests = set(arg for arg in unknown_args if arg[:2] != "--")
    passon_args = [arg for arg in unknown_args if arg[:2] == "--"]
    passon_args.append("--configfile=%s" % configfile)

    # Set up logging
    logging_level = logging.INFO if args.quiet else logging.DEBUG
    logging.basicConfig(format='%(message)s', level=logging_level)

    # Create base test directory
    tmpdir = os.path.join("%s", "bitcoin_test_runner_%s") % (
        args.tmpdirprefix, datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
    os.makedirs(tmpdir)

    logging.debug("Temporary test directory at %s" % tmpdir)

    enable_wallet = config["components"].getboolean("ENABLE_WALLET")
    enable_utils = config["components"].getboolean("ENABLE_UTILS")
    enable_bitcoind = config["components"].getboolean("ENABLE_BITCOIND")

    if not (enable_wallet and enable_utils and enable_bitcoind):
        print(
            "No functional tests to run. Wallet, utils, and bitcoind must all be enabled")
        print(
            "Rerun `configure` with -enable-wallet, -with-utils and -with-daemon and rerun make")
        sys.exit(0)

    # Parse '--run-solo'. Add specified tests to the SOLO_TESTS list
    if args.run_solo:
        for test_name in args.run_solo.split(','):
            test_name = test_name + ".py"
            if test_name not in SOLO_TESTS:
                SOLO_TESTS.add(test_name)
            else:
                print("Warning: %s already a part of SOLO_TESTS" % test_name)

    # Build list of tests
    all_scripts = get_all_scripts_from_disk(tests_dir, NON_SCRIPTS)

    # Check for large block tests parameter 
    if args.large_block_tests:
        tests = LARGE_BLOCK_TESTS
        args.jobs = 1

    if tests:
        # Individual tests have been specified. Run specified tests that exist
        # in the all_scripts list. Accept the name with or without .py
        # extension.
        test_list = [t for t in all_scripts if
                     (t in tests or re.sub(".py$", "", t) in tests)]
        cutoff = sys.maxsize  # do not cut off explicitly specified tests
    else:
        # No individual tests have been specified.
        # Run all tests that do not exceed
        # EXTENDED_CUTOFF, unless --extended was specified
        test_list = all_scripts
        cutoff = EXTENDED_CUTOFF
        if args.extended:
            cutoff = sys.maxsize
        # Exclude tests specified in EXCLUDED_TESTS.
        # These tests should be specified in command line to execute
        test_list = [test for test in test_list if test not in EXCLUDED_TESTS]
        # Exclude large block tests unless explicitly told to run them
        if not args.large_block_tests:
            test_list = [test for test in test_list if test not in LARGE_BLOCK_TESTS]

    # Remove the test cases that the user has explicitly asked to exclude.
    if args.exclude:
        for exclude_test in args.exclude.split(','):
            if exclude_test + ".py" in test_list:
                test_list.remove(exclude_test + ".py")

    # Use and update timings from build_dir only if separate
    # build directory is used. We do not want to pollute source directory.
    build_timings = None
    if (src_dir != build_dir):
        build_timings = Timings(os.path.join(build_dir, 'timing.json'))

    # Always use timings from scr_dir if present
    src_timings = Timings(os.path.join(
        src_dir, "test", "functional", 'timing.json'))

    # Add test parameters and remove long running tests if needed
    test_list, solo_position_start = get_tests_to_run(
        test_list, TEST_PARAMS, cutoff, src_timings, build_timings)

    if (args.timeout_factors.isdigit()):
        for timeout_test in TIMEOUT_FACTOR_FOR_TESTS.keys():
            if timeout_test in test_list:
                test_list[test_list.index(timeout_test)] = (timeout_test + " --timeoutfactor={}"
                    .format(TIMEOUT_FACTOR_FOR_TESTS[timeout_test][int(args.timeout_factors)-1])) # noqa

    else:
        try:
            timeout_factors_json = json.loads(args.timeout_factors)
            if (set(test_list).intersection(TIMEOUT_FACTOR_FOR_TESTS.keys()) - set(timeout_factors_json.keys()) != set()):
                print("Timeout factor tests should include timeout factor for tests: {}"
                    .format([test for test in set(test_list).intersection(TIMEOUT_FACTOR_FOR_TESTS.keys())])) # noqa
                sys.exit(0)
            for timeout_test in timeout_factors_json.keys():
                if timeout_test in test_list:
                    test_list[test_list.index(timeout_test)] = (timeout_test + " --timeoutfactor={}".format(timeout_factors_json[timeout_test]))
        except ValueError as e:
            print ("Error parsing input json: ", e)

    if not test_list:
        print("No valid test scripts specified. Check that your test is in one "
              "of the test lists in test_runner.py, or run test_runner.py with no arguments to run all tests")
        sys.exit(0)

    if args.help:
        # Print help for test_runner.py, then print help of the first script
        # and exit.
        parser.print_help()
        subprocess.check_call(
            [sys.executable, os.path.join(tests_dir, test_list[0]), '-h'])
        sys.exit(0)

    if args.list_tests:
        print("\n".join(test_list))
        sys.exit(0)

    if not args.keepcache:
        shutil.rmtree(os.path.join(build_dir, "test",
                                   "cache"), ignore_errors=True)

    run_tests(test_list, build_dir, tests_dir, args.junitouput, args.failfast,
              config["environment"]["EXEEXT"], tmpdir, args.jobs, args.coverage, passon_args, build_timings, args.buildconfig, args.watch, console, solo_position_start)


def run_tests(test_list, build_dir, tests_dir, junitouput, fail_fast, exeext, tmpdir, jobs=1, enable_coverage=False, args=[],  build_timings=None, buildconfig="", file_for_monitoring=None, console=False, solo_position_start=-1):
    # Warn if bitcoind is already running (unix only)
    try:
        pidofOutput = subprocess.check_output(["pidof", "bitcoind"])
        if pidofOutput is not None and pidofOutput != b'':
            print("%sWARNING!%s There is already a bitcoind process running on this system. Tests may fail unexpectedly due to resource contention!" % (
                BOLD[1], BOLD[0]))
    except (OSError, subprocess.SubprocessError):
        pass

    # Warn if there is a cache directory
    cache_dir = os.path.join(build_dir, "test", "cache")
    if os.path.isdir(cache_dir):
        print("%sWARNING!%s There is a cache directory here: %s. If tests fail unexpectedly, try deleting the cache directory." % (
            BOLD[1], BOLD[0], cache_dir))

    # Set env vars
    if "BITCOIND" not in os.environ:
        os.environ["BITCOIND"] = os.path.join(
            build_dir, 'src', buildconfig, 'bitcoind' + exeext)
        os.environ["BITCOINCLI"] = os.path.join(
            build_dir, 'src', buildconfig, 'bitcoin-cli' + exeext)

    if not os.path.isfile(os.environ["BITCOIND"]):
        print("%sERROR!%s Can not find bitcoind executable here: %s. " % (
            BOLD[1], BOLD[0], os.environ["BITCOIND"]))
        sys.exit(0)

    if not os.path.isfile(os.environ["BITCOINCLI"]):
        print("%sERROR!%s Can not find bitcoin-cli executable here: %s. " % (
            BOLD[1], BOLD[0], os.environ["BITCOINCLI"]))
        sys.exit(0)

    flags = [os.path.join("--srcdir={}".format(build_dir), "src")] + args
    flags.append("--cachedir=%s" % cache_dir)

    if enable_coverage:
        coverage = RPCCoverage()
        flags.append(coverage.flag)
        logging.debug("Initializing coverage directory at %s" % coverage.dir)
    else:
        coverage = None

    if len(test_list) > 1 and jobs > 1:
        # Populate cache
        try:
            subprocess.check_output([sys.executable, os.path.join(tests_dir, 'create_cache.py')] + flags + [os.path.join("--tmpdir=%s", "cache") % tmpdir])
        except subprocess.CalledProcessError as e:
            sys.stdout.buffer.write(e.output)
            raise

    # Run Tests
    job_queue = TestHandler(jobs, tests_dir, tmpdir, test_list, flags, file_for_monitoring, console, solo_position_start)
    time0 = time.time()
    test_results = []

    max_len_name = len(max(test_list, key=len))

    for _ in range(len(test_list)):
        test_result = job_queue.get_next()
        test_results.append(test_result)

        if test_result.status == "Passed":
            print_log("\n%s%s%s passed, Duration: %s s" % (
                BOLD[1], test_result.name, BOLD[0], test_result.time), console=console)
        elif test_result.status == "Skipped":
            print_log("\n%s%s%s skipped" %
                      (BOLD[1], test_result.name, BOLD[0]), console=console)
        else:
            print_log("\n%s%s%s failed, Duration: %s s\n" %
                      (BOLD[1], test_result.name, BOLD[0], test_result.time))
            print(BOLD[1] + 'stdout:\n' + BOLD[0] + test_result.stdout + '\n')
            print(BOLD[1] + 'stderr:\n' + BOLD[0] + test_result.stderr + '\n')

            if(fail_fast):
                break

    runtime = int(time.time() - time0)
    print_results(test_results, max_len_name, runtime)
    save_results_as_junit(test_results, junitouput, runtime)

    if (build_timings is not None):
        build_timings.save_timings(test_results)

    if coverage:
        coverage.report_rpc_coverage()

        logging.debug("Cleaning up coverage data")
        coverage.cleanup()

    # Clear up the temp directory if all subdirectories are gone
    if not os.listdir(tmpdir):
        os.rmdir(tmpdir)

    all_passed_or_skipped = all(
        map(lambda test_result: test_result.status == "Passed" or test_result.status == "Skipped", test_results))

    sys.exit(not all_passed_or_skipped)


def print_results(test_results, max_len_name, runtime):
    results = "\n" + BOLD[1] + "%s | %s | %s\n\n" % (
        "TEST".ljust(max_len_name), "STATUS   ", "DURATION") + BOLD[0]

    test_results.sort(key=lambda result: result.name.lower())
    all_passed = True
    time_sum = 0

    for test_result in test_results:
        all_passed = all_passed and test_result.status != "Failed"
        time_sum += test_result.time
        test_result.padding = max_len_name
        results += str(test_result)

    status = TICK + "Passed" if all_passed else CROSS + "Failed"
    results += BOLD[1] + "\n%s | %s | %s s (accumulated) \n" % (
        "ALL".ljust(max_len_name), status.ljust(9), time_sum) + BOLD[0]
    results += "Runtime: %s s\n" % (runtime)
    print(results)


class TestHandler:
    """
    Trigger the testscrips passed in via the list.
    """

    def __init__(self, num_tests_parallel, tests_dir, tmpdir, test_list=None, flags=None, file_for_monitoring=None, console=False, solo_position_start=-1):
        assert (num_tests_parallel >= 1)
        self.num_jobs = num_tests_parallel
        self.tests_dir = tests_dir
        self.tmpdir = tmpdir
        self.test_list = test_list
        self.flags = flags
        self.num_running = 0
        self.ts = time.time()
        self.console = console
        self.test_count = 0
        self.solo_position_start=solo_position_start
        # In case there is a graveyard of zombie bitcoinds, we can apply a
        # pseudorandom offset to hopefully jump over them.
        # (625 is PORT_RANGE/MAX_NODES)
        self.portseed_offset = int(time.time() * 1000) % 625
        if (file_for_monitoring is not None):
            if file_for_monitoring[:4] == "node" and file_for_monitoring[4:].isnumeric():
                file_for_monitoring = os.path.join(file_for_monitoring, "regtest", "bitcoind.log")
            if file_for_monitoring == "test":
                file_for_monitoring = "test_framework.log"
        self.file_for_monitoring = file_for_monitoring

    def get_next(self):
        log_job = None
        try:
            while self.num_running < self.num_jobs and self.test_list:

                # Run parallel tests first, then solo
                if self.solo_position_start >= 0 and self.test_count >= self.solo_position_start:
                    if self.num_running > 0:
                        # All tests that may still be running must finish before solo tests can be started
                        break
                    if self.num_jobs > 1:
                        # From this point on the tests are not run in parallel anymore
                        self.num_jobs = 1
                        print_log("*\n*** Finished running tests in parallel, now running solo tests ***\n*", console=self.console)
                self.test_count += 1

                # Add tests
                self.num_running += 1
                t = self.test_list.pop(0)
                print_log("  %s%s%s: started" % (BOLD[1], t, BOLD[0]), console=self.console)
                portseed = len(self.test_list) + self.portseed_offset
                portseed_arg = ["--portseed={}".format(portseed)]
                log_stdout = tempfile.SpooledTemporaryFile(max_size=2**16)
                log_stderr = tempfile.SpooledTemporaryFile(max_size=2**16)
                test_argv = t.split()
                tmpdir = [os.path.join("--tmpdir=%s", "%s_%s") %
                          (self.tmpdir, re.sub(".py.*$", "", t), portseed)]
                running_jobs.append((t,
                                    time.time(),
                                    subprocess.Popen([sys.executable,
                                                      os.path.join(self.tests_dir, test_argv[0])]
                                                     + test_argv[1:]
                                                     + self.flags
                                                     + portseed_arg
                                                     + tmpdir,
                                                     universal_newlines=True,
                                                     stdout=log_stdout,
                                                     stderr=log_stderr),
                                    log_stdout,
                                    log_stderr))

                if self.file_for_monitoring is not None:
                    logfile = os.path.join(self.tmpdir, f"{t[:-3]}_{portseed}", self.file_for_monitoring)
                    print("\nWatching file: ", logfile, "\n\n\n")
                    for _ in range(15):
                        if os.path.isfile(logfile):
                            if os.name == 'nt':
                                log_job = subprocess.Popen(["powershell", "Get-Content", logfile, "-Wait"],
                                                           universal_newlines=True)
                            elif os.name == 'posix':
                                log_job = subprocess.Popen(["tail", "-F", logfile],
                                                           universal_newlines=True)
                            break
                        time.sleep(1)

            if not running_jobs:
                raise IndexError('pop from empty list')
            while True:
                # Return first proc that finishes
                for j in running_jobs:
                    (name, time0, proc, log_out, log_err) = j
                    if on_ci() and int(time.time() - time0) > 40 * 60:
                        # In travis, timeout individual tests after 20 minutes (to stop tests hanging and not
                        # providing useful output.
                        proc.send_signal(signal.SIGINT)
                    if proc.poll() is not None:
                        if log_job:
                            log_job.terminate()
                            log_job = None
                        log_out.seek(0), log_err.seek(0)
                        [stdout, stderr] = [l.read().decode('utf-8')
                                            for l in (log_out, log_err)]
                        log_out.close(), log_err.close()
                        if proc.returncode == TEST_EXIT_PASSED and (stderr == "" or name in TESTS_WITH_DISABLED_STDERROR_CHECK):
                            status = "Passed"
                        elif proc.returncode == TEST_EXIT_SKIPPED:
                            status = "Skipped"
                        else:
                            status = "Failed"
                        self.num_running -= 1
                        running_jobs.remove(j)

                        return TestResult(name, status, int(time.time() - time0), stdout, stderr)

                # In a console mode (interactive shell) the output can be more  user friendly
                # and status of running tests will be constantly refreshed on the bottom.
                # When output is to stdout, such as on CI, a more simple output is required.
                # In this case we check every minute for jobs that are running for a long time and print them.
                # This helps identify the jobs that gave up on life.
                if not log_job:
                    if self.console:
                        # This will refresh job statuses
                        print_log(jobs=running_jobs, console=self.console)
                    else:
                        # Check jobs every minute
                        if time.time() - self.ts > 60:
                            self.ts = time.time()
                            check_jobs(running_jobs)
                    time.sleep(0.5)
        finally:
            if log_job:
                log_job.terminate()


class TestResult():
    def __init__(self, name, status, time, stdout, stderr):
        self.name = name
        self.status = status
        self.time = time
        self.padding = 0
        self.stdout = stdout
        self.stderr = stderr

    def __repr__(self):
        if self.status == "Passed":
            color = BLUE
            glyph = TICK
        elif self.status == "Failed":
            color = RED
            glyph = CROSS
        elif self.status == "Skipped":
            color = GREY
            glyph = CIRCLE

        return color[1] + "%s | %s%s | %s s\n" % (self.name.ljust(self.padding), glyph, self.status.ljust(7), self.time) + color[0]


def get_all_scripts_from_disk(test_dir, non_scripts):
    """
    Return all available test script from script directory (excluding NON_SCRIPTS)
    """
    python_files = set([t for t in os.listdir(test_dir) if t[-3:] == ".py"])
    return list(python_files - set(non_scripts))


def get_tests_to_run(test_list, test_params, cutoff, src_timings, build_timings=None):
    """
    Returns only test that will not run longer then cutoff (see --extended option).
    Long running tests are returned first to favor running tests in parallel
    Timings from build directory override those from src directory
    """

    def get_test_time(test):
        if build_timings is not None:
            timing = next(
                (x['time'] for x in build_timings.existing_timings if x['name'] == test), None)
            if timing is not None:
                return timing

        # try source directory. Return 0 if test is unknown to always run it
        return next(
            (x['time'] for x in src_timings.existing_timings if x['name'] == test), 0)

    # Some tests must also be run with additional parameters. Add them to the list.
    # Separate the list in two parts: parallel first, solo last
    tests_with_params = []
    solo_tests = []
    for test_name in test_list:
        # always execute a test without parameters
        if test_name in SOLO_TESTS:
            solo_tests.append(test_name)
        else:
            tests_with_params.append(test_name)

        # Add predefined parameters to tests
        params = test_params.get(test_name)
        if params is not None:
            if test_name in SOLO_TESTS:
                solo_tests.extend([test_name + " " + " ".join(p) for p in params])
            else:
                tests_with_params.extend([test_name + " " + " ".join(p) for p in params])

    # Remove extended tests (tests, whose expected running time is longer than cutoff, see '--extended') and sort
    result = [t for t in tests_with_params if get_test_time(t) <= cutoff]
    result.sort(key=lambda x: (-get_test_time(x), x))

    result_solo = [t for t in solo_tests if get_test_time(t) <= cutoff]
    result_solo.sort(key=lambda x: (-get_test_time(x), x))

    solo_position_start = len(result)
    result = result + result_solo
    return result, solo_position_start


class RPCCoverage():
    """
    Coverage reporting utilities for test_runner.

    Coverage calculation works by having each test script subprocess write
    coverage files into a particular directory. These files contain the RPC
    commands invoked during testing, as well as a complete listing of RPC
    commands per `bitcoin-cli help` (`rpc_interface.txt`).

    After all tests complete, the commands run are combined and diff'd against
    the complete list to calculate uncovered RPC commands.

    See also: test/functional/test_framework/coverage.py

    """

    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="coverage")
        self.flag = '--coveragedir={}'.format(self.dir)

    def report_rpc_coverage(self):
        """
        Print out RPC commands that were unexercised by tests.

        """
        uncovered = self._get_uncovered_rpc_commands()

        if uncovered:
            print("Uncovered RPC commands:")
            print("".join(("  - {}\n".format(i)) for i in sorted(uncovered)))
        else:
            print("All RPC commands covered.")

    def cleanup(self):
        return shutil.rmtree(self.dir)

    def _get_uncovered_rpc_commands(self):
        """
        Return a set of currently untested RPC commands.

        """
        # This is shared from `test/functional/test-framework/coverage.py`
        reference_filename = 'rpc_interface.txt'
        coverage_file_prefix = 'coverage.'

        coverage_ref_filename = os.path.join(self.dir, reference_filename)
        coverage_filenames = set()
        all_cmds = set()
        covered_cmds = set()

        if not os.path.isfile(coverage_ref_filename):
            raise RuntimeError("No coverage reference found")

        with open(coverage_ref_filename, 'r') as f:
            all_cmds.update([i.strip() for i in f.readlines()])

        for root, dirs, files in os.walk(self.dir):
            for filename in files:
                if filename.startswith(coverage_file_prefix):
                    coverage_filenames.add(os.path.join(root, filename))

        for filename in coverage_filenames:
            with open(filename, 'r') as f:
                covered_cmds.update([i.strip() for i in f.readlines()])

        return all_cmds - covered_cmds


def save_results_as_junit(test_results, file_name, time):
    """
    Save tests results to file in JUnit format

    See http://llg.cubic.org/docs/junit/ for specification of format
    """
    e_test_suite = ET.Element("testsuite",
                              {"name": "bitcoin_sv_tests",
                               "tests": str(len(test_results)),
                               #"errors":
                               "failures": str(len([t for t in test_results if t.status == "Failed"])),
                               "id": "0",
                               "skipped": str(len([t for t in test_results if t.status == "Skipped"])),
                               "time": str(time),
                               "timestamp": datetime.datetime.now().isoformat('T')
                               })

    for test_result in test_results:
        e_test_case = ET.SubElement(e_test_suite, "testcase",
                                    {"name": test_result.name,
                                     "classname": test_result.name,
                                     "time": str(test_result.time)
                                     }
                                    )
        if test_result.status == "Skipped":
            ET.SubElement(e_test_case, "skipped")
        elif test_result.status == "Failed":
            ET.SubElement(e_test_case, "failure")
        # no special element for passed tests

        ET.SubElement(e_test_case, "system-out").text = test_result.stdout
        ET.SubElement(e_test_case, "system-err").text = test_result.stderr

    ET.ElementTree(e_test_suite).write(
        file_name, "UTF-8", xml_declaration=True)


class Timings():
    """
    Takes care of loading, merging and saving tests execution times.
    """

    def __init__(self, timing_file):
        self.timing_file = timing_file
        self.existing_timings = self.load_timings()

    def load_timings(self):
        if os.path.isfile(self.timing_file):
            with open(self.timing_file) as f:
                return json.load(f)
        else:
            return []

    def get_merged_timings(self, new_timings):
        """
        Return new list containing existing timings updated with new timings
        Tests that do not exists are not removed
        """

        key = 'name'
        merged = {}
        for item in self.existing_timings + new_timings:
            if item[key] in merged:
                merged[item[key]].update(item)
            else:
                merged[item[key]] = item

        # Sort the result to preserve test ordering in file
        merged = list(merged.values())
        merged.sort(key=lambda t, key=key: t[key])
        return merged

    def save_timings(self, test_results):
        # we only save test that have passed - timings for failed test might be
        # wrong (timeouts or early fails)
        passed_results = [t for t in test_results if t.status == 'Passed']
        new_timings = list(map(lambda t: {'name': t.name, 'time': t.time},
                               passed_results))
        merged_timings = self.get_merged_timings(new_timings)

        with open(self.timing_file, 'w') as f:
            json.dump(merged_timings, f, indent=True)

# Prints a user friendly report of currently running jobs


printed_lines = 0


def print_log(data="", jobs=None, console=False):
    global printed_lines
    if not console:
        if data:
            print(data)
    else:
        # Clear * Running jobs *
        for i in range(printed_lines):
            sys.stdout.write("\033[F")  # back to previous line
            printed_lines = 0

        # Print data
        for line in data.splitlines():
            sys.stdout.write("\033[K")  # clear line
            print(line)

        # Print * Running jobs *
        sys.stdout.write("\033[K")  # clear line
        print("******* Running jobs *******")
        printed_lines += 1
        for job in running_jobs:
            sys.stdout.write("\033[K")  # clear line
            print("%s %ss" % (job[0], f"{time.time() - job[1]:.0f}"))
            printed_lines += 1


# Prints running jobs if they take a long time
def check_jobs(jobs=None):
    printed_header = False
    for job in jobs:
        if time.time() - job[1] > 90:
            if not printed_header:
                print("\n  ******* Long running jobs *******")
                printed_header = True

            print_log(data="  * %s is running for %s" % (job[0], f"{time.time() - job[1]:.0f}"), jobs=jobs)

    if printed_header:
        print("  *********************************")


if __name__ == '__main__':
    main()
