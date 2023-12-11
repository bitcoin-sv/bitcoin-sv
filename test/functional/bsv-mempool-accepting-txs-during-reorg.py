#!/usr/bin/env python3
# Copyright (c) 2021  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from time import sleep

from test_framework.blocktools import create_block, create_coinbase
from test_framework.cdefs import DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, \
    msg_headers
from test_framework.script import CScript, OP_DROP, OP_CHECKSIG, SIGHASH_ALL, SIGHASH_FORKID, \
    SignatureHashForkId, OP_MUL
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, check_mempool_equals

"""
    We are testing acceptance of the transactions to the mempool during the reorg.
     - First, we are creating block structure like this:

       +-----root-----+
       |    |    |    |
      a1    b1   c1   d1
            |    |    |
            b2   c2   d2

       a1 - a block with 2000 p2pk transactions
       b1,b2,c1,c2,d1,d2 each a block with several transactions with long evaluating scripts

       and we send a1 which becomes a tip.

    1. Then we send the same 2000 transactions from the a1 block through p2p, and blocks b1 and b2. This causes
       a reorg, and we are disconnecting a1 block and validating b1 and b2 while validating transactions from a1.
       So while the block a1 is disconnected we are accepting transactions to the mempool resulting in having several
       transactions both in disconnectpool and mempool.


    2. We set b1 as invalid forcing that a1 becomes a tip again.
       Then we send the other 2000 transactions which are in conflict to transactions from the a1 block through p2p,
       and blocks c1 and c2. This also causes a reorg, and we are disconnecting a1 block and validating c1 and c2 while
       validating submitted transactions. So while the block a1 is disconnected we are accepting transactions to
       the mempool resulting in having several transactions in the mempool which are in conflict with transactions
       in the disconnectpool. Transactions in conflict should be removed from mempool.

    3. We set c1 as invalid forcing that a1 becomes a tip again and sending another 2000 transactions which are
       spending outputs of the a1 block transactions. When all transactions are in the mempool we are sending
       blocks d1 and d2 triggering the reorg. During the reorg we are calling rpc generate(1) to generate a block.
       This call would fail if the mempool is in inconsistent state during the reorg.

"""


