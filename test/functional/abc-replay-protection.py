#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
This test checks activation of UAHF and the different consensus
related to this activation.
It is derived from the much more complex p2p-fullblocktest.
"""

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.comptool import TestManager, TestInstance, RejectResult
from test_framework.blocktools import *
import time
from test_framework.key import CECKey
from test_framework.script import *

# far into the future
REPLAY_PROTECTION_START_TIME = 2000000000

# Error due to invalid signature
INVALID_SIGNATURE_ERROR = b'mandatory-script-verify-flag-failed (Signature must be zero for failed CHECK(MULTI)SIG operation)'
RPC_INVALID_SIGNATURE_ERROR = "16: " + \
    INVALID_SIGNATURE_ERROR.decode("utf-8")

class ReplayProtectionTest(ComparisonTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-whitelist=127.0.0.1',
                            "-replayprotectionactivationtime=%d" % REPLAY_PROTECTION_START_TIME]]

    def run_test(self):
        self.nodes[0].setmocktime(REPLAY_PROTECTION_START_TIME)
        self.test.run()

    def get_tests(self):
        self.chain.set_genesis_hash( int(self.nodes[0].getbestblockhash(), 16) )

        # shorthand for functions
        block = self.chain.next_block
        node = self.nodes[0]

        # Create a new block
        block(0)
        self.chain.save_spendable_output()
        yield self.accepted()

        # Now we need that block to mature so we can spend the coinbase.
        test = TestInstance(sync_every_block=False)
        for i in range(99):
            block(5000 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
            self.chain.save_spendable_output()
        yield test

        # collect spendable outputs now to avoid cluttering the code later on
        out = []
        for i in range(100):
            out.append(self.chain.get_spendable_output())

        # Generate a key pair to test P2SH sigops count
        private_key = CECKey()
        private_key.set_secretbytes(b"replayprotection")
        public_key = private_key.get_pubkey()

        # This is a little handier to use than the version in blocktools.py
        def create_fund_and_spend_tx(spend, forkvalue=0):
            # Fund transaction
            script = CScript([public_key, OP_CHECKSIG])
            txfund = create_transaction(
                spend.tx, spend.n, b'', 50 * COIN, script)
            txfund.rehash()

            # Spend transaction
            txspend = CTransaction()
            txspend.vout.append(CTxOut(50 * COIN - 1000, CScript([OP_TRUE])))
            txspend.vin.append(CTxIn(COutPoint(txfund.sha256, 0), b''))

            # Sign the transaction
            sighashtype = (forkvalue << 8) | SIGHASH_ALL | SIGHASH_FORKID
            sighash = SignatureHashForkId(
                script, txspend, 0, sighashtype, 50 * COIN)
            sig = private_key.sign(sighash) + \
                bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
            txspend.vin[0].scriptSig = CScript([sig])
            txspend.rehash()

            return [txfund, txspend]

        def send_transaction_to_mempool(tx):
            tx_id = node.sendrawtransaction(ToHex(tx))
            assert(tx_id in set(node.getrawmempool()))
            return tx_id

        # Before the fork, no replay protection required to get in the mempool.
        txns = create_fund_and_spend_tx(out[0])
        send_transaction_to_mempool(txns[0])
        send_transaction_to_mempool(txns[1])

        # And txns get mined in a block properly.
        block(1)
        self.chain.update_block(1, txns)
        yield self.accepted()

        # Replay protected transactions are rejected.
        replay_txns = create_fund_and_spend_tx(out[1], 0xffdead)
        send_transaction_to_mempool(replay_txns[0])
        assert_raises_rpc_error(-26, RPC_INVALID_SIGNATURE_ERROR,
                                node.sendrawtransaction, ToHex(replay_txns[1]))

        # And block containing them are rejected as well.
        block(2)
        self.chain.update_block(2, replay_txns)
        yield self.rejected(RejectResult(16, b'blk-bad-inputs'))

        # Rewind bad block
        self.chain.set_tip(1)

        # Create a block that would activate the replay protection.
        bfork = block(5555)
        bfork.nTime = REPLAY_PROTECTION_START_TIME - 1
        self.chain.update_block(5555, [])
        yield self.accepted()

        for i in range(5):
            block(5100 + i)
            test.blocks_and_transactions.append([self.chain.tip, True])
        yield test

        # Check we are just before the activation time
        assert_equal(node.getblockheader(node.getbestblockhash())['mediantime'],
                     REPLAY_PROTECTION_START_TIME - 1)

        # We are just before the fork, replay protected txns still are rejected
        assert_raises_rpc_error(-26, RPC_INVALID_SIGNATURE_ERROR,
                                node.sendrawtransaction, ToHex(replay_txns[1]))

        block(3)
        self.chain.update_block(3, replay_txns)
        yield self.rejected(RejectResult(16, b'blk-bad-inputs'))

        # Rewind bad block
        self.chain.set_tip(5104)

        # Send some non replay protected txns in the mempool to check
        # they get cleaned at activation.
        txns = create_fund_and_spend_tx(out[2])
        send_transaction_to_mempool(txns[0])
        tx_id = send_transaction_to_mempool(txns[1])
        assert(tx_id in set(node.getrawmempool()))

        # Activate the replay protection
        block(5556)
        yield self.accepted()

        # At activation the entire mempool is cleared, so the txn we inserted
        # earlier will have gone.
        assert(tx_id not in set(node.getrawmempool()))

        # Good old transactions are still valid
        tx_id = send_transaction_to_mempool(txns[0])
        assert(tx_id in set(node.getrawmempool()))

        # They also can still be mined
        block(4)
        self.chain.update_block(4, txns)
        yield self.accepted()

        # The replay protected transaction is still invalid
        send_transaction_to_mempool(replay_txns[0])
        assert_raises_rpc_error(-26, RPC_INVALID_SIGNATURE_ERROR,
                                node.sendrawtransaction, ToHex(replay_txns[1]))

        # They also still can't be mined
        b5 = block(5)
        self.chain.update_block(5, replay_txns)
        yield self.rejected(RejectResult(16, b'blk-bad-inputs'))

        # Rewind bad block
        self.chain.set_tip(5556)

        # These next few tests look a bit pointless to me since over the activation
        # we completely wipe the mempool, but hey-ho I guess they're only
        # temporary.

        # Ok, now we check if a reorg work properly accross the activation.
        postforkblockid = node.getbestblockhash()
        node.invalidateblock(postforkblockid)
        assert(tx_id in set(node.getrawmempool()))

        # Deactivating replay protection.
        forkblockid = node.getbestblockhash()
        node.invalidateblock(forkblockid)
        assert(tx_id not in set(node.getrawmempool()))

        # Check that we also do it properly on deeper reorg.
        node.reconsiderblock(forkblockid)
        node.reconsiderblock(postforkblockid)
        node.invalidateblock(forkblockid)
        assert(tx_id not in set(node.getrawmempool()))


if __name__ == '__main__':
    ReplayProtectionTest().main()
