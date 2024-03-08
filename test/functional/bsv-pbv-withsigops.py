#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
This test shows making the "hard" blocks that take long to validate
is possible and without artificially extending validation times (with RPC) we
actually make blocks with transactions that take long to validate.
The easier block to validate will be active.

The test:
We are validating 2 blocks on current tip (block 1), the situation we have is:
    1
  /  \
 2    3

block 2 is hard to validate but block 3 is easy to validate so block 3 should
win the validation race and be set as active chain tip

*To see how validation times are extended artificially with RPC calls, for
 testing parallel block validation functionality look at
 test bsv-pbv-firstvalidactive.py

NOTE: Blocks have to be sent from the same node for the test to be deterministic
      otherwise it can happen that easier block is read from network before the
      hard one so the validation race condition can never (is harder to) occur
"""
import time
import glob

from test_framework.blocktools import sign_tx
from test_framework.blocktools import (create_block, create_coinbase, prepare_init_chain)
from test_framework.mininode import (
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    p2p_port,
    assert_equal,
    wait_until)
from test_framework.script import *
from test_framework.blocktools import create_transaction
from test_framework.key import CECKey


class PreviousSpendableOutput:

    def __init__(self, tx=CTransaction(), n=-1):
        self.tx = tx
        self.n = n  # the output we're spending


class PBVWithSigOps(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-whitelist=127.0.0.1"]]
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.chain = ChainManager()

    def sign_expensive_tx(self, tx, spend_tx, n, sigChecks):
        sighash = SignatureHashForkId(
            spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)

        tx.vin[0].scriptSig = CScript(
            [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID])),
             self.coinbase_pubkey] * sigChecks
            + [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID])),
               self.coinbase_pubkey])

    def get_hard_transactions(self, spend, money_to_spend, num_of_transactions, num_of_sig_checks, expensive_script):
        txns = []
        for _ in range(0, num_of_transactions):
            money_to_spend = money_to_spend - 1  # one satoshi to fee
            tx2 = create_transaction(spend.tx, spend.n, b"", money_to_spend, CScript(expensive_script))
            sign_tx(tx2, spend.tx, spend.n, self.coinbase_key)
            tx2.rehash()
            txns.append(tx2)

            money_to_spend = money_to_spend - 1
            tx3 = create_transaction(tx2, 0, b"", money_to_spend, scriptPubKey=CScript([OP_TRUE]))
            self.sign_expensive_tx(tx3, tx2, 0, num_of_sig_checks)
            tx3.rehash()
            txns.append(tx3)

            spend = PreviousSpendableOutput(tx3, 0)
        return txns

    def run_test(self):
        block_count = 0

        # Create a P2P connection
        node0 = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node0)
        node0.add_connection(connection)

        network_thread = NetworkThread()
        network_thread.start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node0.wait_for_verack()

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))

        _, out, block_count = prepare_init_chain(self.chain, 101, 100, block_0=False, start_block=0, node=node0)

        self.log.info("waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)

        block1_num = block_count - 1

        # num of sig operations in one transaction
        num_of_sig_checks = 70

        expensive_scriptPubKey = [OP_DUP, OP_HASH160, hash160(self.coinbase_pubkey),
                                  OP_EQUALVERIFY, OP_CHECKSIG, OP_DROP] * num_of_sig_checks + [OP_DUP, OP_HASH160,
                                                                                               hash160(
                                                                                                   self.coinbase_pubkey),
                                                                                               OP_EQUALVERIFY,
                                                                                               OP_CHECKSIG]

        money_to_spend = 5000000000
        spend = out[0]

        block2_hard = self.chain.next_block(block_count)

        # creates 4000 hard transaction and 4000 transaction to spend them. It will be 8k transactions in total
        add_txns = self.get_hard_transactions(spend, money_to_spend=money_to_spend, num_of_transactions=4000,
                                              num_of_sig_checks=num_of_sig_checks,
                                              expensive_script=expensive_scriptPubKey)
        self.chain.update_block(block_count, add_txns)
        block_count += 1
        self.log.info(f"block2_hard hash: {block2_hard.hash}")

        self.chain.set_tip(block1_num)
        block3_easier = self.chain.next_block(block_count)
        add_txns = self.get_hard_transactions(spend, money_to_spend=money_to_spend, num_of_transactions=1000,
                                              num_of_sig_checks=num_of_sig_checks,
                                              expensive_script=expensive_scriptPubKey)
        self.chain.update_block(block_count, add_txns)
        self.log.info(f"block3_easier hash: {block3_easier.hash}")

        node0.send_message(msg_block(block2_hard))
        node0.send_message(msg_block(block3_easier))

        def wait_for_log():
            text_activation = f"Block {block2_hard.hash} was not activated as best"
            text_block2 = "Verify 8000 txins"
            text_block3 = "Verify 2000 txins"
            results = 0
            for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
                if text_activation in line:
                    results += 1
                elif text_block2 in line:
                    results += 1
                elif text_block3 in line:
                    results += 1
            return True if results == 3 else False

        # wait that everything is written to the log
        # try accounting for slower machines by having a large timeout
        wait_until(wait_for_log, timeout=120)

        text_activation = f"Block {block2_hard.hash} was not activated as best"
        text_block2 = "Verify 8000 txins"
        text_block3 = "Verify 2000 txins"
        for line in open(glob.glob(self.options.tmpdir + "/node0" + "/regtest/bitcoind.log")[0]):
            if text_activation in line:
                self.log.info(f"block2_hard was not activated as block3_easy won the validation race")
            elif text_block2 in line:
                line = line.split()
                self.log.info(f"block2_hard took {line[len(line) - 1]} to verify")
            elif text_block3 in line:
                line = line.split()
                self.log.info(f"block3_easy took {line[len(line)-1]} to verify")

        assert_equal(block3_easier.hash, self.nodes[0].getbestblockhash())
        node0.connection.close()


if __name__ == '__main__':
    PBVWithSigOps().main()
