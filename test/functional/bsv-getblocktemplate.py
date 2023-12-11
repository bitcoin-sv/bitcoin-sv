#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import create_block, create_coinbase, create_transaction
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.util import assert_equal, wait_until, assert_raises_rpc_error
from test_framework.mininode import ToHex, msg_block, msg_tx, CBlock, CTransaction, CTxIn, COutPoint, CTxOut
from test_framework.script import CScript, OP_TRUE, OP_RETURN
from test_framework.cdefs import BUFFER_SIZE_HttpTextWriter
from binascii import b2a_hex


def b2x(b):
    return b2a_hex(b).decode('ascii')


class GetBlockTemplateRPCTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.chain = ChainManager()

    def check_mempool(self, rpc, should_be_in_mempool):
        wait_until(lambda: {t.hash for t in should_be_in_mempool}.issubset(set(rpc.getrawmempool())))

    def createLargeTransaction(self, size, depends):
        tx = CTransaction()
        for depend in depends:
            tx.vin.append(CTxIn(COutPoint(depend.sha256, 0), b''))
        tx.vout.append(CTxOut(int(100), CScript([OP_RETURN,  b"a" * size])))
        tx.rehash()
        return tx

    def create_bad_block(self, template):
        coinbase_tx = create_coinbase(height=int(template["height"]) + 1)
        coinbase_tx.vin[0].nSequence = 2 ** 32 - 2
        coinbase_tx.rehash()
        block = CBlock()
        block.nVersion = template["version"]
        block.nTime = template["curtime"]
        block.nBits = int(template["bits"], 16)
        block.nNonce = 0
        block.vtx = [coinbase_tx]

        # Make this block incorrect.
        block.hashPrevBlock = 123
        block.hashMerkleRoot = block.calc_merkle_root()

        return block

    def checkBlockTemplate(self, template, txs, dependingTx):

        assert 'capabilities' in template
        assert_equal('proposal', template['capabilities'][0])

        assert 'version' in template
        assert 'previousblockhash' in template
        assert_equal(self.nodes[0].getbestblockhash(), template['previousblockhash'])

        assert 'transactions' in template

        # check if hex data was parsed correctly
        txs_data = [tx['data'] for tx in template['transactions']]
        assert(ToHex(dependingTx) in txs_data)
        for tx in txs:
            assert(ToHex(tx) in txs_data)

        # check dependencies
        depending_indices = []
        depending_txs_hash = [tx.hash for tx in txs]
        for i in range(len(template['transactions'])):
            if template['transactions'][i]['hash'] in depending_txs_hash:
                depending_indices.append(i+1)

        for tmpl_tx in template['transactions']:
            if tmpl_tx['hash'] == dependingTx.hash:
                assert_equal(2, len(tmpl_tx['depends']))
                assert_equal(set(tmpl_tx['depends']), set(depending_indices))
                break

        assert 'coinbaseaux' in template
        assert 'coinbasevalue' in template
        assert 'longpollid' in template
        assert 'target' in template
        assert 'mintime' in template
        assert 'mutable' in template
        assert 'noncerange' in template
        assert 'sizelimit' in template
        assert 'curtime' in template
        assert 'bits' in template
        assert 'height' in template
        assert_equal(self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height'] + 1, template['height'])

    def run_test(self):

        self.stop_node(0)
        with self.run_node_with_connections("test getblocktemplate RPC call", 0, ["-minminingtxfee=0.0000001"], 1) as connections:

            connection = connections[0]

            # Preparation.
            self.chain.set_genesis_hash(int(self.nodes[0].getbestblockhash(), 16))
            starting_blocks = 101
            block_count = 0
            for i in range(starting_blocks):
                block = self.chain.next_block(block_count)
                block_count += 1
                self.chain.save_spendable_output()
                connection.cb.send_message(msg_block(block))
            out = []
            for i in range(starting_blocks):
                out.append(self.chain.get_spendable_output())
            self.nodes[0].waitforblockheight(starting_blocks)

            # Create and send 2 transactions.
            transactions = []
            for i in range(2):
                tx = create_transaction(out[i].tx, out[i].n, b"", 100000, CScript([OP_TRUE]))
                connection.cb.send_message(msg_tx(tx))
                transactions.append(tx)
            self.check_mempool(self.nodes[0], transactions)

            # Create large transaction that depends on previous two transactions.
            # If transaction pubkey contains 1/2 of BUFFER_SIZE_HttpTextWriter of data, it means that final result will for sure be chunked.
            largeTx = self.createLargeTransaction(int(BUFFER_SIZE_HttpTextWriter/2), transactions)
            connection.cb.send_message(msg_tx(largeTx))
            self.check_mempool(self.nodes[0], [largeTx])

            # Check getblocktemplate response.
            template = self.nodes[0].getblocktemplate()
            self.checkBlockTemplate(template, transactions, largeTx)

            # Check getblocktemplate with invalid reponse
            block = self.create_bad_block(template)
            rsp = self.nodes[0].getblocktemplate({'data': b2x(block.serialize()), 'mode': 'proposal'})
            assert_equal(rsp, "inconclusive-not-best-prevblk")

            assert_raises_rpc_error(-22,
                                    "Block decode failed",
                                    self.nodes[0].getblocktemplate,
                                    {'data': b2x(block.serialize()[:-1]),
                                     'mode': 'proposal'})

            # Test getblocktemplate in a batch
            batch = self.nodes[0].batch([
                self.nodes[0].getblockbyheight.get_request(100),
                self.nodes[0].getblocktemplate.get_request(),
                self.nodes[0].getblockcount.get_request(),
                self.nodes[0].undefinedmethod.get_request()])

            assert_equal(batch[0]["error"], None)
            assert_equal(batch[1]["error"], None)
            assert_equal(batch[1]["result"], template)
            assert_equal(batch[2]["error"], None)
            assert_equal(batch[3]["error"]["message"], "Method not found")
            assert_equal(batch[3]["result"], None)


if __name__ == '__main__':
    GetBlockTemplateRPCTest().main()
