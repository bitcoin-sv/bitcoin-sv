#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Test confiscation transactions

_check_invalid_confiscation_transactios:
    test detection of confiscation transactions with invalid contents

_check_confiscation:
    test confiscating TXOs and spending confiscated TXOs

_check_mempool_removal:
    test removal of confiscation transactions that have become invalid from mempool

See comments and log entries in above functions for detailed description of steps performed in each test.
"""

from test_framework.blocktools import make_block, create_transaction, PreviousSpendableOutput
from test_framework.key import CECKey
from test_framework.mininode import (
    COIN,
    COutPoint,
    NetworkThread,
    NodeConn,
    NodeConnCB,
    msg_block,
    msg_tx,
    ToHex,
    CTxOut
)
from test_framework.script import (
    CScript,
    hash160,
    OP_1,
    OP_CHECKSIG,
    OP_CHECKMULTISIG,
    OP_DUP,
    OP_IF,
    OP_EQUAL,
    OP_EQUALVERIFY,
    OP_FALSE,
    OP_HASH160,
    OP_NOP,
    OP_PUSHDATA1,
    OP_RETURN,
    OP_TRUE,
    CScriptOp
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    check_for_log_msg,
    p2p_port,
    wait_until
)
import glob
import re
import time

'''
> Tested rejection reasons
bad-ctx-invalid
bad-ctx-not-whitelisted
bad-txns-premature-spend-of-confiscation
bad-txns-inputs-missingorspent
flexible-bad-txns-vout-p2sh
bad-txns-vout-p2sh
bad-txns-in-belowout
bad-txns-inputs-duplicate
bad-txns-vout-negative
bad-txns-txouttotal-toolarge
bad-txns-vout-toolarge
bad-txns-nonfinal
bad-txns-premature-spend-of-coinbase
bad-txns-vin-empty
absurdly-high-fee
tx-size
txn-mempool-conflict
scriptsig-not-pushonly
bare-multisig
dust
scriptpubkey
datacarrier-size-exceeded

! Not tested rejection reasons
bad-txns-inputvalues-outofrange (such coin(s) doesn't exist)
bad-txns-prevout-null (bad-txns-inputs-missingorspent happens first)
bad-txns-fee-negative (bad-txns-in-belowout happens first)
bad-txns-fee-outofrange (fees cannot be more than all coins)
bad-txns-vout-empty (ctfx protocol is encoded in first output thus it's not a confiscation tx)
bad-txns-oversize (too big transaction at consensus)
bad-txn-sigops (before genesis)
txn-already-known (it's valid tx with already known outputs)
non-final-pool-full (confiscation tx cannot be non-final)
too-long-non-final-chain (confiscation tx cannot be non-final)
non-BIP68-final (confiscation tx cannot be non-final)
too-long-validation-time (script checks are skipped for confiscation tx)
too-long-mempool-chain (confiscation tx shouldn't have unconfirmed parents)
'''

# OP_RETURN protocol with confiscation transaction protocol id
CTX_OP_RETURN = [OP_FALSE, OP_RETURN, b'cftx']


class FrozenTXOConfiscation(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.chain = ChainManager()
        self.extra_args = [["-whitelist=127.0.0.1",
                            "-minrelaytxfee=0",
                            "-minminingtxfee=0",
                            "-limitfreerelay=999999",
                            "-genesisactivationheight=100",
                            "-maxtxsigopscountspolicy=1MB",
                            "-permitbaremultisig=0"]]*2
        self.block_count = 0

    def _init(self):
        # Private key used in scripts with CHECKSIG
        self.prvkey = CECKey()
        self.prvkey.set_secretbytes(b"horsebattery")
        self.pubkey = self.prvkey.get_pubkey()

        # Create a P2P connections
        node = NodeConnCB()
        connection = NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], node)
        node.add_connection(connection)
        node.rpc = connection.rpc

        node1 = NodeConnCB()
        connection1 = NodeConn('127.0.0.1', p2p_port(1), self.nodes[1], node1)
        node1.add_connection(connection1)
        node1.rpc = self.nodes[1]

        NetworkThread().start()
        # wait_for_verack ensures that the P2P connection is fully up.
        node.wait_for_verack()
        node1.wait_for_verack()

        self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
        block = self.chain.next_block(self.block_count)
        self.block_count += 1
        self.chain.save_spendable_output()
        node.send_message(msg_block(block))

        for i in range(100):
            block = self.chain.next_block(self.block_count)
            self.block_count += 1
            self.chain.save_spendable_output()
            node.send_message(msg_block(block))

        self.log.info("Waiting for block height 101 via rpc")
        self.nodes[0].waitforblockheight(101)
        self.nodes[1].waitforblockheight(101)

        return node, node1

    def _wait_for_mempool(self, node, contents):
        wait_until(lambda: node.rpc.getrawmempool() == contents, check_interval=0.15, timeout=10)

    def _wait_for_block_status(self, node, blockhash, status):
        def wait_predicate():
            for tips in node.rpc.getchaintips():
                if tips["status"] == status and tips["hash"] == blockhash:
                    return True
            return False
        wait_until(wait_predicate, check_interval=0.15, timeout=10)

    def make_block_with_tx(self, node, tx, block_time_offset=None):
        block, _ = make_block(node.rpc)
        if block_time_offset != None:
            # Changing the block time can be used to create block with same contents and different hash
            block.nTime += block_time_offset
        block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.calc_sha256()
        block.solve()
        return block

    def _create_tx(self, tx_out, unlock, lock):
        unlock_script = b'' if callable(unlock) else unlock
        tx_out_value = tx_out.tx.vout[tx_out.n].nValue  # value of the spendable output
        tx = create_transaction(tx_out.tx, tx_out.n, unlock_script, tx_out_value, lock)

        if callable(unlock):
            tx.vin[0].scriptSig = unlock(tx, tx_out.tx)
            tx.calc_sha256()

        return tx

    def _create_confiscation_tx(self, tx_out, unlock, lock):
        ctx = self._create_tx(tx_out, unlock, lock)

        # Insert OP_RETURN output that makes this a confiscation transaction.
        ctx.vout.insert(0, CTxOut(0, CScript(CTX_OP_RETURN + [
            b'\x01' +                       # protocol version number
            hash160(b'confiscation order')  # hash of confiscation order document
        ])))
        ctx.rehash()

        return ctx

    def _create_and_send_tx(self, node):
        tx = self._create_tx(self.chain.get_spendable_output(), b'', CScript([OP_DUP, OP_HASH160, hash160(self.pubkey), OP_EQUALVERIFY, OP_CHECKSIG])) # TXO with standard P2PKH script that can normally only be spent if private key is known
        self.log.info(f"Sending transaction {tx.hash} and generating a new block")
        node.rpc.sendrawtransaction(ToHex(tx))
        assert_equal(node.rpc.getrawmempool(), [tx.hash])
        node.rpc.generate(1)
        assert_equal(node.rpc.getrawmempool(), [])
        return tx

    def _freeze_txo(self, node, tx, start_height, stop_height=None):
        self.log.info(f"Freezing TXO {tx.hash},0 on consensus blacklist at heights [{start_height},{stop_height})")

        enforceAtHeight = {"start": start_height}
        if stop_height != None:
            enforceAtHeight["stop"] = stop_height

        result=node.rpc.addToConsensusBlacklist({
            "funds": [
                {
                    "txOut" : {
                        "txId" : tx.hash,
                        "vout" : 0
                    },
                    "enforceAtHeight": [enforceAtHeight],
                    "policyExpiresWithConsensus": False
                }]
        })
        assert_equal(result["notProcessed"], [])

    def _whitelistTx(self, node, tx, enforce_at_height):
        self.log.info(f"Whitelisting confiscation transaction {tx.hash} with enforceAtHeight={enforce_at_height}")
        result = node.rpc.addToConfiscationTxidWhitelist({"confiscationTxs": [{"confiscationTx" : {"enforceAtHeight" : enforce_at_height, "hex": ToHex(tx)}}]})
        assert_equal(result["notProcessed"], [])

    def check_log(self, node, line_text):
        for line in open(glob.glob(node.datadir + "/regtest/bitcoind.log")[0]):
            if re.search(line_text, line) is not None:
                self.log.debug("Found line in bitcoind.log: %s", line.strip())
                return True
        return False

    def _check_invalid_confiscation_transaction_common(self, node):
        frozen_tx = self._create_and_send_tx(node)
        self._freeze_txo(node, frozen_tx, 0)

        def send_over_p2p_and_rpc(tx, err_str, err_code=-26):
            best_hash = node.rpc.getbestblockhash()
            if len(tx.vin) > 0:
                self._whitelistTx(node, tx, node.rpc.getblockcount()+1)
            node.send_message(msg_tx(tx))
            err_str_tx = 'flexible-' + err_str if err_str == 'bad-txns-vout-p2sh' else err_str
            wait_until(lambda: check_for_log_msg(node.rpc, f"txn= {tx.hash} rejected {err_str_tx}")
                       or (err_str == 'Missing inputs'
                           and check_for_log_msg(node.rpc, f"txn= {tx.hash} detected orphan")))
            assert_raises_rpc_error(err_code, err_str_tx, node.rpc.sendrawtransaction, ToHex(confiscate_tx))
            assert tx.hash not in node.rpc.getrawmempool()
            block = self.make_block_with_tx(node, tx)
            if err_str == 'Missing inputs':
                err_str = "bad-txns-inputs-missingorspent"
            assert_equal(node.rpc.submitblock(ToHex(block)), err_str)
            node.send_message(msg_block(block))
            wait_until(lambda: check_for_log_msg(node.rpc, f"received block {block.hash}"
                                                 or f"ConnectBlock {block.hash} failed"))
            assert_equal(best_hash, node.rpc.getbestblockhash())

        # invalid tx vin is null
        result = node.rpc.addToConsensusBlacklist({
            "funds": [{"txOut": {"txId": '0' * 256, "vout": 0},
                       "enforceAtHeight": [{"start": 0}],
                       "policyExpiresWithConsensus": False}]
        })
        assert_equal(result["notProcessed"], [])
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vin[0].prevout = COutPoint()
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "Missing inputs", -25)

        # invalid tx p2sh
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout.append(CTxOut(COIN, CScript([OP_HASH160, hash160(self.pubkey), OP_EQUAL])))
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-vout-p2sh")

        # invalid tx value in < value out
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout[0].nValue = 100 * COIN
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-in-belowout")

        # invalid tx duplicate vin
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vin.append(confiscate_tx.vin[-1])
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-inputs-duplicate")

        # invalid tx vout < 0
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout[0].nValue = -1
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-vout-negative")

        # invalid sum tx vout > MAX_MONEY
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout[0].nValue = 11000000 * COIN
        confiscate_tx.vout.append(CTxOut(11000000 * COIN, CScript([OP_TRUE])))
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-txouttotal-toolarge")

        # invalid tx vout > MAX_MONEY
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout[0].nValue = 21000000 * COIN + 1
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-vout-toolarge")

        # invalid tx double-spent
        prev_spent = frozen_tx.vin[0].prevout
        result = node.rpc.addToConsensusBlacklist({
            "funds": [{"txOut": {"txId": '%064x' % prev_spent.hash, "vout": prev_spent.n},
                       "enforceAtHeight": [{"start": 0}],
                       "policyExpiresWithConsensus": False}]
        })
        assert_equal(result["notProcessed"], [])
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vin[0].prevout = frozen_tx.vin[0].prevout
        confiscate_tx.rehash()
        send_over_p2p_and_rpc(confiscate_tx, "Missing inputs", -25)

        # non-final tx in block only
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.nLockTime = node.rpc.getblockcount()+1
        confiscate_tx.vin[0].nSequence = 0
        confiscate_tx.rehash()
        best_hash = node.rpc.getbestblockhash()
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        block = self.make_block_with_tx(node, confiscate_tx)
        assert_equal(node.rpc.submitblock(ToHex(block)), "bad-txns-nonfinal")
        node.send_message(msg_block(block))
        wait_until(lambda: check_for_log_msg(node.rpc, f"received block {block.hash}"))
        assert_equal(best_hash, node.rpc.getbestblockhash())

        # coinbase confiscation tx
        block, _ = make_block(node.rpc)
        node.rpc.submitblock(ToHex(block))
        wait_until(lambda: node.rpc.getbestblockhash() == block.hash)
        coinbase_tx = block.vtx[0]
        result = node.rpc.addToConsensusBlacklist({
            "funds": [{"txOut": {"txId": coinbase_tx.hash, "vout": 0},
                       "enforceAtHeight": [{"start": 0}],
                       "policyExpiresWithConsensus": False}]
        })
        assert_equal(result["notProcessed"], [])
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(coinbase_tx, 0), b'', CScript([OP_TRUE]))
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-premature-spend-of-coinbase")

        # bad-txns-vin-empty
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vin = []
        confiscate_tx.rehash()
        node.send_message(msg_tx(confiscate_tx))
        send_over_p2p_and_rpc(confiscate_tx, "bad-txns-vin-empty")

        node.rpc.clearBlacklists({"removeAllEntries" : True})

    def _check_invalid_confiscation_transaction_policy(self, node):
        frozen_tx = self._create_and_send_tx(node)
        self._freeze_txo(node, frozen_tx, 0)

        def assert_result(result, txid, err_str):
            assert 'invalid' in result
            invalid = result['invalid'][0]
            assert_equal(invalid['txid'], txid)
            assert_equal(invalid['reject_reason'], err_str)

        def send_block_over_p2p_accept_invalidate(tx):
            block = self.make_block_with_tx(node, tx)
            node.send_message(msg_block(block))
            wait_until(lambda: node.rpc.getbestblockhash() == block.hash)
            node.rpc.invalidateblock(block.hash)

        # invalid tx vout > max fee
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout[1].nValue = COIN
        confiscate_tx.rehash()
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        assert_raises_rpc_error(-26, "absurdly-high-fee", node.rpc.sendrawtransaction, ToHex(confiscate_tx))
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid tx size
        script = CScript([OP_RETURN] * 1048576)
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', script)
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'maxtxsizepolicy': 1048576, 'acceptnonstdoutputs': False}}])
        assert_result(result, confiscate_tx.hash, 'tx-size')
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid tx mempool conflict
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        # previous tx locks the utxo
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'acceptnonstdoutputs': True}}])
        assert_result(result, confiscate_tx.hash, 'txn-mempool-conflict')
        # it's needed block to be accepted
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid script not push only
        script = CScript([OP_NOP])
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), script, CScript([OP_TRUE]))
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'acceptnonstdoutputs': False}}])
        assert_result(result, confiscate_tx.hash, 'scriptsig-not-pushonly')
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid tx multisig
        script = CScript([b'\x01', self.pubkey, b'\x01', OP_CHECKMULTISIG])
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', script)
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'acceptnonstdoutputs': False}}])
        assert_result(result, confiscate_tx.hash, 'bare-multisig')
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid tx vout dust
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout.append(CTxOut(0, CScript([OP_RETURN])))
        confiscate_tx.rehash()
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'acceptnonstdoutputs': False}}])
        assert_result(result, confiscate_tx.hash, 'dust')
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid tx scriptpubkey
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_RETURN]))
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'acceptnonstdoutputs': False}}])
        assert_result(result, confiscate_tx.hash, 'scriptpubkey')
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid tx scriptpubkey (2)
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_RETURN]))
        confiscate_tx.vout[1].nValue = 1
        # no rehash call
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        confiscate_tx.rehash()
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'acceptnonstdoutputs': False}}])
        assert_result(result, confiscate_tx.hash, 'scriptpubkey')
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        # invalid tx data carrier size
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout[0].scriptPubKey = CScript(CTX_OP_RETURN + [b'\x01' * 70])
        confiscate_tx.rehash()
        self._whitelistTx(node, confiscate_tx, node.rpc.getblockcount()+1)
        result = node.rpc.sendrawtransactions([{'hex': ToHex(confiscate_tx), 'config': {'datacarriersize': 50, 'acceptnonstdoutputs': False}}])
        assert_result(result, confiscate_tx.hash, 'datacarrier-size-exceeded')
        send_block_over_p2p_accept_invalidate(confiscate_tx)

        node.rpc.clearBlacklists({"removeAllEntries" : True})

    def _check_invalid_confiscation_transactios(self, node):
        frozen_tx = self._create_and_send_tx(node)
        self._freeze_txo(node, frozen_tx, 0)

        def check_script(scriptPubKeyArray):
            confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
            confiscate_tx.vout[0].scriptPubKey = CScript(CTX_OP_RETURN + scriptPubKeyArray)
            confiscate_tx.rehash()
            assert_raises_rpc_error(-26, "bad-ctx-invalid", node.rpc.sendrawtransaction, ToHex(confiscate_tx))
            # Check that invalid confiscation transaction cannot be whitelisted
            result = node.rpc.addToConfiscationTxidWhitelist({"confiscationTxs": [{"confiscationTx" : {"enforceAtHeight" : 100, "hex": ToHex(confiscate_tx)}}]})
            assert_equal(result["notProcessed"], [{'confiscationTx': {'txId': confiscate_tx.hash}, 'reason': 'confiscation transaction is not valid'}])

        self.log.info(f"Checking detection of invalid confiscation transactions trying to spend frozen TXO {frozen_tx.hash},0")

        # too short scripts
        check_script([])
        check_script([b'\x01' + bytes(19)])
        check_script([CScriptOp(1)])

        # script shorter than length specified in PUSHDATA
        check_script([CScriptOp(21), CScriptOp(1)] + [CScriptOp(0)]*19)
        check_script([CScriptOp(42), CScriptOp(1)] + [CScriptOp(0)]*40)

        # script longer than length specified in PUSHDATA
        check_script([CScriptOp(21), CScriptOp(1)] + [CScriptOp(0)]*21)
        check_script([CScriptOp(42), CScriptOp(1)] + [CScriptOp(0)]*42)

        # 84 bytes is one byte too long and requires OP_PUSHDATA1
        check_script([b'\x01' + bytes(75)])

        # not using pushdata
        check_script([OP_NOP, b'\x01' + hash160(b'confiscation order')])
        check_script([OP_FALSE, b'\x01' + hash160(b'confiscation order')])
        check_script([OP_PUSHDATA1, b'\x01' + hash160(b'confiscation order')])

        # invalid version
        check_script([b'\x00' + hash160(b'confiscation order')])
        check_script([b'\x02' + hash160(b'confiscation order')])

        # there should be no other OP_RETURN output
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        confiscate_tx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN])))
        assert_raises_rpc_error(-26, "bad-ctx-invalid", node.rpc.sendrawtransaction, ToHex(confiscate_tx))

        # transactions without any input should be detected when whitelisting
        ctx0 = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        ctx0.vin=[]
        ctx0.rehash()
        result = node.rpc.addToConfiscationTxidWhitelist({"confiscationTxs": [{"confiscationTx" : {"enforceAtHeight" : 0, "hex": ToHex(ctx0)}}]})
        assert_equal(result["notProcessed"], [{'confiscationTx': {'txId': ctx0.hash}, 'reason': 'confiscation transaction is not valid'}])

        # invalid hex strings should be detected when whitelisting
        result = node.rpc.addToConfiscationTxidWhitelist({"confiscationTxs": [{"confiscationTx" : {"enforceAtHeight" : 0, "hex": ""}}]})
        assert_equal(result["notProcessed"], [{'confiscationTx': {'txId': '0000000000000000000000000000000000000000000000000000000000000000'}, 'reason': 'invalid transaction hex string'}])
        result = node.rpc.addToConfiscationTxidWhitelist({"confiscationTxs": [{"confiscationTx" : {"enforceAtHeight" : 0, "hex": "02"}}]})
        assert_equal(result["notProcessed"], [{'confiscationTx': {'txId': '0000000000000000000000000000000000000000000000000000000000000000'}, 'reason': 'invalid transaction hex string'}])

        # Cleanup
        result = node.rpc.clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 1) # 1 frozen txo

    def _check_confiscation(self, node, node1):
        # node and node1 are assumed to be connected so that node will send new transactions and blocks to node1

        frozen_tx = self._create_and_send_tx(node)
        self._freeze_txo(node, frozen_tx, 0)

        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE])) # confiscation transaction with empty unlock script
        self.log.info(f"Sending confiscation transaction {confiscate_tx.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is rejected because it is not whitelisted")
        assert_raises_rpc_error(-26, "bad-ctx-not-whitelisted", node.rpc.sendrawtransaction, ToHex(confiscate_tx))

        block_ctx1 = self.make_block_with_tx(node, confiscate_tx)
        self.log.info(f"Sending block {block_ctx1.hash} with confiscation transaction {confiscate_tx.hash} via P2P and checking that it is detected as invalid")
        node.send_and_ping(msg_block(block_ctx1))
        self._wait_for_block_status(node, block_ctx1.hash, "invalid")

        current_block_height = node.rpc.getblockcount()
        self.log.info(f"Current block height: {current_block_height}")

        enforce_at_height = current_block_height + 2 # one block after the mempool height
        self._whitelistTx(node, confiscate_tx, enforce_at_height)

        self.log.info(f"Sending confiscation transaction {confiscate_tx.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is still rejected because it is not whitelisted at current mempool height")
        assert_raises_rpc_error(-26, "bad-ctx-not-whitelisted", node.rpc.sendrawtransaction, ToHex(confiscate_tx))

        block_ctx2 = self.make_block_with_tx(node, confiscate_tx, 2)
        self.log.info(f"Sending block {block_ctx2.hash} with confiscation transaction {confiscate_tx.hash} via P2P and checking that it is detected as invalid")
        node.send_and_ping(msg_block(block_ctx2))
        self._wait_for_block_status(node, block_ctx2.hash, "invalid")

        self.log.info("Clearing blacklists with removeAllEntries=true to remove all whitelisted txs and frozen TXOs")
        result = node.rpc.clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 2)

        # Make sure both nodes are synchronized
        node1.rpc.waitforblockheight(current_block_height)
        assert_equal(node1.rpc.getblockcount(), current_block_height)

        self.log.info("Freezing TXO on both nodes")
        enforce_at_height = current_block_height + 1 # mempool height
        self._freeze_txo(node,  frozen_tx, enforce_at_height)
        self._freeze_txo(node1, frozen_tx, enforce_at_height)

        self.log.info("Whitelisting confiscation transaction on one node but not on the other")
        self._whitelistTx(node,  confiscate_tx, enforce_at_height)

        self.log.info(f"Sending confiscation transaction {confiscate_tx.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is accepted on one node, but not on the other node")
        node.rpc.sendrawtransaction(ToHex(confiscate_tx))
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])
        time.sleep(1)
        assert_equal(node1.rpc.getrawmempool(), [])

        node.rpc.clearConfiscationWhitelist() # remove confiscation transaction from mempool by clearing whitelist
        assert_equal(node.rpc.getrawmempool(), [])

        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE, OP_NOP])) # must use a different confiscation tx so that it is relayed again to node1 (because the first one was rejected)
        self._whitelistTx(node,  confiscate_tx, enforce_at_height)
        self._whitelistTx(node1, confiscate_tx, enforce_at_height+1) # whitelisted, but not at current mempool height

        self.log.info(f"Sending confiscation transaction {confiscate_tx.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is accepted on one node, but not on the other node")
        node.rpc.sendrawtransaction(ToHex(confiscate_tx))
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])
        time.sleep(1)
        assert_equal(node1.rpc.getrawmempool(), [])

        node.rpc.clearConfiscationWhitelist()
        assert_equal(node.rpc.getrawmempool(), [])
        node1.rpc.clearConfiscationWhitelist()

        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE, OP_NOP, OP_NOP])) # must use a different confiscation tx
        self.log.info("Whitelisting confiscation transaction on both nodes")
        self._whitelistTx(node,  confiscate_tx, enforce_at_height)
        self._whitelistTx(node1, confiscate_tx, enforce_at_height)

        self.log.info(f"Sending confiscation transaction {confiscate_tx.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is accepted on both nodes")
        node.rpc.sendrawtransaction(ToHex(confiscate_tx))
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])
        self._wait_for_mempool(node1, [confiscate_tx.hash])

        self.log.info("Checking that output of confiscation transaction is marked as confiscated in mempool")
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=True)
        assert_equal(result["confiscation"], True)
        result = node.rpc.gettxout(txid=frozen_tx.hash, n=0, include_mempool=False) # NOTE: Exclude mempool because in mempool this TXO is already spent
        assert_equal(result["confiscation"], False) # frozen TXO should not be marked as confiscation output
        result = node1.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=True) # same on node1
        assert_equal(result["confiscation"], True)
        result = node1.rpc.gettxout(txid=frozen_tx.hash, n=0, include_mempool=False)
        assert_equal(result["confiscation"], False)

        self.log.info("Generating block that will contain confiscation transaction")
        block_hash = node.rpc.generate(1)[0]
        assert_equal(node.rpc.getrawmempool(), [])
        self._wait_for_mempool(node1, [])

        current_block_height = node.rpc.getblockcount()
        self.log.info(f"Current block height: {current_block_height}")
        assert_equal(node1.rpc.getblockcount(), current_block_height)

        self.log.info("Checking that output of confiscation transaction is marked as confiscated in UTXODB")
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=False)
        assert_equal(result["confiscation"], True)
        result = node1.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=False) # same on node1
        assert_equal(result["confiscation"], True)

        self.log.info(f"Invalidating and reconsidering tip {block_hash} to check that output of confiscation transaction is properly removed and added back to UTXODB")
        node.rpc.invalidateblock(block_hash)
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=False)
        assert_equal(result, None)
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=True)
        assert_equal(result["confiscation"], True)
        node.rpc.reconsiderblock(block_hash)
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=False)
        assert_equal(result["confiscation"], True)

        # Create another confiscation transaction spending the same TXO
        confiscate_tx2 = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_NOP, OP_TRUE]))
        self._whitelistTx(node, confiscate_tx2, current_block_height)

        self.log.info(f"Sending another confiscation transaction {confiscate_tx2.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is rejected because input is already spent")
        assert_raises_rpc_error(-25, "Missing inputs", node.rpc.sendrawtransaction, ToHex(confiscate_tx2))

        spend_tx = self._create_tx(PreviousSpendableOutput(confiscate_tx, 1), b'', CScript([OP_TRUE]))
        self.log.info(f"Sending transaction {spend_tx.hash} spending confiscated TXO {confiscate_tx.hash},1 and checking that it is rejected because output has not matured")
        assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-confiscation", node.rpc.sendrawtransaction, ToHex(spend_tx))

        spend_tx1 = self._create_tx(PreviousSpendableOutput(confiscate_tx, 1), b'', CScript([OP_TRUE, OP_NOP])) # Must use a different tx, otherwise node1 would immediately force relay it to node (because node is whitelisted), which would then not relay it below in test to node1 when it is received again via RPC and is valid.
        self.log.info(f"Sending transaction {spend_tx1.hash} spending confiscated TXO {confiscate_tx.hash},1 via P2P to node1 and checking that it is not accepted because output has not matured")
        node1.send_and_ping(msg_tx(spend_tx1))
        time.sleep(1)
        assert_equal(node1.rpc.getrawmempool(), [])

        self.log.info(f"Sending block with transaction {spend_tx.hash} spending confiscated TXO {confiscate_tx.hash},1 via P2P to node1 and checking that it is detected as invalid")
        block1 = self.make_block_with_tx(node1, spend_tx)
        node1.send_and_ping(msg_block(block1))
        self._wait_for_block_status(node1, block1.hash, "invalid")

        self.log.info("Mining 998 blocks")
        node.rpc.generate(998)
        current_block_height = node.rpc.getblockcount()
        self.log.info(f"Current block height: {current_block_height}")
        node1.rpc.waitforblockheight(current_block_height)

        self.log.info(f"Sending transaction {spend_tx.hash} spending confiscated TXO {confiscate_tx.hash},1 and checking that it is rejected because output has still not matured")
        assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-confiscation", node.rpc.sendrawtransaction, ToHex(spend_tx))

        self.log.info("Mining 1 block")
        block_hash1 = node.rpc.generate(1)[0]
        node1.rpc.waitforblockheight(current_block_height+1)

        self.log.info("Clearing blacklists+whitelist on node1 to check it is not needed when spending confiscation TXO")
        result = node1.rpc.clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 2) # 1 whitelisted txs + 1 frozen txo

        self.log.info(f"Sending transaction {spend_tx.hash} spending confiscated TXO {confiscate_tx.hash},1 and checking that it is accepted on both nodes")
        node.rpc.sendrawtransaction(ToHex(spend_tx))
        assert_equal(node.rpc.getrawmempool(), [spend_tx.hash])
        self._wait_for_mempool(node1, [spend_tx.hash])

        self.log.info("Mining 1 block")
        block_hash = node.rpc.generate(1)[0]
        assert_equal(node.rpc.getrawmempool(), [])
        node1.rpc.waitforblockheight(current_block_height+2)

        self.log.info(f"Invalidating tip {block_hash} to check that confiscated TXO is properly added back to UTXODB")
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=False)
        assert_equal(result, None)
        node.rpc.invalidateblock(block_hash)
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=False)
        assert_equal(result["confiscation"], True)
        assert_equal(node.rpc.getrawmempool(), [spend_tx.hash]) # transaction should be put back to mempool

        self.log.info(f"Invalidating tip {block_hash1} to check transaction spending confiscated TXO is removed from mempool because output is now immature")
        node.rpc.invalidateblock(block_hash1)
        assert_equal(node.rpc.getrawmempool(), [])

        self.log.info(f"Reconsidering tip {block_hash1}")
        node.rpc.reconsiderblock(block_hash1)
        self.log.info(f"Reconsidering tip {block_hash} and checking that confiscated TXO is removed from UTXODB")
        node.rpc.reconsiderblock(block_hash)
        result = node.rpc.gettxout(txid=confiscate_tx.hash, n=1, include_mempool=False)
        assert_equal(result, None)

        # Sanity check to ensure both nodes are at the same tip
        assert_equal(node .rpc.getbestblockhash(), block_hash)
        assert_equal(node1.rpc.getbestblockhash(), block_hash)

        # Cleanup
        result = node.rpc.clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 3) # 2 whitelisted txs + 1 frozen txo

    def _check_mempool_removal(self, node):
        frozen_tx = self._create_and_send_tx(node)

        root_block, _ = make_block(node)
        self.log.info(f"Submitting new block {root_block.hash}")
        node.rpc.submitblock(root_block.serialize().hex())
        assert_equal(root_block.hash, node.rpc.getbestblockhash())
        root_block_height = node.rpc.getblockcount()
        self.log.info(f"Current block height: {root_block_height}")

        self._freeze_txo(node, frozen_tx, root_block_height, root_block_height+2)
        confiscate_tx = self._create_confiscation_tx(PreviousSpendableOutput(frozen_tx, 0), b'', CScript([OP_TRUE]))
        self._whitelistTx(node, confiscate_tx, root_block_height+1)
        self.log.info(f"Sending confiscation transaction {confiscate_tx.hash} spending frozen TXO {frozen_tx.hash},0 and checking that it is accepted")
        node.rpc.sendrawtransaction(ToHex(confiscate_tx))
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])

        self.log.info("Checking that confiscation transaction is removed from mempool if blacklist is cleared")
        result=node.rpc.clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 2)
        assert_equal(node.rpc.getrawmempool(), [])

        self.log.info("Restoring status of TXO so that it is frozen at mempool height, whitelisting transaction, sending confiscation transaction again and checking that it is accepted")
        self._freeze_txo(node, frozen_tx, root_block_height, root_block_height+2)
        self._whitelistTx(node, confiscate_tx, root_block_height+1)
        node.rpc.sendrawtransaction(ToHex(confiscate_tx))
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])

        self.log.info("Checking that confiscation transaction is removed from mempool if confiscation whitelist is cleared")
        result=node.rpc.clearConfiscationWhitelist()
        assert_equal(result["numFrozenBackToConsensus"], 1)
        assert_equal(result["numUnwhitelistedTxs"], 1)
        assert_equal(node.rpc.getrawmempool(), [])
        assert_raises_rpc_error(-26, "bad-ctx-not-whitelisted", node.rpc.sendrawtransaction, ToHex(confiscate_tx))

        self.log.info("Whitelisting transaction, sending confiscation transaction again and checking that it is accepted")
        self._whitelistTx(node, confiscate_tx, root_block_height+1)
        node.rpc.sendrawtransaction(ToHex(confiscate_tx))
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])

        block, _ = make_block(node)
        self.log.info(f"Submitting new block {block.hash} and checking that confiscation transaction {confiscate_tx.hash} stays in mempool even if TXO is no longer consensus frozen at higher height")
        node.rpc.submitblock(block.serialize().hex())
        assert_equal(block.hash, node.rpc.getbestblockhash())
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])

        self._freeze_txo(node, frozen_tx, root_block_height+2, root_block_height+3)

        self.log.info(f"Invalidating block {block.hash} and checking that confiscation transaction {confiscate_tx.hash} stays in mempool even if TXO consensus freeze interval was updated to no longer be frozen at lower height")
        node.rpc.invalidateblock(block.hash)
        assert_equal(node.rpc.getrawmempool(), [confiscate_tx.hash])

        mempool_scan_check_log_string = "Removing any confiscation transactions, which were previously valid, but are now not because the mempool height has become lower"
        assert not self.check_log(node.rpc, mempool_scan_check_log_string) # bitcoind should not unnecessarily scan whole mempool to find invalid confiscation transactions.

        self.log.info(f"Invalidating block {root_block.hash} and checking that confiscation transaction {confiscate_tx.hash} is removed from mempool because it is no longer whitelisted at lower height")
        node.rpc.invalidateblock(root_block.hash)
        assert_equal(node.rpc.getrawmempool(), [])
        assert self.check_log(node.rpc, mempool_scan_check_log_string) # bitcoind now should scan whole mempool

        # Cleanup
        node.rpc.reconsiderblock(root_block.hash) # this will also reconsider next block which was invalidated above
        assert_equal(block.hash, node.rpc.getbestblockhash())
        result = node.rpc.clearBlacklists({"removeAllEntries" : True})
        assert_equal(result["numRemovedEntries"], 2) # 1 whitelisted tx + 1 frozen txo

    def run_test(self):
        node, node1 = self._init()

        self._check_invalid_confiscation_transactios(node)
        self._check_invalid_confiscation_transaction_common(node)
        self._check_invalid_confiscation_transaction_policy(node)
        self._check_confiscation(node, node1)
        self._check_mempool_removal(node)


if __name__ == '__main__':
    FrozenTXOConfiscation().main()
