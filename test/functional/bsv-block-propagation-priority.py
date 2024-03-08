#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Verify that a BSV node is able to prioritise block propagation above other
messages over the P2P.

# For Release build with sanitizers enabled (TSAN / ASAN / UBSAN), recommended timeoutfactor is 2.
# For Debug build, recommended timeoutfactor is 2.
# For Debug build with sanitizers enabled, recommended timeoutfactor is 3.
"""

from test_framework.associations import AssociationCB
from test_framework.comptool import logger
from test_framework.mininode import FromHex, ToHex, CTransaction, CTxIn, CTxOut, COutPoint, CInv, msg_tx, msg_getdata, msg_mempool
from test_framework.script import CScript, OP_TRUE
from test_framework.streams import StreamType, BlockPriorityStreamPolicy, DefaultStreamPolicy
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, assert_equal, assert_greater_than

from itertools import islice


# Create initial funding transaction
def make_funding_transaction(node, n_outputs=30, value=100000000):

    ftx = CTransaction()
    for i in range(n_outputs):
        ftx.vout.append(CTxOut(value, CScript([OP_TRUE])))

    # fund the transcation:
    ftxHex = node.fundrawtransaction(ToHex(ftx), {'changePosition': len(ftx.vout)})['hex']
    ftxHex = node.signrawtransaction(ftxHex)['hex']
    ftx = FromHex(CTransaction(), ftxHex)
    ftx.rehash()

    node.sendrawtransaction(ftxHex)
    node.generate(1)

    return ftx


# Generate a list of transactions
def transaction_generator(funding_tx, count=-1, exp_rate=30):

    def _children_tx_generator(parent_tx, exp_rate):
        for i in range(exp_rate):
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, i), b''))
            out_value = parent_tx.vout[i].nValue // exp_rate - 1000
            for _ in range(exp_rate):
                tx.vout.append(CTxOut(out_value, CScript([OP_TRUE])))
            tx.rehash()
            yield tx

    last_generation = [funding_tx]
    while True:
        new_generation = []
        for old_tx in last_generation:
            for new_tx in _children_tx_generator(old_tx, exp_rate):
                if count == 0: return
                count -= 1
                new_generation.append(new_tx)
                yield new_tx

        last_generation = new_generation


# Subclass association callback type to get notifications we are interested in
class MyAssociationCB(AssociationCB):
    def __init__(self):
        super().__init__()
        self.reset_msg_counts()
        self.block_inv_stream_type = StreamType.UNKNOWN

    def reset_msg_counts(self):
        self.msg_count = 0
        self.block_count = 0
        self.tx_count = 0
        self.block_msg_position = -1

    # Track received tx messages
    def on_tx(self, stream, message):
        super().on_tx(stream, message)
        self.msg_count += 1
        self.tx_count += 1

    # Track received block messages
    def on_block(self, stream, message):
        super().on_block(stream, message)
        self.msg_count += 1
        self.block_count += 1
        self.block_msg_position = self.msg_count

    # Track received inventory messages
    def on_inv(self, stream, message):
        super().on_inv(stream, message)
        for inv in message.inv:
            if inv.type == 2:
                # Remember stream we got block INV over
                self.block_inv_stream_type = stream.stream_type


# Main test class
class BlockPriorityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.num_txns = 2000

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        inv_items = []
        block_priority_block_msg_pos = []
        default_block_msg_pos = []
        last_msg_pos = self.num_txns + 1

        # Initial node setup
        extra_args = [
            '-checkmempool=0',
            '-mindebugrejectionfee=0.00000250'
        ]
        with self.run_node_with_connections("Setup node", 0, extra_args, 1) as connections:
            conn = connections[0]

            # Create and send some transactions to the node
            node = self.nodes[0]
            node.generate(100)
            funding_tx = make_funding_transaction(node)
            tx_generator = transaction_generator(funding_tx)
            for tx in islice(tx_generator, self.num_txns):
                inv_items.append(CInv(1, tx.sha256))
                conn.send_message(msg_tx(tx))
            wait_until(lambda: node.getmempoolinfo()['size'] == self.num_txns, timeout=(240 * self.options.timeoutfactor))

        # Restart node with associations
        associations_stream_policies = [BlockPriorityStreamPolicy(), DefaultStreamPolicy(), BlockPriorityStreamPolicy(), DefaultStreamPolicy()]
        extra_args = [
            '-whitelist=127.0.0.1',
            '-mindebugrejectionfee=0.00000250'
        ]
        with self.run_node_with_associations("Test block priority", 0, extra_args, associations_stream_policies, cb_class=MyAssociationCB) as associations:
            # Wait for node to fully reinitialise itself
            node = self.nodes[0]
            wait_until(lambda: node.getmempoolinfo()['size'] == self.num_txns, timeout=180)

            # Send MEMPOOL request so node will accept our GETDATA for transactions in the mempool
            for association in associations:
                association.send_message(msg_mempool())
                # This request will result in us requesting all the txns. Wait until that finishes and
                # then reset our message counts in preperation for the real test to come.
                wait_until(lambda: association.callbacks.msg_count == self.num_txns)
                association.callbacks.reset_msg_counts()

            # Send GETDATA to request txns and a block, with the block as the last item in the list
            blockhash = int(node.getbestblockhash(), 16)
            inv_items.append(CInv(2, blockhash))
            for association in associations:
                association.send_message(msg_getdata(inv_items))

            # Wait for all GETDATA requests to have a response
            for association in associations:
                wait_until(lambda: association.callbacks.block_count == 1)

                # Remember at what position we got the block msg for the different policies
                if type(association.stream_policy) is BlockPriorityStreamPolicy:
                    block_priority_block_msg_pos.append(association.callbacks.block_msg_position)
                    logger.info("BlockPriority policy block received at position {}".format(association.callbacks.block_msg_position))
                elif type(association.stream_policy) is DefaultStreamPolicy:
                    default_block_msg_pos.append(association.callbacks.block_msg_position)
                    logger.info("Default policy block received at position {}".format(association.callbacks.block_msg_position))

            # For the DEFAULT policy, the block will have been received last (because it was requested last)
            for pos in default_block_msg_pos:
                assert_equal(pos, last_msg_pos)
            # For the BLOCKPRIORITY policy, the block should have been received sooner (this is possibly
            # slightly racy, but it's been very safe on all systems I've tried it on)
            avg_pos = sum(block_priority_block_msg_pos) / len(block_priority_block_msg_pos)
            assert_greater_than(last_msg_pos, avg_pos)

            # Generate a new block to trigger a block INV and wait for the INV
            node.generate(1)
            for association in associations:
                wait_until(lambda: association.callbacks.block_inv_stream_type != StreamType.UNKNOWN)

                # Verify that BlockPriority associations got block INV over the high priority stream
                if type(association.stream_policy) is BlockPriorityStreamPolicy:
                    assert_equal(association.callbacks.block_inv_stream_type, StreamType.DATA1)


if __name__ == '__main__':
    BlockPriorityTest().main()
