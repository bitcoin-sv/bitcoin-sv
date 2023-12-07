#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Testing scenario: our node has one active chain and then connects to node with longer chain but invalid for our
software. We then check whether the node detects block that violates TTOR (Topological Transaction Ordering Rule).
Invalid chain should be at least MIN_TTOR_VALIDATION_DISTANCE apart from the active tip in order for node to start detecting invalid TTOR.
MIN_TTOR_VALIDATION_DISTANCE = 100

The situation we test looks like this

               |
          101 blocks
               |
     /                \
valid branch    invalid branch

Test:
1. Create 101 blocks and send to node in order to provide spendable outputs.
2. Create valid branch of 101 blocks with valid transaction ordering and send to node. Node's active chain's height is 202.
3. Create invalid branch of 101 blocks with invalid transaction ordering. Send first 100 headers to node.
   Node does not ask for blocks yet. It is true that the chain is MIN_TTOR_VALIDATION_DISTANCE apart from the active tip (202 - 102 = 100),
   but node does not ask for the block if alternative chain is not the same or higher than node's active chain.
4. Send last header (height 202) of invalid chain. GETDATA message is received after that.
   Respond with the first block. TTOR violation is detected.
"""
import glob

from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    msg_headers,
    CBlockHeader
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    p2p_port,
    wait_until
)
from test_framework.script import CScript, OP_TRUE
from test_framework.blocktools import create_transaction, PreviousSpendableOutput
from test_framework.cdefs import MIN_TTOR_VALIDATION_DISTANCE
import time


class BSVCheckTTORViolation(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-whitelist=127.0.0.1"]]
        self.chain = ChainManager()

    # generating transactions in order so first transaction's output will be input for second transaction
    def get_chained_transactions(self, spend, num_of_transactions):
        money_to_spend = 5000000000
        txns = []
        for _ in range(0, num_of_transactions):
            money_to_spend = money_to_spend - 1  # one satoshi to fee
            tx2 = create_transaction(spend.tx, spend.n, b"", money_to_spend, CScript([OP_TRUE]))
            txns.append(tx2)

            money_to_spend = money_to_spend - 1
            tx3 = create_transaction(tx2, 0, b"", money_to_spend, scriptPubKey=CScript([OP_TRUE]))
            txns.append(tx3)

            spend = PreviousSpendableOutput(tx3, 0)
        return txns

    def run_test(self):
        block_count = 0

        # Create a P2P connections
        node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection)

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))

        getDataMessages = []

        def on_getdata(conn, message):
            getDataMessages.append(message)

        node0.on_getdata = on_getdata

        # ***** 1. *****
        # starting_blocks are needed to provide spendable outputs
        starting_blocks = MIN_TTOR_VALIDATION_DISTANCE + 1
        for i in range(starting_blocks):
            block = self.chain.next_block(block_count)
            block_count += 1
            self.chain.save_spendable_output()
            node0.send_message(msg_block(block))
        out = []
        for i in range(starting_blocks):
            out.append(self.chain.get_spendable_output())
        self.nodes[0].waitforblockheight(starting_blocks)

        tip_block_index = block_count - 1

        self.log.info("Block tip height: %d " % block_count)

        # ***** 2. *****
        # branch with blocks that do not violate TTOR
        valid_ttor_branch_height = MIN_TTOR_VALIDATION_DISTANCE + 1
        for i in range(0, valid_ttor_branch_height):
            block = self.chain.next_block(block_count, spend=out[i], extra_txns=8)
            block_count += 1
            node0.send_message(msg_block(block))
        chaintip_valid_branch = block
        self.nodes[0].waitforblockheight(starting_blocks + valid_ttor_branch_height)

        self.log.info("Node's active chain height: %d " % (starting_blocks + valid_ttor_branch_height))

        # ***** 3. *****
        # branch with invalid transaction order that will try to cause a reorg
        self.chain.set_tip(tip_block_index)
        blocks_invalid_ttor = []
        headers_message = msg_headers()
        headers_message.headers = []
        invalid_ttor_branch_height = MIN_TTOR_VALIDATION_DISTANCE + 1
        for i in range(0, invalid_ttor_branch_height):
            spend = out[i]
            block = self.chain.next_block(block_count)
            add_txns = self.get_chained_transactions(spend, num_of_transactions=10)

            # change order of transaction that output uses transaction that comes later (makes block violate TTOR)
            temp1 = add_txns[1]
            temp2 = add_txns[2]
            add_txns[1] = temp2
            add_txns[2] = temp1
            self.chain.update_block(block_count, add_txns)
            blocks_invalid_ttor.append(block)
            block_count += 1

            if (i == 0):
                first_block = block
            # wait with sending header for the last block
            if (i != MIN_TTOR_VALIDATION_DISTANCE):
                headers_message.headers.append(CBlockHeader(block))

        self.log.info("Sending %d headers..." % MIN_TTOR_VALIDATION_DISTANCE)

        node0.send_message(headers_message)
        # Wait to make sure we do not receive GETDATA messages yet.
        time.sleep(1)
        # Check that getData is not received until this chain is long at least as the active chain.
        assert_equal(len(getDataMessages), 0)

        self.log.info("Sending 1 more header...")
        # Send HEADERS message for the last block.
        headers_message.headers = [CBlockHeader(block)]
        node0.send_message(headers_message)
        node0.wait_for_getdata()
        self.log.info("Received GETDATA.")
        assert_equal(len(getDataMessages), 1)

        # Send the first block on invalid chain. Chain should be invalidated.
        node0.send_message(msg_block(first_block))

        def wait_to_invalidate_fork():
            chaintips = self.nodes[0].getchaintips()
            if len(chaintips) > 1:
                chaintips_status = [chaintips[0]["status"], chaintips[1]["status"]]
                if "active" in chaintips_status and "invalid" in chaintips_status:
                    active_chain_tip_hash = chaintips[0]["hash"] if chaintips[0]["status"] == "active" else chaintips[1]["hash"]
                    invalid_fork_tip_hash = chaintips[0]["hash"] if chaintips[0]["status"] == "invalid" else chaintips[1]["hash"]
                    assert(active_chain_tip_hash != invalid_fork_tip_hash)

                    for block in blocks_invalid_ttor:
                        if block.hash == invalid_fork_tip_hash:
                            return True
                    return False
                else:
                    return False
            else:
                return False

        wait_until(wait_to_invalidate_fork)

        # chaintip of valid branch should be active
        assert_equal(self.nodes[0].getbestblockhash(), chaintip_valid_branch.hash)

        # check log file that reorg didnt happen
        disconnect_block_log = False
        for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
            if f"Disconnect block" in line:
                disconnect_block_log = True
                self.log.info("Found line: %s", line.strip())
                break

        # we should not find information about disconnecting blocks
        assert_equal(disconnect_block_log, False)

        # check log file that contains information about TTOR violation
        ttor_violation_log = False
        for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
            if f"violates TTOR order" in line:
                ttor_violation_log = True
                self.log.info("Found line: %s", line.strip())
                break

        # we should find information about TTOR being violated
        assert_equal(ttor_violation_log, True)


if __name__ == '__main__':
    BSVCheckTTORViolation().main()
