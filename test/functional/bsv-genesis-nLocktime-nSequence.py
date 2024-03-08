#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test restoring the original meaning of nLockTime & nSequence
Scenario:
Genesis height is 600.

1. At height 582, create a transaction with nLockTime in the past & nSequence set to prevent
   mining until height 584. The transaction should be rejected because genesis has not yet
   activated and the transaction is not yet minable.

2. At height 582, create a transaction with nLockTime in the past & nSequence set to prevent
   mining until height 583. The transaction should be accepted because genesis has not yet
   activated but the transaction is minable in the next block. Call this txn1.

3. Move height on to 583, txn1 is mined and removed from mempool.

4. Check that BIP68 is activated and that a block with a BIP68 non-final transaction will
   be rejected.

5. Move height on to block 600; Genesis is activated.

6. Move height on to 601 (to create some more spendable outputs post-genesis).

7. Create txn with empty vin and verify it is rejected with the expected error code
   (Check for failed test CORE-430)

8. Create a transaction with nLockTime in the past & nSequence set to the value
   that would have (pre-genesis) meant minable at height 604. The transaction should be accepted
   because this is now considered final. Call this txn2.

9. Create 3 transactions with nLockTime in the future & nSequence 0x00000003.
   They should be accepted into the non-final-mempool but not the main mempool.
   Call these txn3, txn4 & txn5.

10. Create a transaction with nLockTime in the future & nSequence equal to 0xFFFFFFFF.
    The transaction will be considered final, accepted into the main mempool.
    Call this txn6.

