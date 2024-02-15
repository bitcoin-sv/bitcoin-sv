#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test genesis activation gracefull period sending transactions.
The test checks 4 scenarios that can occur around genesis activation height that would normally mean the
node gets banned, but it mustn't be banned if block is inside Genesis gracefull period
1. Test (!gracefullPeriod && !isGenesis): Node is banned when it tries to send a transaction to mempool that is valid only after Genesis is activated
    (OP_ADD with big numbers) (multiple OP_ELSE is accepted into block)
2. Test (gracefullPeriod && !isGenesis): Transaction is rejected from a mempool (not banned) if it contains a script that is valid only after Genesis is activated
    (OP_ADD with big numbers) (multiple OP_ELSE is accepted into block)
3. Test (gracefullPeriod &&  isGenesis): Transaction is rejected from a mempool (not banned) if it contains a script that is valid only before Genesis is activated
    (multiple OP_ELSE) (OP_ADD is accepted into block)
4. Test (!gracefullPeriod &&  isGenesis): Node is banned when it tries to send a transaction to mempool that is valid only before Genesis is activated
    (multiple OP_ELSE) (OP_ADD is accepted into block)

Additional 2 tests are made
1. Test that checks CheckRegularTransaction method which also mustn't ban the node if too many sigops are present. This method is executed before
    CheckInputs method which is basically tested in this test
