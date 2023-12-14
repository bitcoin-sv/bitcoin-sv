#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
This test checks whether block files are created as expected in different cases.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.comptool import TestInstance
from test_framework.cdefs import (ONE_MEGABYTE)
from test_framework.blocktools import ChainManager, prepare_init_chain
from test_framework.mininode import (NetworkThread, NodeConn, NodeConnCB, msg_block)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (p2p_port, assert_equal)
import glob
import time

"""
RunnerNode represents local node with its own block chain
and connection to a remote node

During initialization it:
- prepares spendable output for spending in consecutive test blocks
- fills remote node's blockfile with more than 1MB of contents so
  that the next block will always go into the beginning of the second blockfile
"""


class RunnerNode(NodeConnCB):
    def __init__(self, remote_node, node_number):
        super(RunnerNode,self).__init__()
        self.chain = ChainManager()
        self.next_block = 0
        self.remote_node = remote_node
        self.node_number = node_number

        connections = []
        connections.append(
            NodeConn('127.0.0.1', p2p_port(self.node_number), self.remote_node, self))
        self.add_connection(connections[0])

    def finish_setup_after_network_is_started(self, tmp_dir):
        self.wait_for_verack()

        self.chain.set_genesis_hash(int(self.remote_node.getbestblockhash(), 16))

        # Build the blockchain
        self.tip = int(self.remote_node.getbestblockhash(), 16)
        self.block_time = self.remote_node.getblock(
            self.remote_node.getbestblockhash())['time'] + 1

        _, _, self.next_block = prepare_init_chain(self.chain, 100, 0, block_0=False, start_block=0, node=self)

        self.sync_with_ping()

        # Create a new block - half of max block file size. Together with above blocks
        # this leaves less than 1MB of free space in the first block file
        self.create_and_send_block(ONE_MEGABYTE)
        assert(
            len(glob.glob(tmp_dir + "/node" + str(self.node_number) + "/regtest/blocks/blk0000*.dat"))
            == 1) # sanity check that there is still only one file

    def create_and_send_block(self, block_size):
        out = self.chain.get_spendable_output()
        block = self.chain.next_block(self.next_block, spend=out, block_size=block_size)
        self.next_block += 1
        self.chain.save_spendable_output()
        self.send_and_ping(msg_block(block))

        return block, self.next_block - 1


class BlockStoringInFile(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.runner_nodes = []
        self.setup_clean_chain = True
        self.mining_block_max_size = 20 * ONE_MEGABYTE
        self.excessive_block_size = 22 * ONE_MEGABYTE

        extra_header_space = 8 # one block header takes up 8 bytes
        self.preferred_blockfile_size = 2 * (ONE_MEGABYTE + extra_header_space)
        # +1 is a magic number for extra block file space as check whether the
        # file is already full is written with >= instead of >
        self.preferred_blockfile_size += 1

        self.extra_args = ['-whitelist=127.0.0.1',
                           "-excessiveblocksize=%d" % self.excessive_block_size,
                           "-preferredblockfilesize=%d" % self.preferred_blockfile_size,
                           "-blockmaxsize=%d" % self.mining_block_max_size]

    def setup_network(self):
        self.add_nodes(self.num_nodes)

        for i in range(self.num_nodes):
            self.start_node(i, self.extra_args)
            self.runner_nodes.append(RunnerNode(self.nodes[i], i))

        # Start up network handling in another thread
        self._network_thread = NetworkThread()
        self._network_thread.start()

        for i in range(self.num_nodes):
            self.runner_nodes[i].finish_setup_after_network_is_started(self.options.tmpdir)

    def __count_blk_files(self, block_number, expected_number_of_files, node_number):
        blockfile_count = len(glob.glob(self.options.tmpdir + "/node" + str(node_number) + "/regtest/blocks/blk0000*.dat"))

        assert blockfile_count == expected_number_of_files, ("unexpected blockfile count for block: "
                                                             + str(block_number)
                                                             + "; node: "
                                                             + str(node_number)
                                                             + "; expected: "
                                                             + str(expected_number_of_files)
                                                             + "; got: "
                                                             + str(blockfile_count))

    def __compare_local_and_remote_block_size(self, local_block, remote_node):
        remote_best_block_hash = remote_node.getbestblockhash()
        assert_equal(remote_best_block_hash, local_block.hash)

        # check that we can successfully read block from file
        remote_block = remote_node.getblock(remote_best_block_hash)
        assert_equal(remote_block['size'], len(local_block.serialize()))

    def __send_and_test_block(self, runner_node, block_size, expected_number_of_files):
        local_block, block_number = runner_node.create_and_send_block(block_size)
        self.__compare_local_and_remote_block_size(local_block, runner_node.remote_node)
        self.__count_blk_files(block_number, expected_number_of_files, runner_node.node_number)

    def __test_two_blocks_less_than_preferred_file_size_in_single_file(self, runner_node):
        print("- test two blocks size is less than preferred file size, put in single file")
        self.__send_and_test_block(runner_node, ONE_MEGABYTE, 2)
        self.__send_and_test_block(runner_node, ONE_MEGABYTE, 2)

    def __test_one_block_in_file_second_block_exceeds_preferred_file_size(self, runner_node):
        print("- test one block in file, second block exceeds preferred file size")
        self.__send_and_test_block(runner_node, ONE_MEGABYTE, 2)
        self.__send_and_test_block(runner_node, ONE_MEGABYTE * 2, 3)
        self.__send_and_test_block(runner_node, ONE_MEGABYTE, 4)
        self.__send_and_test_block(runner_node, ONE_MEGABYTE, 4)

    def __test_block_larger_than_preferred_file_size(self, runner_node):
        print("- test block larger than preferred filesize")
        self.__send_and_test_block(runner_node, ONE_MEGABYTE * 5, 2)
        self.__send_and_test_block(runner_node, ONE_MEGABYTE, 3)
        self.__send_and_test_block(runner_node, ONE_MEGABYTE, 3)

    def run_test(self):
        # each test should run with different runner/test node combination as we want to start
        # with a fresh blockfile structure

        self.__test_two_blocks_less_than_preferred_file_size_in_single_file(self.runner_nodes[0])
        self.__test_one_block_in_file_second_block_exceeds_preferred_file_size(self.runner_nodes[1])
        self.__test_block_larger_than_preferred_file_size(self.runner_nodes[2])


if __name__ == '__main__':
    BlockStoringInFile().main()