class MemepoolAcceptingTransactionsDuringReorg(BitcoinTestFramework):

    def __init__(self, *a, **kw):
        super(MemepoolAcceptingTransactionsDuringReorg, self).__init__(*a, **kw)
        self.private_key = CECKey()
        self.private_key.set_secretbytes(b"fatstacks")
        self.public_key = self.private_key.get_pubkey()

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    long_eval_script = [bytearray(b"x" * 300000), bytearray(b"y" * 290000), OP_MUL, OP_DROP]

    def create_tx(self, outpoints, noutput, feerate, make_long_eval_script=False):
        """creates p2pk transaction always using the same key (created in constructor), if make_long_eval_script is set
        we are prepending long evaluating script to the locking script
        """
        pre_script = MemepoolAcceptingTransactionsDuringReorg.long_eval_script if make_long_eval_script else []

        tx = CTransaction()
        total_input = 0
        for parent_tx, n in outpoints:
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, n), CScript([b"0"*72]), 0xffffffff))
            total_input += parent_tx.vout[n].nValue

        for _ in range(noutput):
            tx.vout.append(CTxOut(total_input//noutput, CScript(pre_script + [self.public_key, OP_CHECKSIG])))

        tx.rehash()

        tx_size = len(tx.serialize())
        fee_per_output = int(tx_size * feerate // noutput)

        for output in tx.vout:
            output.nValue -= fee_per_output

        for input,(parent_tx, n) in zip(tx.vin, outpoints):
            sighash = SignatureHashForkId(parent_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                          parent_tx.vout[n].nValue)
            input.scriptSig = CScript([self.private_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

        tx.rehash()
        return tx

    def make_block(self, txs, parent_hash, parent_height, parent_time):
        """ creates a block with given transactions"""
        block = create_block(int(parent_hash, 16),
                             coinbase=create_coinbase(pubkey=self.public_key, height=parent_height + 1),
                             nTime=parent_time + 1)
        block.vtx.extend(txs)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.calc_sha256()
        block.solve()

        return block

    def run_test(self):
        with self.run_node_with_connections("Preparation", 0,
                                            ["-minminingtxfee=0.00001",
                                             "-mindebugrejectionfee=0.000005",
                                             "-checkmempool=0"],
                                            number_of_connections=1) as (conn,):
            mining_fee = 1.1

            # create block with coinbase
            coinbase = create_coinbase(pubkey=self.public_key, height=1)
            first_block = create_block(int(conn.rpc.getbestblockhash(), 16), coinbase=coinbase)
            first_block.solve()
            conn.send_message(msg_block(first_block))
            wait_until(lambda: conn.rpc.getbestblockhash() == first_block.hash, check_interval=0.3)

            # mature the coinbase
            conn.rpc.generate(150)

            funding_tx = self.create_tx([(coinbase, 0)], 2006, mining_fee)
            conn.send_message(msg_tx(funding_tx))
            check_mempool_equals(conn.rpc, [funding_tx])

            # generates a root block with our funding transaction
            conn.rpc.generate(1)

            # create 2000 standard p2pk transactions
            a1_txs = []
            for m in range(2000):
                a1_txs.append(self.create_tx([(funding_tx, m)], 1, mining_fee))

            a1_spends = []
            for a1_tx in a1_txs:
                a1_spends.append(self.create_tx([(a1_tx, 0)], 1, mining_fee))

            # create 2000 standard p2pk transactions which are spending the same outputs as a1_txs
            double_spend_txs = []
            for m in range(2000):
                double_spend_txs.append(self.create_tx([(funding_tx, m)], 1, mining_fee))

            TX_COUNT = 8
            # create for pairs of long-evaluating transactions for blocks b1, b2, c1, and c2
            long_eval_txs = []
            for m in range(2000, 2006):
                long_eval_txs.append(self.create_tx([(funding_tx, m)], 1, 0.0001, make_long_eval_script=True))
                for _ in range(TX_COUNT - 1):
                    long_eval_txs.append(self.create_tx([(long_eval_txs[-1], 0)], 1, 0.0001, make_long_eval_script=True))

            root_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
            root_hash = root_block_info["hash"]
            root_height = root_block_info["height"]
            root_time = root_block_info["time"]

            # create all blocks needed for this test
            block_a1 = self.make_block(a1_txs, root_hash, root_height, root_time)
            block_b1 = self.make_block(long_eval_txs[0*TX_COUNT: 1*TX_COUNT], root_hash, root_height, root_time)
            block_b2 = self.make_block(long_eval_txs[1*TX_COUNT: 2*TX_COUNT], block_b1.hash, root_height+1, root_time+100)
            block_c1 = self.make_block(long_eval_txs[2*TX_COUNT: 3*TX_COUNT], root_hash, root_height, root_time)
            block_c2 = self.make_block(long_eval_txs[3*TX_COUNT: 4*TX_COUNT], block_c1.hash, root_height+1, root_time+101)
            block_d1 = self.make_block(long_eval_txs[4*TX_COUNT: 5*TX_COUNT], root_hash, root_height, root_time)
            block_d2 = self.make_block(long_eval_txs[5*TX_COUNT: 6*TX_COUNT], block_d1.hash, root_height+1, root_time+102)

            conn.send_message(msg_block(block_a1))
            wait_until(lambda: conn.rpc.getbestblockhash() == block_a1.hash, check_interval=0.3)

        with self.run_node_with_connections("1. Try sending the same transaction that are in the disconnected block during the reorg",
                                            0,
                                            ["-minminingtxfee=0.00001",
                                             "-mindebugrejectionfee=0.000005",
                                             "-maxtxsizepolicy=0",
                                             "-maxstdtxnsperthreadratio=1",
                                             "-maxnonstdtxnsperthreadratio=1",
                                             '-maxnonstdtxvalidationduration=100000',
                                             '-maxtxnvalidatorasynctasksrunduration=100001',
                                             '-genesisactivationheight=1',
                                             '-maxstackmemoryusageconsensus=2GB',
                                             "-maxscriptsizepolicy=2GB",
                                             "-acceptnonstdoutputs=1",],
                                            number_of_connections=1) as (conn,):

            # send all transactions form block a1 at once and flood the PTV
            for tx in a1_txs:
                conn.send_message(msg_tx(tx))

            # announce blocks b1, and b2 and send them triggering the reorg
            headers = msg_headers()
            headers.headers.append(block_b1)
            headers.headers.append(block_b2)
            conn.send_message(headers)

            conn.send_message(msg_block(block_b1))
            conn.send_message(msg_block(block_b2))

            # here we are having the PTV and PBV working at the same time, filling the mempool while
            # the a1 is disconnected

            # check if everything is as expected
            wait_until(lambda: conn.rpc.getbestblockhash() == block_b2.hash, timeout=60, check_interval=1)
            check_mempool_equals(conn.rpc, a1_txs)

            # now prepare for next scenario
            conn.rpc.invalidateblock(block_b1.hash)
            wait_until(lambda: conn.rpc.getbestblockhash() == block_a1.hash, check_interval=1)

            # transactions from the disconnected blocks b1 and b2 will not be added to mempool because of
            # the insufficient priority (zero fee)
            check_mempool_equals(conn.rpc, [], timeout=60, check_interval=1)

        with self.run_node_with_connections("2. Try sending transaction that are spending same inputs as transactions in the disconnected block during the reorg",
                                            0,
                                            ["-minminingtxfee=0.00001",
                                             "-mindebugrejectionfee=0.000005",
                                             "-maxtxsizepolicy=0",
                                             "-maxstdtxnsperthreadratio=1",
                                             "-maxnonstdtxnsperthreadratio=1",
                                             '-maxnonstdtxvalidationduration=100000',
                                             '-maxtxnvalidatorasynctasksrunduration=100001',
                                             '-genesisactivationheight=1',
                                             '-maxstackmemoryusageconsensus=2GB',
                                             "-maxscriptsizepolicy=2GB",
                                             "-acceptnonstdoutputs=1",],
                                            number_of_connections=1) as (conn,):

            # see if everything is still as expected
            wait_until(lambda: conn.rpc.getbestblockhash() == block_a1.hash, check_interval=1)
            check_mempool_equals(conn.rpc, [], timeout=60, check_interval=1)

            # send all transactions that are the double-spends of txs form block a1
            for double_spend_tx in double_spend_txs:
                conn.send_message(msg_tx(double_spend_tx))

            # announce and send c1, and c2
            headers = msg_headers()
            headers.headers.append(block_c1)
            headers.headers.append(block_c2)
            conn.send_message(headers)

            conn.send_message(msg_block(block_c1))
            conn.send_message(msg_block(block_c2))

            # here we are having the PTV and PBV working at the same time, filling the mempool with double-spends
            # while the a1 is disconnected

            # see if everything is as expected
            wait_until(lambda: conn.rpc.getbestblockhash() == block_c2.hash, timeout=60, check_interval=1)
            # in the mempool we want all transactions for blocks a1
            # while no double_spend_txs should be present
            check_mempool_equals(conn.rpc, a1_txs, timeout=60, check_interval=1)

            # now prepare for next scenario
            conn.rpc.invalidateblock(block_c1.hash)
            wait_until(lambda: conn.rpc.getbestblockhash() == block_a1.hash, check_interval=1)

            # transactions from the disconnected blocks c1 and c2 will not be added to mempool because of
            # the insufficient priority (zero fee)
            check_mempool_equals(conn.rpc, [], timeout=60, check_interval=1)

        with self.run_node_with_connections("3. Submit transactions that are spending ouputs from disconnecting block and try to mine a block during the reorg",
                                            0,
                                            ["-minminingtxfee=0.00001",
                                             "-mindebugrejectionfee=0.000005",
                                             "-maxtxsizepolicy=0",
                                             '-maxnonstdtxvalidationduration=200000',
                                             '-maxtxnvalidatorasynctasksrunduration=200010',
                                             '-genesisactivationheight=1',
                                             '-maxstackmemoryusageconsensus=2GB',
                                             "-maxscriptsizepolicy=2GB",
                                             "-acceptnonstdoutputs=1",],
                                            number_of_connections=1) as (conn,):

            # see if everything is still as expected
            wait_until(lambda: conn.rpc.getbestblockhash() == block_a1.hash, check_interval=1)
            check_mempool_equals(conn.rpc, [], timeout=60, check_interval=1)

            for tx in a1_spends:
                conn.send_message(msg_tx(tx))

            # send transactions that are spending outputs from the soon-to-be-disconnected block (a1)
            check_mempool_equals(conn.rpc, a1_spends, timeout=120)

            # announce blocks d1, and d2 and send them triggering the reorg
            headers = msg_headers()
            headers.headers.append(block_d1)
            headers.headers.append(block_d2)
            conn.send_message(headers)

            conn.send_message(msg_block(block_d1))
            conn.send_message(msg_block(block_d2))

            # lets give a chance for reorg to start
            sleep(0.5)

            # we are in the middle of the reorg, let try to mine a block
            # if we are in inconsistent state this call would fail
            conn.rpc.generate(1)


if __name__ == '__main__':
    MemepoolAcceptingTransactionsDuringReorg().main()