2. Test that checks that node doesn't get banned in case it exceeds policy limit set by miner, but just rejects the transaction
"""
import socket

from test_framework.test_framework import ComparisonTestFramework, wait_until
from test_framework.blocktools import create_transaction, prepare_init_chain
from test_framework.util import assert_equal
from test_framework.comptool import TestInstance
from test_framework.mininode import msg_tx, mininode_lock
from test_framework.cdefs import GENESIS_GRACEFULL_ACTIVATION_PERIOD
from test_framework.key import CECKey
from time import sleep
from test_framework.key import CECKey
from test_framework.script import (CScript, SignatureHashForkId, OP_0, OP_TRUE, OP_ADD, OP_4, OP_DROP, OP_FALSE, OP_TRUE,
                                   OP_IF, OP_ELSE, OP_ENDIF, OP_1, OP_CHECKMULTISIG, SIGHASH_ALL, SIGHASH_FORKID, OP_NOP)

_lan_ip = None


def get_lan_ip():
    global _lan_ip
    if _lan_ip: return _lan_ip
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # doesn't even have to be reachable
        s.connect(('10.255.255.255', 1))
        _lan_ip = s.getsockname()[0]
    finally:
        s.close()
    return _lan_ip


def makePubKeys(numOfKeys):
    key = CECKey()
    key.set_secretbytes(b"randombytes2")
    return [key.get_pubkey()] * numOfKeys


class BSVGenesisActivationGracefullPeriod(ComparisonTestFramework):

    def __init__(self):
        super(BSVGenesisActivationGracefullPeriod, self).__init__(get_lan_ip())

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 154 + int(GENESIS_GRACEFULL_ACTIVATION_PERIOD)
        self.extra_args = [['-genesisactivationheight=%d' % self.genesisactivationheight, '-acceptnonstdtxn', '-banscore=1', '-maxopsperscriptpolicy=1000', '-txnvalidationmaxduration=100000']]

    def run_test(self):
        self.test.run()

    def get_tests(self):

        rejected_txs = []

        def on_reject(conn, msg):
            if msg.message == b'tx':
                rejected_txs.append(msg)

        self.test.connections[0].cb.on_reject = on_reject

        # shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]
        self.chain.set_genesis_hash(int(node.getbestblockhash(), 16))

        block(0)
        yield self.accepted()

        test, out, _ = prepare_init_chain(self.chain, 150, 150)

        yield test

        # Create transaction with OP_ADD in the locking script which should be banned
        txOpAdd1 = create_transaction(out[0].tx, out[0].n, b'', 100000, CScript([b'\xFF'*4, b'\xFF'*4, OP_ADD, OP_4, OP_ADD, OP_DROP, OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpAdd1])
        # Create transaction that spends the previous transaction
        txOpAdd2 = create_transaction(txOpAdd1, 0, b'', 1, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd2))
        # wait for transaction processing
        wait_until(lambda: txOpAdd2.sha256 in [msg.data for msg in rejected_txs], timeout=5, lock=mininode_lock)

        assert_equal(len(rejected_txs), 1) # rejected
        assert_equal(rejected_txs[0].reason, b'max-script-num-length-policy-limit-violated (Script number overflow)')
        wait_until(lambda: len(self.nodes[0].listbanned()) == 1, timeout=5)  # and banned
        self.nodes[0].clearbanned()
        wait_until(lambda: len(self.nodes[0].listbanned()) == 0, timeout=5)  # and not banned
        rejected_txs = []

        # node was banned we need to restart the node
        self.restart_network()
        # TODO: This sleep needs to be replaced with a proper wait_until function
        sleep(3)
        self.test.connections[0].cb.on_reject = on_reject

        # generate a block, height is genesis gracefull height - 2
        block(1)
        # Create transaction with multiple OP_ELSE in the locking script which should be accepted to block
        txOpElse1 = create_transaction(out[1].tx, out[1].n, b'', 100000, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        #update Block with OP_ELSE transaction
        self.chain.update_block(1, [txOpElse1])
        yield self.accepted()

        # generate a block, height is genesis gracefull height - 1
        block(2)
        # Create transaction that spends the previous transaction
        txOpElse2 = create_transaction(txOpElse1, 0, b'', 1, CScript([OP_TRUE]))
        # Update block with new transactions.
        self.chain.update_block(2, [txOpElse2])
        yield self.accepted()

        assert_equal(len(rejected_txs), 0) #not rejected
        assert len(self.nodes[0].listbanned()) == 0  # and not banned

        # Create transaction with OP_ELSE in the locking script and send it to mempool which should be accepted
        txOpElse1 = create_transaction(out[2].tx, out[2].n, b'', 100000, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        self.test.connections[0].send_message(msg_tx(txOpElse1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpElse1])
        txOpElse2 = create_transaction(txOpElse1, 0, b'', 1, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpElse2))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpElse2])

        assert_equal(len(rejected_txs), 0) # not rejected
        assert len(self.nodes[0].listbanned()) == 0  # and not banned

        # generate a block to move into genesis gracefull height
        block(3)
        # Create transaction with multiple OP_ELSE in the locking script which should be accepted to block and will be spent later when we move into Genesis
        txOpElseIsSpentInGenesis1 = create_transaction(out[3].tx, out[3].n, b'', 100000, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        self.chain.update_block(3, [txOpElseIsSpentInGenesis1])
        yield self.accepted()

        # Create transaction with OP_ELSE in the locking script and send it to mempool which should be accepted
        txOpElse1 = create_transaction(out[4].tx, out[4].n, b'', 100000, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        self.test.connections[0].send_message(msg_tx(txOpElse1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpElse1])
        txOpElse2 = create_transaction(txOpElse1, 0, b'', 1, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpElse2))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpElse2])

        assert_equal(len(rejected_txs), 0) #not rejected
        assert len(self.nodes[0].listbanned()) == 0  # and not banned

        # Create transaction with OP_ADD in the locking script and send it to mempool
        # which should not be banned (but should be rejected instead), because we are in Genesis gracefull period now
        txOpAdd1 = create_transaction(out[5].tx, out[5].n, b'', 100003, CScript([b'\xFF'*4, b'\xFF'*4, OP_ADD, OP_4, OP_ADD, OP_DROP, OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpAdd1])
        # Create transaction that spends the previous transaction
        txOpAdd2 = create_transaction(txOpAdd1, 0, b'', 3, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd2))
        # wait for transaction processing

        wait_until(lambda: len(rejected_txs) > 0, timeout=5, lock=mininode_lock) #rejected
        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].reason, b'genesis-script-verify-flag-failed (Script number overflow)')
        assert len(self.nodes[0].listbanned()) == 0  # and not banned
        rejected_txs = []

        # generate a block, height is  genesis gracefull height + 1
        block(4)
        # Create transaction with OP_ELSE in the locking script which should be accepted to block
        txOpElse1 = create_transaction(out[6].tx, out[6].n, b'', 100000, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        #update Block with OP_ELSE transaction
        self.chain.update_block(4, [txOpElse1])
        yield self.accepted()

        # generate a block, height is genesis gracefull height + 2
        block(5)
        # Create transaction that spends the previous transaction
        txOpElse2 = create_transaction(txOpElse1, 0, b'', 1, CScript([OP_TRUE]))
        # Update block with new transactions.
        self.chain.update_block(5, [txOpElse2])
        yield self.accepted()

        assert_equal(len(rejected_txs), 0) # not rejected
        assert len(self.nodes[0].listbanned()) == 0  # not banned

        # Create transaction that will check if CheckRegularTransaction method inside TxnValidation method will reject instead ban the node
        txPubKeys = create_transaction(out[7].tx, out[7].n, b'', 100000, CScript(([OP_1] + makePubKeys(1) + [5000, OP_CHECKMULTISIG])*1001))
        self.test.connections[0].send_message(msg_tx(txPubKeys))
        # wait for transaction processing
        wait_until(lambda: len(rejected_txs) > 0, timeout=5, lock=mininode_lock) #rejected
        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].reason, b'flexible-bad-txn-sigops')
        assert len(self.nodes[0].listbanned()) == 0  # not banned
        rejected_txs = []

        #now we need to raise block count so we are in genesis
        height = self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height']
        for x in range(height, self.genesisactivationheight):
            block(6000 + x)
            test.blocks_and_transactions.append([self.chain.tip, True])
        yield test
        height = self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height']
        assert_equal(height, self.genesisactivationheight) # check if we are in right height

        # Create transaction with OP_ELSE in the locking script and send it to mempool
        # which should not be banned but should be rejected now
        txOpElse1 = create_transaction(out[8].tx, out[8].n, b'', 100004, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        self.test.connections[0].send_message(msg_tx(txOpElse1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpElse1])
        # Create transaction that spends the previous transaction
        txOpElse2 = create_transaction(txOpElse1, 0, b'', 4, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpElse2))
        # wait for transaction processing
        wait_until(lambda: len(rejected_txs) > 0, timeout=5, lock=mininode_lock) #rejected
        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].reason, b'genesis-script-verify-flag-failed (Invalid OP_IF construction)')
        assert len(self.nodes[0].listbanned()) == 0  # not banned
        rejected_txs = []

        # generate an empty block, height is Genesis + 1
        block(6)
        #Create transaction with OP_ADD in the locking script that should be accepted to block
        txOpAdd1 = create_transaction(out[9].tx, out[9].n, b'', 100003, CScript([b'\xFF'*4, b'\xFF'*4, OP_ADD, OP_4, OP_ADD, OP_DROP, OP_TRUE]))
        self.chain.update_block(6, [txOpAdd1])
        yield self.accepted()

        # generate an empty block, height is Genesis + 2 and make
        block(7)
        # Create transaction that spends the previous transaction
        txOpAdd2 = create_transaction(txOpAdd1, 0, b'', 3, CScript([OP_TRUE]))
        self.chain.update_block(7, [txOpAdd2])
        yield self.accepted()

        assert_equal(len(rejected_txs), 0) #not rejected
        assert len(self.nodes[0].listbanned()) == 0  # not banned

        # Create transaction with OP_ADD in the locking script and send it to mempool
        # which should not be banned (but should be rejected instead), because we are in genesis gracefull period now
        txOpAdd1 = create_transaction(out[10].tx, out[10].n, b'', 100003, CScript([b'\xFF'*4, b'\xFF'*4, OP_ADD, OP_4, OP_ADD, OP_DROP, OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpAdd1])
        # Create transaction that spends the previous transaction
        txOpAdd2 = create_transaction(txOpAdd1, 0, b'', 3, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd2))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpAdd2])

        assert_equal(len(rejected_txs), 0) # not rejected
        assert len(self.nodes[0].listbanned()) == 0  # not banned

        # Create transaction that spends the OP_ELSE transaction that was put into block before Genesis
        txOpElseIsSpentInGenesis2 = create_transaction(txOpElseIsSpentInGenesis1, 0, b'', 4, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpElseIsSpentInGenesis2))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpElseIsSpentInGenesis2])

        assert_equal(len(rejected_txs), 0) #not rejected
        assert len(self.nodes[0].listbanned()) == 0  # not banned

        # generate a block
        block(8)
        # Create transaction with multiple OP_ELSE in the locking script which should be accepted to block and will be spent later when we move into Genesis
        txOpElseIsSpentInGenesis3 = create_transaction(out[11].tx, out[11].n, b'', 100000, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        self.chain.update_block(8, [txOpElseIsSpentInGenesis3])
        yield self.accepted()

        # Create transaction that spends the OP_ELSE transaction that was put into block after Genesis
        txOpElseIsSpentInGenesis4 = create_transaction(txOpElseIsSpentInGenesis3, 0, b'', 4, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpElseIsSpentInGenesis4))
        # wait for transaction processing
        wait_until(lambda: len(rejected_txs) > 0, timeout=5, lock=mininode_lock) #rejected
        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].reason, b'genesis-script-verify-flag-failed (Invalid OP_IF construction)')
        assert len(self.nodes[0].listbanned()) == 0  # not banned
        rejected_txs = []

        height = self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height']
        genesisHeightNoGracefullPeriod = self.genesisactivationheight + int(GENESIS_GRACEFULL_ACTIVATION_PERIOD)

        #now we need to raise block count so we are in genesis but before gracefull period is over
        for x in range(height, genesisHeightNoGracefullPeriod):
            block(6000 + x)
            test.blocks_and_transactions.append([self.chain.tip, True])
        yield test
        height = self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height']
        assert_equal(height, self.genesisactivationheight + int(GENESIS_GRACEFULL_ACTIVATION_PERIOD)) # check if we are in right height

        # generate an empty block, height is Genesis + gracefull period + 1, we moved beyond gracefull period now
        block(9, spend=out[12])
        yield self.accepted()

        # generate a block, height is Genesis + gracefull period + 2
        block(10)
        #Create transaction with OP_ADD in the locking script that should be accepted to block
        txOpAdd1 = create_transaction(out[13].tx, out[13].n, b'', 100003, CScript([b'\xFF'*4, b'\xFF'*4, OP_ADD, OP_4, OP_ADD, OP_DROP, OP_TRUE]))
        self.chain.update_block(10, [txOpAdd1])
        yield self.accepted()

        # generate a block, height is Genesis + gracefull period + 3
        block(11)
        # Create transaction that spends the previous transaction
        txOpAdd2 = create_transaction(txOpAdd1, 0, b'', 3, CScript([OP_TRUE]))
        self.chain.update_block(11, [txOpAdd2])
        yield self.accepted()

        assert_equal(len(rejected_txs), 0) # accepted
        assert len(self.nodes[0].listbanned()) == 0  # not banned

        # Create transaction with OP_ADD in the locking script and send it to mempool
        # which should not be banned (but should be rejected instead), because we are in genesis gracefull period now
        txOpAdd1 = create_transaction(out[14].tx, out[14].n, b'', 100003, CScript([b'\xFF'*4, b'\xFF'*4, OP_ADD, OP_4, OP_ADD, OP_DROP, OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpAdd1])
        # Create transaction that spends the previous transaction
        txOpAdd2 = create_transaction(txOpAdd1, 0, b'', 3, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpAdd2))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpAdd2])

        assert_equal(len(rejected_txs), 0) # accepted
        assert len(self.nodes[0].listbanned()) == 0  # not banned

        # Create transaction with OP_ELSE in the locking script which should now be banned
        txOpElse1 = create_transaction(out[15].tx, out[15].n, b'', 100004, CScript([OP_FALSE, OP_IF, OP_FALSE, OP_ELSE, OP_TRUE, OP_ELSE, OP_FALSE, OP_ENDIF]))
        self.test.connections[0].send_message(msg_tx(txOpElse1))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txOpElse1])
        # Create transaction that spends the previous transaction
        txOpElse2 = create_transaction(txOpElse1, 0, b'', 4, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txOpElse2))
        # wait for transaction processing
        wait_until(lambda: len(rejected_txs) > 0, timeout=5, lock=mininode_lock) #rejected
        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].reason, b'mandatory-script-verify-flag-failed (Invalid OP_IF construction)')
        wait_until(lambda: len(self.nodes[0].listbanned()) == 1, timeout=5) # banned
        rejected_txs = []

        self.nodes[0].clearbanned()
        wait_until(lambda: len(self.nodes[0].listbanned()) == 0, timeout=5) # and not banned
        rejected_txs = []
        # node was banned we need to restart the node
        self.restart_network()
        # TODO: This sleep needs to be replaced with a proper wait_until function
        sleep(3)
        self.test.connections[0].cb.on_reject = on_reject

        # Create transaction with OP_NOP that exceeds policy limits to check that node does not get banned for exceeding our policy limit
        txPubKeys = create_transaction(out[16].tx, out[16].n, b'', 100004, CScript([OP_TRUE] + [OP_NOP] * 1001))
        self.test.connections[0].send_message(msg_tx(txPubKeys))
        # wait for transaction processing
        self.check_mempool(self.test.connections[0].rpc, [txPubKeys])
        # Create transaction that spends the previous transaction
        txPubKeys2 = create_transaction(txPubKeys, 0, b'', 4, CScript([OP_TRUE]))
        self.test.connections[0].send_message(msg_tx(txPubKeys2))
        # wait for transaction processing
        wait_until(lambda: len(rejected_txs) > 0, timeout=5, lock=mininode_lock)  # rejected
        assert_equal(len(rejected_txs), 1)
        assert_equal(rejected_txs[0].reason, b'non-mandatory-script-verify-flag (Operation limit exceeded)')
        assert len(self.nodes[0].listbanned()) == 0  # banned


if __name__ == '__main__':
    BSVGenesisActivationGracefullPeriod().main()
