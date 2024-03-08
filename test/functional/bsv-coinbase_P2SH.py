#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.


from test_framework.blocktools import create_coinbase_P2SH, create_block_from_candidate, create_block, create_coinbase
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import p2p_port, assert_equal, assert_raises_rpc_error
from test_framework.mininode import CBlock, NodeConn, NodeConnCB, NetworkThread, ToHex, msg_block

example_script_hash = "748284390f9e263a4b766a75d0633c50426eb875"


class MiningCoinbaseWithP2SHTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.genesisactivationheight = 1
        self.extra_args = [['-genesisactivationheight=%d' % self.genesisactivationheight]]

    def make_block_withP2SH_coinbase(self):
        tmpl = self.nodes[0].getblocktemplate()
        coinbase_tx = create_coinbase_P2SH(int(tmpl["height"]) + 1, example_script_hash)
        coinbase_tx.vin[0].nSequence = 2 ** 32 - 2
        coinbase_tx.rehash()
        block = CBlock()
        block.nVersion = tmpl["version"]
        block.hashPrevBlock = int(tmpl["previousblockhash"], 16)
        block.nTime = tmpl["curtime"]
        block.nBits = int(tmpl["bits"], 16)
        block.nNonce = 0
        block.vtx = [coinbase_tx]
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        block.rehash()
        return block

    def run_test(self):
        test_node = NodeConnCB()
        connections = []
        connections.append(NodeConn('127.0.0.1', p2p_port(0), self.nodes[0], test_node))
        test_node.add_connection(connections[0])
        NetworkThread().start()
        test_node.wait_for_verack()

        starting_height = 3
        self.nodes[0].generate(starting_height)

        # Create block with P2SH output and send it to node.
        # It should be validated and accepted.
        block = self.make_block_withP2SH_coinbase()
        test_node.send_message(msg_block(block))
        test_node.sync_with_ping()
        # check if block was accepted
        assert_equal(self.nodes[0].getbestblockhash(), block.hash)

        # submitblock with P2SH in coinbase tx (not included in blockchain)
        block = self.make_block_withP2SH_coinbase()
        block.solve()
        assert_raises_rpc_error(-26, "bad-txns-vout-p2sh",
                                self.nodes[0].submitblock, ToHex(block))

        # verifyblockcandidate with P2SH in coinbase tx (not included in blockchain)
        assert_raises_rpc_error(-26, "bad-txns-vout-p2sh",
                                self.nodes[0].verifyblockcandidate, ToHex(block))

        # submitblock without P2SH in coinbase tx (included in blockchain)
        hashPrev = int(self.nodes[0].getbestblockhash(), 16)
        ctx = create_coinbase(self.nodes[0].getblockcount() + 1)
        block2 = create_block(hashPrev, ctx)
        block2.solve()
        self.nodes[0].submitblock(ToHex(block2))
        assert_equal(block2.hash, self.nodes[0].getbestblockhash())

        # submit block with: submitminingsolution
        # Add P2SH to coinbase output - should be rejected
        candidate = self.nodes[0].getminingcandidate(False)
        block, ctx = create_block_from_candidate(candidate, False)
        coinbase_tx = create_coinbase_P2SH(self.nodes[0].getblockcount()+1, example_script_hash)

        # submitminingsolution with P2SH in coinbase tx - should be denied.
        assert_raises_rpc_error(-26, "bad-txns-vout-p2sh",
                                self.nodes[0].submitminingsolution,
                                {'id': candidate['id'],
                                 'nonce': block.nNonce,
                                 'coinbase': '{}'.format(ToHex(coinbase_tx))}
                                )
        # submitminingsolution without P2SH in coinbase - should be accepted
        candidate = self.nodes[0].getminingcandidate(False)
        block, ctx = create_block_from_candidate(candidate, False)
        result = self.nodes[0].submitminingsolution({'id': candidate['id'], 'nonce': block.nNonce,
                                                     'coinbase': '{}'.format(ToHex(ctx))})
        assert_equal(result, True)
        assert_equal(block.hash, self.nodes[0].getbestblockhash())

        # generatetoaddress with nonP2SH address
        height_before = self.nodes[0].getblockcount()
        address = self.nodes[0].getnewaddress()
        self.nodes[0].generatetoaddress(1, address)
        height_after = self.nodes[0].getblockcount()
        assert_equal(height_before + 1, height_after)

        # generatetoaddress with P2SH address (example for regtest: 2MzQwSSnBHWHqSAqtTVQ6v47XtaisrJa1Vc)
        assert_raises_rpc_error(-26, "bad-txns-vout-p2sh",
                                self.nodes[0].generatetoaddress, 1, '2MzQwSSnBHWHqSAqtTVQ6v47XtaisrJa1Vc')


if __name__ == '__main__':
    MiningCoinbaseWithP2SHTest().main()