11. Create transaction that tries to double spend UTXOs locked by txn3. It will be discarded
    (actually it will be rejected but with code txn-mempool-conflict which is internal and so
    doesn't send message back to client).

12. Move height on. Txn2 and Txn6 will be mined.

13. Create standard transaction which tries to spend non-final txn3. It will be an orphan.

14. Create non-final transaction which tries to spend non-final txn3. It will be rejected.

15. Send an update for txn3 with a lower nSequence number. It will be rejected.

16. Send an update for txn3 with a higher nSequence number. It will be accepted.

17. Send multiple updates together for txn3 with higher and different nSequence numbers.
    Just one of the updates will be accepted, but due to PTV we can't be sure which.

18. Send an invalid update in a single txn which wants to update both txn4 and txn5.
    It will be rejected.

19. Send an invalid update for txn4 which changes the number of inputs.
    It will be rejected.

20. Send an update for txn3 with nSequence = 0xFFFFFFFF. It will be finalised and moved
    into the main mempool.

21. Move time on beyond the nLockTime for txn4. It will be finalised and moved into the
    main mempool.

22. Move time beyond expiry period for txn5. It will be purged.

23. Mine a time-locked txn with MTP valid for the block it is contained in. Put a txn in the
    mempool that spends that first txn. Reorg so that the first txn is again non-final for
    the new MTP. Both txns will be removed from the mempool.

24. Create and send non-final txn. Send a block that contains a transaction that spends the
    some outputs as the earlier non-final txn. The non-final txn will not make it into the
    main mempool.

25. Check that post-genesis if we receive a block containing a txn that pre-genesis would
    have been BIP68 non-final but now is final, we accept that block ok.

26. Check that post-genesis we will not accept a block containing a non-final transaction

27. Check that updates to non-final transactions above the configured maximum rate are
    rejected.

"""
from test_framework.test_framework import ComparisonTestFramework
from test_framework.script import CScript, OP_TRUE
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal, p2p_port, wait_until, check_for_log_msg
from test_framework.comptool import TestManager, TestInstance, TestNode, RejectResult, DiscardResult
from test_framework.mininode import NodeConn, NodeConnCB, NetworkThread, msg_getdata, msg_tx, CInv, mininode_lock
import time
import copy


class MyNode(TestNode):
    def __init__(self, block_store, tx_store, blocks_recvd):
        super().__init__(block_store, tx_store)
        self.blocks_recvd = blocks_recvd

    def on_block(self, conn, message):
        super().on_block(conn, message)

        block = message.block
        block.rehash()
        self.blocks_recvd[block.sha256] = block

    def on_inv(self, conn, message):
        super().on_inv(conn, message)

        for x in message.inv:
            if x.type == CInv.TX:
                # Remember txn
                pass
            elif x.type == CInv.BLOCK:
                # Request block
                self.conn.send_message(msg_getdata([CInv(x.type, x.hash)]))


class MyTestManager(TestManager):
    def __init__(self, testgen, datadir):
        super().__init__(testgen, datadir)
        self.blocks_recvd = {}

    def add_all_connections(self, nodes):
        for i in range(len(nodes)):
            # Create a p2p connection to each node
            test_node = MyNode(self.block_store, self.tx_store, self.blocks_recvd)
            self.test_nodes.append(test_node)
            self.connections.append(NodeConn('127.0.0.1', p2p_port(i), nodes[i], test_node))
            # Make sure the TestNode (callback class) has a reference to its associated NodeConn
            test_node.add_connection(self.connections[-1])


class BSVGenesis_Restore_nLockTime_nSequence(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 600
        self.extra_args = [['-debug', '-whitelist=127.0.0.1', '-genesisactivationheight=%d' % self.genesisactivationheight,
                            '-txnpropagationfreq=1', '-txnvalidationasynchrunfreq=1', '-checknonfinalfreq=100',
                            '-mempoolexpirynonfinal=1', '-maxgenesisgracefulperiod=0',
                            '-mempoolnonfinalmaxreplacementrate=5',
                            '-mempoolnonfinalmaxreplacementrateperiod=1']] * self.num_nodes
        self.start_time = int(time.time())

    def init_network(self):
        # Start creating test manager which help to manage test cases
        self.test = MyTestManager(self, self.options.tmpdir)
        # (Re)start network
        self.restart_network()

    def run_test(self):
        self.test.run()

    def create_transaction(self, prevtx, n, sig, value, scriptPubKey):
        tx = create_transaction(prevtx, n, sig, value, scriptPubKey)
        tx.nVersion = 2
        tx.rehash()
        return tx

    def create_locked_transaction(self, prevtx, n, sig, value, scriptPubKey, lockTime, sequence):
        tx = self.create_transaction(prevtx, n, sig, value, scriptPubKey)
        tx.nLockTime = lockTime
        tx.vin[0].nSequence = sequence
        tx.rehash()
        return tx

    def make_sequence(self, n):
        # Bits 31 and 22 must be cleared to enable nSequence relative lock time meaning pre-genesis
        n = n - self.nodes[0].getblockcount()
        sequence = n & 0x0000FFFF
        return sequence

    # Get tip from bitcoind and allow us to build on it
    def build_on_tip(self, n):
        tip_hash = int(self.nodes[0].getbestblockhash(), 16)
        tip_height = self.nodes[0].getblockcount()
        wait_until(lambda: tip_hash in self.test.blocks_recvd, lock=mininode_lock)
        self.chain.block_heights[tip_hash] = tip_height
        self.chain.blocks[n] = self.test.blocks_recvd[tip_hash]
        self.chain.set_tip(n)

    def get_tests(self):
        # Shorthand for functions
        block = self.chain.next_block

        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 580, 200)

        yield test

        # Create block on height 581 with some transactions for us to spend
        nLockTime = self.start_time
        block(1, spend=out[0])
        spend_tx1 = self.create_transaction(out[1].tx, out[1].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx2 = self.create_transaction(out[2].tx, out[2].n, CScript(), 100000, CScript([OP_TRUE]))
        self.chain.update_block(1, [spend_tx1, spend_tx2])
        yield self.accepted()

        # At height 582, create a transaction with nSequence set to prevent mining until height 584
        rej_tx = self.create_locked_transaction(spend_tx1, 0, CScript(), 1, CScript([OP_TRUE]), nLockTime, self.make_sequence(584))
        # The transaction should be rejected because genesis has not yet activated and the transaction is not yet minable
        yield TestInstance([[rej_tx, RejectResult(64, b'non-BIP68-final')]])

        # At height 582, create a transaction with nSequence set to prevent mining until height 583.
        tx1 = self.create_locked_transaction(spend_tx1, 0, CScript(), 1, CScript([OP_TRUE]), nLockTime, self.make_sequence(583))
        # The transaction should be accepted because genesis has not yet activated but the transaction is minable in the next block
        yield TestInstance([[tx1, True]])
        assert(tx1.hash in self.nodes[0].getrawmempool())

        # Move height on to 583, tx1 is mined and removed from mempool
        self.nodes[0].generate(1)
        assert(tx1.hash not in self.nodes[0].getrawmempool())

        # Create block with bip68 non-final txn in it. It will be rejected.
        self.build_on_tip(9)
        block(10, spend=out[5])
        bip68_non_final_tx = self.create_locked_transaction(spend_tx2, 0, CScript(), 1, CScript([OP_TRUE]), nLockTime, self.make_sequence(587))
        self.chain.update_block(10, [bip68_non_final_tx])
        yield self.rejected(RejectResult(16, b'bad-txns-nonfinal'))

        # Move height on to block 600; genesis is activated.
        self.nodes[0].generate(self.genesisactivationheight - self.nodes[0].getblockcount())
        self.log.info("Genesis activated, height {}".format(self.nodes[0].getblockcount()))

        # Get tip from bitcoind because it's further along than we are, and we want to build on it
        self.build_on_tip(2)

        # Create block on height 601 with some more transactions for us to spend
        nLockTime = self.start_time
        block(3, spend=out[10])
        spend_tx2 = self.create_transaction(out[12].tx, out[12].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx3 = self.create_transaction(out[13].tx, out[13].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx4 = self.create_transaction(out[14].tx, out[14].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx5 = self.create_transaction(out[15].tx, out[15].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx6 = self.create_transaction(out[16].tx, out[16].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx7 = self.create_transaction(out[17].tx, out[17].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx8 = self.create_transaction(out[18].tx, out[18].n, CScript(), 100000, CScript([OP_TRUE]))
        self.chain.update_block(3, [spend_tx2, spend_tx3, spend_tx4, spend_tx5, spend_tx6])
        yield self.accepted()

        # Check txn with empty vin is rejected with the expected code (fix for CORE-430).
        tx2 = self.create_locked_transaction(spend_tx2, 0, CScript(), 1000, CScript([OP_TRUE]), nLockTime, 0x00000001)
        tx2.vin = []
        tx2.rehash()
        yield TestInstance([[tx2, RejectResult(16, b'bad-txns-vin-empty')]])

        # At height 601, create a transaction with nLockTime in the past & nSequence set to the value
        # that would have (pre-genesis) meant minable at height 605.
        tx2 = self.create_locked_transaction(spend_tx2, 0, CScript(), 1000, CScript([OP_TRUE]), nLockTime, self.make_sequence(605))
        # The transaction should be accepted because this is now considered final.
        yield TestInstance([[tx2, True]])
        mempool = self.nodes[0].getrawmempool()
        assert(tx2.hash in mempool)

        # At height 601, create a transaction with nLockTime in the future & nSequence not 0xFFFFFFFF.
        nLockTime = int(time.time()) + 1000
        tx3 = self.create_locked_transaction(spend_tx3, 0, CScript(), 1000, CScript([OP_TRUE]), nLockTime, 0x00000003)
        tx4 = self.create_locked_transaction(spend_tx4, 0, CScript(), 1000, CScript([OP_TRUE]), nLockTime, 0x00000003)
        tx5 = self.create_locked_transaction(spend_tx5, 0, CScript(), 1000, CScript([OP_TRUE]), nLockTime + 30, 0x00000003)
        # The transactions should be accepted into the non-final-mempool but not the main mempool.
        yield TestInstance([[tx3, DiscardResult()], [tx4, DiscardResult()], [tx5, DiscardResult()]])
        mempool = self.nodes[0].getrawmempool()
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx3.hash not in mempool)
        assert(tx3.hash in nonfinalmempool)
        assert(tx4.hash not in mempool)
        assert(tx4.hash in nonfinalmempool)
        assert(tx5.hash not in mempool)
        assert(tx5.hash in nonfinalmempool)

        # At height 601, create a transaction with nLockTime in the future & nSequence equal to 0xFFFFFFFF.
        tx6 = self.create_locked_transaction(spend_tx6, 0, CScript(), 1000, CScript([OP_TRUE]), nLockTime, 0xFFFFFFFF)
        # The transaction will be considered final and accepted into the main mempool.
        yield TestInstance([[tx6, True]])
        mempool = self.nodes[0].getrawmempool()
        assert(tx6.hash in mempool)

        # Create transaction that tries to double spend UTXOs locked by txn3. It will be discarded.
        tx3_double_spend = self.create_transaction(spend_tx3, 0, CScript(), 1000, CScript([OP_TRUE]))
        tx3_double_spend.vin.append(spend_tx7.vin[0])
        yield TestInstance([[tx3_double_spend, DiscardResult()]])

        # Move height on to 602. Txn2 and Txn6 will be mined and removed from mempool, txn3, 4 & 5 will not have been moved there.
        self.nodes[0].generate(1)
        mempool = self.nodes[0].getrawmempool()
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx2.hash not in mempool)
        assert(tx3.hash not in mempool)
        assert(tx4.hash not in mempool)
        assert(tx5.hash not in mempool)
        assert(tx6.hash not in mempool)
        assert(tx3.hash in nonfinalmempool)
        assert(tx4.hash in nonfinalmempool)
        assert(tx5.hash in nonfinalmempool)

        # Create standard transaction which tries to spend non-final txn3. It will be an orphan.
        txn_spend = self.create_transaction(tx3, 0, CScript(), 100, CScript([OP_TRUE]))
        yield TestInstance([[txn_spend, DiscardResult()]])

        # Create non-final transaction which tries to spend non-final txn3. It will be rejected.
        txn_spend = self.create_locked_transaction(tx3, 0, CScript(), 100, CScript([OP_TRUE]), nLockTime, 0x00000003)
        yield TestInstance([[txn_spend, RejectResult(64, b'too-long-non-final-chain')]])

        # Send update to txn3 with lower nSequence. It will be rejected.
        tx3.vin[0].nSequence -= 1
        tx3.rehash()
        yield TestInstance([[tx3, RejectResult(16, b'bad-txn-update')]])

        # Send update to txn3 with higher nSequence. It will be accepted.
        tx3.vin[0].nSequence += 2
        tx3.rehash()
        yield TestInstance([[tx3, DiscardResult()]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert_equal(len(nonfinalmempool), 3)
        assert(tx3.hash in nonfinalmempool)
        assert(tx4.hash in nonfinalmempool)
        assert(tx5.hash in nonfinalmempool)

        # Send multiple updates together for txn3 with higher and different nSequence numbers.
        # Just one of the updates will be accepted, but due to PTV we can't be sure which.
        tx3_update1 = copy.deepcopy(tx3)
        tx3_update2 = copy.deepcopy(tx3)
        tx3_update3 = copy.deepcopy(tx3)
        tx3_update1.vin[0].nSequence += 1
        tx3_update2.vin[0].nSequence += 3   # update2 has the highest nSequence
        tx3_update3.vin[0].nSequence += 2
        tx3_update1.rehash()
        tx3_update2.rehash()
        tx3_update3.rehash()
        updateHashes = {tx3_update1.hash, tx3_update2.hash, tx3_update3.hash}
        yield TestInstance([[tx3_update1, None], [tx3_update2, None], [tx3_update3, None]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert_equal(len(nonfinalmempool), 3)
        assert(bool(tx3_update1.hash in nonfinalmempool) ^ bool(tx3_update2.hash in nonfinalmempool) ^ bool(tx3_update3.hash in nonfinalmempool))
        assert(tx4.hash in nonfinalmempool)
        assert(tx5.hash in nonfinalmempool)

        # Remember which update got accepted
        tx3_update_accepted = None
        for update in updateHashes:
            if update in nonfinalmempool:
                tx3_update_accepted = update
                break

        # Send an invalid update in a single txn which wants to update both txn4 and txn5. It will be rejected.
        tx4_tx5_invalid = copy.deepcopy(tx4)
        tx4_tx5_invalid.vin.append(tx5.vin[0])
        tx4_tx5_invalid.vin[0].nSequence += 1
        tx4_tx5_invalid.vin[1].nSequence += 1
        tx4_tx5_invalid.rehash()
        yield TestInstance([[tx4_tx5_invalid, RejectResult(16, b'bad-txn-update')]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert_equal(len(nonfinalmempool), 3)
        assert(tx4.hash in nonfinalmempool)
        assert(tx5.hash in nonfinalmempool)

        # Send an invalid update for txn4 which changes the number of inputs. It will be rejected.
        tx4_invalid = copy.deepcopy(tx4)
        tx4_invalid.vin.append(spend_tx8.vin[0])
        tx4_invalid.vin[0].nSequence += 1
        tx4_invalid.rehash()
        yield TestInstance([[tx4_invalid, RejectResult(16, b'bad-txn-update')]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert_equal(len(nonfinalmempool), 3)
        assert(tx4.hash in nonfinalmempool)

        # Send an update for txn3 with nSequence = 0xFFFFFFFF. It will be finalised and moved
        # into the main mempool.
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx3_update_accepted in nonfinalmempool)
        tx3.vin[0].nSequence = 0xFFFFFFFF
        tx3.rehash()
        yield TestInstance([[tx3, True]])
        mempool = self.nodes[0].getrawmempool()
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx3.hash in mempool)
        assert(tx3.hash not in nonfinalmempool)
        assert(tx3_update_accepted not in nonfinalmempool)

        # Move time on beyond the nLockTime for txn4. It will be finalised and moved into the
        # main mempool.
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx4.hash in nonfinalmempool)
        assert(tx5.hash in nonfinalmempool)
        self.nodes[0].setmocktime(nLockTime + 1)
        self.nodes[0].generate(6)
        wait_until(lambda: tx4.hash in self.nodes[0].getrawmempool(), timeout=5)
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx4.hash not in nonfinalmempool)
        assert(tx5.hash in nonfinalmempool)

        # Get tip from bitcoind because it's further along than we are, and we want to build on it
        self.build_on_tip(4)

        # Create block with some more transactions for us to spend
        block(5, spend=out[20])
        spend_tx11 = self.create_transaction(out[21].tx, out[21].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx12 = self.create_transaction(out[22].tx, out[22].n, CScript(), 100000, CScript([OP_TRUE]))
        spend_tx13 = self.create_transaction(out[23].tx, out[23].n, CScript(), 100000, CScript([OP_TRUE]))
        self.chain.update_block(5, [spend_tx11, spend_tx12, spend_tx13])
        yield self.accepted()

        # Send txn time-locked for the current block height so that it can be immediately mined
        self.nodes[0].generate(1)
        currentHeight = self.nodes[0].getblockcount()
        tx7 = self.create_locked_transaction(spend_tx11, 0, CScript(), 1000, CScript([OP_TRUE]), currentHeight, 0x01)
        yield TestInstance([[tx7, True]])
        # Mine it & spend it
        self.nodes[0].generate(1)
        tx7_spend = self.create_transaction(tx7, 0, CScript(), 100, CScript([OP_TRUE]))
        yield TestInstance([[tx7_spend, True]])
        mempool = self.nodes[0].getrawmempool()
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx7.hash not in nonfinalmempool)
        assert(tx7.hash not in mempool)
        assert(tx7_spend.hash not in nonfinalmempool)
        assert(tx7_spend.hash in mempool)
        # Reorg back so tx7 is no longer final
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        # Both txns will have been removed from the mempool
        mempool = self.nodes[0].getrawmempool()
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx7.hash not in nonfinalmempool)
        assert(tx7.hash not in mempool)
        assert(tx7_spend.hash not in nonfinalmempool)
        assert(tx7_spend.hash not in mempool)

        # Send non-final txn
        currentHeight = self.nodes[0].getblockcount()
        tx7 = self.create_locked_transaction(spend_tx11, 0, CScript(), 1000, CScript([OP_TRUE]), currentHeight + 1, 0x01)
        yield TestInstance([[tx7, DiscardResult()]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        mempool = self.nodes[0].getrawmempool()
        assert(tx7.hash in nonfinalmempool)
        # Create a block that contains a txn that conflicts with the previous non-final txn
        tx7_double_spend = self.create_transaction(spend_tx11, 0, CScript(), 1000, CScript([OP_TRUE]))
        block(6, spend=out[30])
        self.chain.update_block(6, [tx7_double_spend])
        yield self.accepted()
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        mempool = self.nodes[0].getrawmempool()
        assert(tx7.hash not in nonfinalmempool)
        assert(tx7.hash not in mempool)

        # Reorg back and check that the transaction from the block is the txn picked to be kept in the mempool
        mempool = self.nodes[0].getrawmempool()
        assert(tx7_double_spend.hash not in mempool)
        assert(tx7.hash not in mempool)
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        mempool = self.nodes[0].getrawmempool()
        assert(tx7_double_spend.hash in mempool)
        assert(tx7.hash not in mempool)

        # Check that post-genesis if we receive a block containing a txn that pre-genesis would have been BIP68
        # non-final but now is final, we accept that block ok.
        self.nodes[0].generate(1)
        self.build_on_tip(11)
        currentHeight = self.nodes[0].getblockcount()
        block(12, spend=out[31])
        bip68_non_final_tx = self.create_locked_transaction(spend_tx12, 0, CScript(), 1, CScript([OP_TRUE]), 0, self.make_sequence(currentHeight + 10))
        self.chain.update_block(12, [bip68_non_final_tx])
        yield self.accepted()

        # Check that post-genesis we will not accept a block containing a non-final transaction
        currentHeight = self.nodes[0].getblockcount()
        block(13, spend=out[32])
        non_final_tx = self.create_locked_transaction(out[32].tx, out[32].n, CScript(), 1, CScript([OP_TRUE]), currentHeight + 1, 0x01)
        self.chain.update_block(13, [non_final_tx])
        yield self.rejected(RejectResult(16, b'bad-txns-nonfinal'))

        # Send non-final txn that we will use to test rate limiting of updates to it
        currentHeight = self.nodes[0].getblockcount()
        tx8 = self.create_locked_transaction(spend_tx13, 0, CScript(), 1000, CScript([OP_TRUE]), currentHeight + 1, 0x01)
        yield TestInstance([[tx8, DiscardResult()]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx8.hash in nonfinalmempool)
        # Send updates within the permitted rate
        for i in range(5):
            tx8.vin[0].nSequence += 1
            tx8.rehash()
            yield TestInstance([[tx8, DiscardResult()]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx8.hash in nonfinalmempool)
        # Send another update to take us above the permitted rate
        tx8.vin[0].nSequence += 1
        tx8.rehash()
        yield TestInstance([[tx8, RejectResult(69, b'non-final-txn-replacement-rate')]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx8.hash not in nonfinalmempool)
        assert(not check_for_log_msg(self, "Non-final txn that exceeds replacement rate made it to validation", "/node0"))
        # Wait and retry, should now be accepted
        time.sleep(12)
        tx8.vin[0].nSequence += 1
        tx8.rehash()
        yield TestInstance([[tx8, DiscardResult()]])
        nonfinalmempool = self.nodes[0].getrawnonfinalmempool()
        assert(tx8.hash in nonfinalmempool)


if __name__ == '__main__':
    BSVGenesis_Restore_nLockTime_nSequence().main()
