#!/usr/bin/env python3
# Copyright (c) 2023 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from decimal import Decimal
from test_framework.blocktools import (
    make_block,
    create_block_from_candidate,
    create_transaction,
    PreviousSpendableOutput
)
from test_framework.key import CECKey
from test_framework.mininode import (
    msg_block,
    msg_tx,
    ToHex,
    CTxOut
)
from test_framework.script import (
    CScript,
    hash160,
    OP_DUP,
    OP_CHECKSIG,
    OP_EQUALVERIFY,
    OP_HASH160,
)
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import (
    assert_equal,
    bytes_to_hex_str,
    check_for_log_msg,
    wait_until
)

'''
Test 0-satoshi outputs are allowed in unconfirmed txs.
Sends simple payment tx with 0-satoshi output and no change via RPC and P2P.
Test 0-satoshi outputs are allowed in block.
Creates block with tx 0-satoshi output and no change via submitminingsolution and P2P.
Regtest/Testnet/STN allowed scenarios are successfully covered
Mainnet configuration doesn't allow a node to accept and relay non-standard tx at the policy level
(0-satoshi output is identified as dust and tx is marked as non-standard and rejected),
no restriction on confirmed tx propagated in block
'''


class TestZeroSatoshiOutput(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = ["-minminingtxfee=0.00000001",
                           "-genesisactivationheight=100"]

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        prvkey = CECKey()
        prvkey.set_secretbytes(b"horsebattery")
        pubkey = prvkey.get_pubkey()
        chain = ChainManager()
        p2pk = CScript([pubkey, OP_CHECKSIG])
        p2pkh = CScript([OP_DUP, OP_HASH160, hash160(pubkey),
                         OP_EQUALVERIFY, OP_CHECKSIG])

        def send_block_with_tx(node, tx):
            block, _ = make_block(node.rpc)
            block.vtx.append(tx)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.calc_sha256()
            block.solve()
            node.send_message(msg_block(block))
            wait_until(lambda: node.rpc.getbestblockhash() == block.hash)

        def create_tx_1_utxo_0_sat(script):
            out = chain.get_spendable_output()
            tx = create_transaction(out.tx, out.n, b'', 0, script)
            tx.vout = [tx.vout[0]] # no change
            tx.rehash()
            return tx

        def send_tx_over_p2p(node, tx):
            node.send_message(msg_tx(tx))
            wait_until(lambda: tx.hash in node.rpc.getrawmempool())

        def send_tx_over_rpc(node, tx, dontCheckFee=True):
            node.rpc.sendrawtransaction(ToHex(tx), True, dontCheckFee)
            wait_until(lambda: tx.hash in node.rpc.getrawmempool())

        def check_txout(node, tx):
            # ensures changes are flushed to coinsdb
            node.rpc.gettxoutsetinfo()
            include_mempool = False
            info_tx = node.rpc.gettxout(tx.hash, 0, include_mempool)
            assert_equal(info_tx['value'], Decimal('0'))
            assert_equal(info_tx['scriptPubKey']['hex'],
                         bytes_to_hex_str(tx.vout[0].scriptPubKey))

        def test_tx(script, dontCheckFee=True):
            tx1 = create_tx_1_utxo_0_sat(script)
            send_tx_over_p2p(node, tx1)

            tx2 = create_tx_1_utxo_0_sat(script)
            send_tx_over_rpc(node, tx2, dontCheckFee)

            node.rpc.generate(1)
            assert_equal(node.rpc.getrawmempool(), [])

            check_txout(node, tx1)
            check_txout(node, tx2)

        def mine_block(script, dontCheckFee=True):
            tx1 = create_tx_1_utxo_0_sat(script)
            send_tx_over_p2p(node, tx1)

            tx2 = create_tx_1_utxo_0_sat(script)
            send_tx_over_rpc(node, tx2, dontCheckFee)

            candidate = node.rpc.getminingcandidate(True)
            block, coinbase_tx = create_block_from_candidate(candidate, True)
            node.rpc.submitminingsolution({'id': candidate['id'],
                                           'nonce': block.nNonce,
                                           'coinbase': ToHex(coinbase_tx)})

            assert_equal(node.rpc.getrawmempool(), [])
            check_txout(node, tx1)
            check_txout(node, tx2)

        def test_block(script):
            tx = create_tx_1_utxo_0_sat(script)
            send_block_with_tx(node, tx)

            assert_equal(node.rpc.getrawmempool(), [])
            check_txout(node, tx)

        '''
        In regtest, testnet, stn there is no requirement relayed txs to be standard
        so dust output (zero satoshi output) are allowed via RPC, P2P and in block
        '''
        with self.run_node_with_connections("Test txs with 0-satoshi outputs (testnet)",
                                            0,
                                            self.extra_args,
                                            1) as (node,):

            chain.set_genesis_hash(int(node.rpc.getbestblockhash(), 16))

            blocks = 120
            for i in range(blocks):
                block = chain.next_block(i)
                chain.save_spendable_output()
                node.send_message(msg_block(block))

            node.rpc.waitforblockheight(blocks)

            test_tx(p2pk, dontCheckFee=True)
            test_tx(p2pkh, dontCheckFee=True)

            test_tx(p2pk, dontCheckFee=False)
            test_tx(p2pkh, dontCheckFee=False)

            mine_block(p2pk, dontCheckFee=True)
            mine_block(p2pkh, dontCheckFee=True)

            mine_block(p2pk, dontCheckFee=False)
            mine_block(p2pkh, dontCheckFee=False)

            test_block(p2pk)
            test_block(p2pkh)

        '''
        In mainnet only standard outputs are allowed, policy exception is available
        via sendrawtransactions including policy rules in config parameters unfortunately
        it cannot be used to relay non-standard tx in case of dust (zero satoshi output)
        acceptnonstdtxn=0 ensures fRequireStandard = true during tx validation
        It simulates mainnet tx validation process
        '''
        with self.run_node_with_connections("Test txs with 0-satoshi outputs (mainnet)",
                                            0,
                                            self.extra_args + ["-acceptnonstdtxn=0"],
                                            1) as (node,):
            # blocks are accepted
            test_block(p2pk)
            test_block(p2pkh)

            # tx cannot be propagated via p2p nor rpc
            tx = create_tx_1_utxo_0_sat(p2pk)
            node.send_message(msg_tx(tx))
            wait_until(lambda: check_for_log_msg(node.rpc, f"txn= {tx.hash} rejected dust"))
            result = node.rpc.sendrawtransactions([{'hex': ToHex(tx),
                                                    'config': {'acceptnonstdoutputs': True}}])
            assert 'invalid' in result
            invalid = result['invalid'][0]
            assert_equal(invalid['txid'], tx.hash)
            assert_equal(invalid['reject_reason'], "dust")


if __name__ == '__main__':
    TestZeroSatoshiOutput().main()
