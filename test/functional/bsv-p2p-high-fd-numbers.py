#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin SV developers
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Regression test: the P2P layer must keep working when every socket bitcoind
creates is numbered above FD_SETSIZE (1024).

Background: from 1.2.3 LevelDB may keep thousands of table files open. Linux
hands out the lowest free descriptor number, so on a busy node every new
socket ended up numbered >= 1024 and the select()-based P2P loop rejected it
as a "non-selectable socket", inbound and outbound alike, until the node had
no peers left. The loop now uses poll() on POSIX, which has no such limit.

A LevelDB-driven reproduction is not feasible in regtest: LevelDB mmaps the
first 4096 table files without holding a descriptor, so it would take more
than 4096 open tables to push socket numbers past 1024. Instead node 0 is
started through a small wrapper that raises the descriptor limit, opens
HOG_FDS inheritable descriptors on /dev/null and then execs bitcoind, so that
every socket bitcoind creates is numbered above FD_SETSIZE.

On an unfixed build this fails at startup rather than at connect time: the
listen socket itself is rejected ("Couldn't create a listenable socket") and
bitcoind exits with "Failed to listen on any port".
"""
import os
import resource
import shutil
import stat
import sys

from test_framework.mininode import P2PHandler, P2PEventHandler, NetworkThread
from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.transport import Connection
from test_framework.util import (assert_equal, assert_greater_than,
                                 check_for_log_msg, connect_nodes, p2p_port,
                                 wait_until)

# Descriptors the wrapper holds open before exec'ing bitcoind. Comfortably
# above FD_SETSIZE so that even the listen socket is numbered past it.
HOG_FDS = 1100

# The wrapper needs room for its own descriptors plus whatever bitcoind opens.
MIN_HARD_LIMIT = 2048

WRAPPER_SOURCE = """#!/usr/bin/env python3
import os
import resource
import sys

HOG_FDS = {hog_fds}

soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
need = HOG_FDS + 256
if soft < need:
    resource.setrlimit(resource.RLIMIT_NOFILE, (need, hard))

# bitcoind inherits whatever stdin the test harness was started with, which
# in some environments is a socket. Replace it so that the only sockets the
# test sees in /proc/<pid>/fd are bitcoind's own.
devnull = os.open('/dev/null', os.O_RDONLY)
os.dup2(devnull, 0)
os.close(devnull)

# Keep the descriptors open across the exec so bitcoind inherits them.
for _ in range(HOG_FDS):
    fd = os.open('/dev/null', os.O_RDONLY)
    os.set_inheritable(fd, True)

os.execv(sys.argv[1], sys.argv[1:])
"""


class P2PHighFdNumbers(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        if not sys.platform.startswith('linux'):
            raise SkipTest("This test relies on Linux descriptor numbering and /proc.")

        _, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        if hard != resource.RLIM_INFINITY and hard < MIN_HARD_LIMIT:
            raise SkipTest("Hard RLIMIT_NOFILE {} is too low to hold {} descriptors open."
                           .format(hard, HOG_FDS))

        bitcoind = shutil.which(os.getenv("BITCOIND", "bitcoind"))
        if bitcoind is None:
            raise SkipTest("Cannot locate the bitcoind binary to wrap.")

        wrapper = os.path.join(self.options.tmpdir, "fd_hog_wrapper.py")
        with open(wrapper, "w", encoding="utf8") as f:
            f.write(WRAPPER_SOURCE.format(hog_fds=HOG_FDS))
        os.chmod(wrapper, os.stat(wrapper).st_mode | stat.S_IXUSR)

        # Node 0 runs behind the fd hog; node 1 is a plain peer for the
        # outbound direction.
        self.add_nodes(self.num_nodes, binaries=[[sys.executable, wrapper, bitcoind], None])
        self.start_nodes()

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        # The wrapper's descriptors were inherited, so bitcoind is holding well
        # over FD_SETSIZE descriptors before it has opened a single socket.
        # bitcoind may have exec'd a helper of its own, so look at the direct
        # child the framework started.
        fd_dir = "/proc/{}/fd".format(node0.process.pid)
        fd_count = len(os.listdir(fd_dir))
        self.log.info("node0 holds %d descriptors", fd_count)
        assert_greater_than(fd_count, HOG_FDS)

        # Inbound: a connection to node0's listen socket completes a handshake.
        self.log.info("Inbound connection to a node whose sockets are all >= FD_SETSIZE")
        event_handler = P2PEventHandler()
        handler = P2PHandler(Connection('127.0.0.1', p2p_port(0), event_handler), node0)
        event_handler.add_connection(handler)
        # Daemon so that a thread that fails to stop cannot hang the run at
        # interpreter exit; the bounded join below then reports it instead.
        # Same pattern as the webhook server threads in bsv-safe-mode.py.
        network_thread = NetworkThread()
        network_thread.daemon = True
        network_thread.start()
        try:
            event_handler.wait_for_verack()
            wait_until(lambda: len(node0.getpeerinfo()) == 1, timeout=30)

            # Outbound: node0 connects to node1 and node1 sees it.
            self.log.info("Outbound connection from the same node")
            connect_nodes(self.nodes, 0, 1)
            wait_until(lambda: len(node1.getpeerinfo()) >= 1, timeout=30)
            wait_until(lambda: len(node0.getpeerinfo()) == 2, timeout=30)

            # None of the old select()-era failure modes fired.
            for msg in ["non-selectable socket",
                        "Couldn't create a listenable socket",
                        "Reducing -maxconnections"]:
                assert not check_for_log_msg(node0, msg), "unexpected log line: {}".format(msg)

            # Belt and braces: the peer sockets really are numbered past
            # FD_SETSIZE. The node is live, so an entry can vanish between
            # listing the directory and reading the link; skip those. The
            # standard descriptors are inherited, not bitcoind's, so skip
            # them too.
            socket_fds = []
            for fd in os.listdir(fd_dir):
                if int(fd) < 3:
                    continue
                try:
                    target = os.readlink(os.path.join(fd_dir, fd))
                except OSError:
                    continue
                if target.startswith("socket:"):
                    socket_fds.append(int(fd))
            self.log.info("node0 socket descriptors: %s", sorted(socket_fds))
            assert socket_fds, "no socket descriptors found in {}".format(fd_dir)
            assert_greater_than(min(socket_fds), 1024)
        finally:
            handler.close()
            network_thread.join(timeout=30)
        # Outside the finally so that a genuine test failure above is what gets
        # reported, not a secondary complaint about the thread.
        assert not network_thread.is_alive(), "NetworkThread did not stop within 30 s"

        assert_equal(len(node1.getpeerinfo()), 1)


if __name__ == '__main__':
    P2PHighFdNumbers().main()
