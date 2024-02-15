#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""Test RPCs related to blockchainstate.

Test the following RPCs:
    - gettxoutsetinfo
    - getdifficulty
    - getbestblockhash
    - getblock
    - getblocbyheight
    - getblockhash
    - getblockheader
    - getchaintxstats
    - getnetworkhashps
    - verifychain

Tests correspond to code in rpc/blockchain.cpp.
"""

from decimal import Decimal
import http.client
import subprocess

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises,
    assert_raises_rpc_error,
    assert_is_hex_string,
    assert_is_hash_string,
)


class BlockchainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-stopatheight=207']]

    def run_test(self):
        self._test_getchaintxstats()
        self._test_gettxoutsetinfo()
        self._test_getblockheader()
        self._test_getblock()
        self._test_getblockbyheight()
        self._test_getdifficulty()
        self._test_getnetworkhashps()
        self._test_getblock_num_tx()
        self._test_stopatheight()
        assert self.nodes[0].verifychain(4, 0)

    def _test_getchaintxstats(self):
        self.log.info("Test getchaintxstats")
        chaintxstats = self.nodes[0].getchaintxstats(1)
        # 200 txs plus genesis tx
        assert_equal(chaintxstats['txcount'], 201)
        # tx rate should be 1 per 10 minutes, or 1/600
        # we have to round because of binary math
        assert_equal(round(chaintxstats['txrate'] * 600, 10), Decimal(1))

        b1 = self.nodes[0].getblock(self.nodes[0].getblockhash(1))
        b200 = self.nodes[0].getblock(self.nodes[0].getblockhash(200))
        time_diff = b200['mediantime'] - b1['mediantime']

        chaintxstats = self.nodes[0].getchaintxstats()
        assert_equal(chaintxstats['time'], b200['time'])
        assert_equal(chaintxstats['txcount'], 201)
        assert_equal(chaintxstats['window_block_count'], 199)
        assert_equal(chaintxstats['window_tx_count'], 199)
        assert_equal(chaintxstats['window_interval'], time_diff)
        assert_equal(
            round(chaintxstats['txrate'] * time_diff, 10), Decimal(199))

        chaintxstats = self.nodes[0].getchaintxstats(blockhash=b1['hash'])
        assert_equal(chaintxstats['time'], b1['time'])
        assert_equal(chaintxstats['txcount'], 2)
        assert_equal(chaintxstats['window_block_count'], 0)
        assert('window_tx_count' not in chaintxstats)
        assert('window_interval' not in chaintxstats)
        assert('txrate' not in chaintxstats)

        assert_raises_rpc_error(-8, "Invalid block count: should be between 0 and the block's height - 1",
                                self.nodes[0].getchaintxstats, 201)

    def _test_gettxoutsetinfo(self):
        node = self.nodes[0]
        res = node.gettxoutsetinfo()

        assert_equal(res['total_amount'], Decimal('8725.00000000'))
        assert_equal(res['transactions'], 200)
        assert_equal(res['height'], 200)
        assert_equal(res['txouts'], 200)
        assert_equal(res['bogosize'], 17000),
        assert_equal(res['bestblock'], node.getblockhash(200))
        size = res['disk_size']
        assert size > 6400
        assert size < 64000
        assert_equal(len(res['bestblock']), 64)
        assert_equal(len(res['hash_serialized']), 64)

        self.log.info("Test that gettxoutsetinfo() works for blockchain with just the genesis block")
        b1hash = node.getblockhash(1)
        node.invalidateblock(b1hash)

        res2 = node.gettxoutsetinfo()
        assert_equal(res2['transactions'], 0)
        assert_equal(res2['total_amount'], Decimal('0'))
        assert_equal(res2['height'], 0)
        assert_equal(res2['txouts'], 0)
        assert_equal(res2['bogosize'], 0),
        assert_equal(res2['bestblock'], node.getblockhash(0))
        assert_equal(len(res2['hash_serialized']), 64)

        self.log.info("Test that gettxoutsetinfo() returns the same result after invalidate/reconsider block")
        node.reconsiderblock(b1hash)

        res3 = node.gettxoutsetinfo()
        assert_equal(res['total_amount'], res3['total_amount'])
        assert_equal(res['transactions'], res3['transactions'])
        assert_equal(res['height'], res3['height'])
        assert_equal(res['txouts'], res3['txouts'])
        assert_equal(res['bogosize'], res3['bogosize'])
        assert_equal(res['bestblock'], res3['bestblock'])
        assert_equal(res['hash_serialized'], res3['hash_serialized'])

    def _test_getblock(self):
        node = self.nodes[0]
        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, "nonsense")

        besthash = node.getbestblockhash()
        secondbesthash = node.getblockhash(199)

        self.log.info("Test getblock with verbosity=0")
        blockhex = node.getblock(besthash, 0)
        assert_is_hex_string(blockhex)
        self.log.info("Test getblock with verbosity=RAW_BLOCK")
        blockhex = node.getblock(besthash, "RAW_BLOCK")
        assert_is_hex_string(blockhex)
        self.log.info("Test getblock with verbosity=RaW_BlocK")
        blockhex = node.getblock(besthash, "RaW_BlocK")
        assert_is_hex_string(blockhex)

        self.log.info("Test getblock with verbosity=1")
        blockjson = node.getblock(besthash, 1)
        assert_equal(blockjson['hash'], besthash)
        assert_equal(blockjson['height'], 200)
        assert_equal(blockjson['confirmations'], 1)
        assert_equal(blockjson['previousblockhash'], secondbesthash)
        assert_is_hex_string(blockjson['chainwork'])
        assert_is_hash_string(blockjson['hash'])
        assert_is_hash_string(blockjson['previousblockhash'])
        assert_is_hash_string(blockjson['merkleroot'])
        assert_is_hash_string(blockjson['bits'], length=None)
        assert isinstance(blockjson['time'], int)
        assert isinstance(blockjson['mediantime'], int)
        assert isinstance(blockjson['nonce'], int)
        assert isinstance(blockjson['version'], int)
        assert isinstance(int(blockjson['versionHex'], 16), int)
        assert isinstance(blockjson['difficulty'], Decimal)
        assert isinstance(blockjson['tx'], list)
        for tx in blockjson['tx']:
            assert_is_hash_string(tx)

        self.log.info("Test getblock with verbosity=DECODE_HEADER")
        blockjson = node.getblock(besthash, "DECODE_HEADER")
        assert_equal(blockjson['hash'], besthash)
        for tx in blockjson['tx']:
            assert_is_hash_string(tx)

        self.log.info("Test getblock with verbosity=2")
        blockjson = node.getblock(besthash, 2)
        assert_equal(blockjson['hash'], besthash)
        assert_equal(blockjson['height'], 200)
        assert_equal(blockjson['confirmations'], 1)
        assert_equal(blockjson['previousblockhash'], secondbesthash)
        assert_is_hex_string(blockjson['chainwork'])
        assert_is_hash_string(blockjson['hash'])
        assert_is_hash_string(blockjson['previousblockhash'])
        assert_is_hash_string(blockjson['merkleroot'])
        assert_is_hash_string(blockjson['bits'], length=None)
        assert isinstance(blockjson['time'], int)
        assert isinstance(blockjson['mediantime'], int)
        assert isinstance(blockjson['nonce'], int)
        assert isinstance(blockjson['version'], int)
        assert isinstance(int(blockjson['versionHex'], 16), int)
        assert isinstance(blockjson['difficulty'], Decimal)
        for tx in blockjson['tx']:
            assert isinstance(tx, dict)

        self.log.info("Test getblock with verbosity=DECODE_TRANSACTIONS")
        blockjson = node.getblock(besthash, "DECODE_TRANSACTIONS")
        assert_equal(blockjson['hash'], besthash)
        for tx in blockjson['tx']:
            assert isinstance(tx, dict)

        self.log.info("Test getblock with verbosity=3")
        blockjson = node.getblock(besthash, 3)
        assert_equal(blockjson['hash'], besthash)
        assert_equal(blockjson['height'], 200)
        assert_equal(blockjson['confirmations'], 1)
        assert_equal(blockjson['previousblockhash'], secondbesthash)
        assert_is_hex_string(blockjson['chainwork'])
        assert_is_hash_string(blockjson['hash'])
        assert_is_hash_string(blockjson['previousblockhash'])
        assert_is_hash_string(blockjson['merkleroot'])
        assert_is_hash_string(blockjson['bits'], length=None)
        assert isinstance(blockjson['time'], int)
        assert isinstance(blockjson['mediantime'], int)
        assert isinstance(blockjson['nonce'], int)
        assert isinstance(blockjson['version'], int)
        assert isinstance(int(blockjson['versionHex'], 16), int)
        assert isinstance(blockjson['difficulty'], Decimal)
        #only coinbase tx should be in block
        assert_equal(len(blockjson['tx']), 1)
        tx = blockjson['tx'][0]
        assert isinstance(tx, dict)
        assert_is_hash_string(tx['vin'][0]['coinbase'], length=None)

        self.log.info("Test getblock with invalid verbosity fails")
        assert_raises_rpc_error(-8, "Verbosity value out of range", node.getblock, besthash, 4)
        assert_raises_rpc_error(-8, "Verbosity value out of range", node.getblock, besthash, -1)
        assert_raises_rpc_error(-8, "Verbosity value not recognized", node.getblock, besthash, "ASDFG")

    def _test_getblockbyheight(self):
        node = self.nodes[0]
        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, "nonsense")

        besthash = node.getbestblockhash()
        secondbesthash = node.getblockhash(199)

        self.log.info("Test getblockbyheight with verbosity=0")
        blockhex = node.getblockbyheight(1, 0)
        assert_is_hex_string(blockhex)
        self.log.info("Test getblockbyheight with verbosity=RAW_BLOCK")
        blockhex = node.getblockbyheight(1, "RAW_BLOCK")
        assert_is_hex_string(blockhex)
        self.log.info("Test getblockbyheight with verbosity=RaW_BlocK")
        blockhex = node.getblockbyheight(1, "RaW_BlocK")
        assert_is_hex_string(blockhex)

        self.log.info("Test getblockbyheight with verbosity=1")
        blockjson = node.getblockbyheight(200, 1)
        assert_equal(blockjson['hash'], besthash)
        assert_equal(blockjson['height'], 200)
        assert_equal(blockjson['confirmations'], 1)
        assert_equal(blockjson['previousblockhash'], secondbesthash)
        assert_is_hex_string(blockjson['chainwork'])
        assert_is_hash_string(blockjson['hash'])
        assert_is_hash_string(blockjson['previousblockhash'])
        assert_is_hash_string(blockjson['merkleroot'])
        assert_is_hash_string(blockjson['bits'], length=None)
        assert isinstance(blockjson['time'], int)
        assert isinstance(blockjson['mediantime'], int)
        assert isinstance(blockjson['nonce'], int)
        assert isinstance(blockjson['version'], int)
        assert isinstance(int(blockjson['versionHex'], 16), int)
        assert isinstance(blockjson['difficulty'], Decimal)
        assert isinstance(blockjson['tx'], list)
        for tx in blockjson['tx']:
            assert_is_hash_string(tx)

        self.log.info("Test getblockbyheight with verbosity=DECODE_HEADER")
        blockjson = node.getblockbyheight(200, "DECODE_HEADER")
        assert_equal(blockjson['hash'], besthash)
        for tx in blockjson['tx']:
            assert_is_hash_string(tx)

        self.log.info("Test getblockbyheight with verbosity=2")
        blockjson = node.getblockbyheight(200, 2)
        assert_equal(blockjson['hash'], besthash)
        assert_equal(blockjson['height'], 200)
        assert_equal(blockjson['confirmations'], 1)
        assert_equal(blockjson['previousblockhash'], secondbesthash)
        assert_is_hex_string(blockjson['chainwork'])
        assert_is_hash_string(blockjson['hash'])
        assert_is_hash_string(blockjson['previousblockhash'])
        assert_is_hash_string(blockjson['merkleroot'])
        assert_is_hash_string(blockjson['bits'], length=None)
        assert isinstance(blockjson['time'], int)
        assert isinstance(blockjson['mediantime'], int)
        assert isinstance(blockjson['nonce'], int)
        assert isinstance(blockjson['version'], int)
        assert isinstance(int(blockjson['versionHex'], 16), int)
        assert isinstance(blockjson['difficulty'], Decimal)
        for tx in blockjson['tx']:
            assert isinstance(tx, dict)

        self.log.info("Test getblockbyheight with verbosity=DECODE_TRANSACTIONS")
        blockjson = node.getblockbyheight(200, "DECODE_TRANSACTIONS")
        assert_equal(blockjson['hash'], besthash)
        for tx in blockjson['tx']:
            assert isinstance(tx, dict)

        self.log.info("Test getblockbyheight with verbosity=3")
        blockjson = node.getblockbyheight(200, 3)
        assert_equal(blockjson['hash'], besthash)
        assert_equal(blockjson['height'], 200)
        assert_equal(blockjson['confirmations'], 1)
        assert_equal(blockjson['previousblockhash'], secondbesthash)
        assert_is_hex_string(blockjson['chainwork'])
        assert_is_hash_string(blockjson['hash'])
        assert_is_hash_string(blockjson['previousblockhash'])
        assert_is_hash_string(blockjson['merkleroot'])
        assert_is_hash_string(blockjson['bits'], length=None)
        assert isinstance(blockjson['time'], int)
        assert isinstance(blockjson['mediantime'], int)
        assert isinstance(blockjson['nonce'], int)
        assert isinstance(blockjson['version'], int)
        assert isinstance(int(blockjson['versionHex'], 16), int)
        assert isinstance(blockjson['difficulty'], Decimal)
        #only coinbase tx should be in block
        assert_equal(len(blockjson['tx']), 1)
        tx = blockjson['tx'][0]
        assert isinstance(tx, dict)
        assert_is_hash_string(tx['vin'][0]['coinbase'], length=None)

        self.log.info("Test getblockbyheight with verbosity=DECODE_HEADER_AND_COINBASE")
        blockjson = node.getblockbyheight(200, "DECODE_HEADER_AND_COINBASE")
        assert_equal(blockjson['hash'], besthash)
        #only coinbase tx should be in block
        assert_equal(len(blockjson['tx']), 1)
        tx = blockjson['tx'][0]
        assert isinstance(tx, dict)
        assert_is_hash_string(tx['vin'][0]['coinbase'], length=None)

        self.log.info("Test getblock with invalid verbosity fails")
        assert_raises_rpc_error(-8, "Verbosity value out of range", node.getblockbyheight, 200, 4)
        assert_raises_rpc_error(-8, "Verbosity value out of range", node.getblockbyheight, 200, -1)
        assert_raises_rpc_error(-8, "Verbosity value not recognized", node.getblockbyheight, 200, "ASDFG")
        assert_raises_rpc_error(-8, "Block height out of range", node.getblockbyheight, -1)
        assert_raises_rpc_error(-8, "Block height out of range", node.getblockbyheight, 300)

    def _test_getblockheader(self):
        self.log.info("Test getblockheader")
        node = self.nodes[0]

        assert_raises_rpc_error(-5, "Block not found",
                                node.getblockheader, "nonsense")

        besthash = node.getbestblockhash()
        secondbesthash = node.getblockhash(199)
        header = node.getblockheader(besthash)

        assert_equal(header['hash'], besthash)
        assert_equal(header['height'], 200)
        assert_equal(header['confirmations'], 1)
        assert_equal(header['previousblockhash'], secondbesthash)
        assert_is_hex_string(header['chainwork'])
        assert_is_hash_string(header['hash'])
        assert_is_hash_string(header['previousblockhash'])
        assert_is_hash_string(header['merkleroot'])
        assert_is_hash_string(header['bits'], length=None)
        assert isinstance(header['time'], int)
        assert isinstance(header['mediantime'], int)
        assert isinstance(header['nonce'], int)
        assert isinstance(header['version'], int)
        assert isinstance(int(header['versionHex'], 16), int)
        assert isinstance(header['difficulty'], Decimal)

    def _test_getdifficulty(self):
        self.log.info("Test getdifficulty")
        difficulty = self.nodes[0].getdifficulty()
        # 1 hash in 2 should be valid, so difficulty should be 1/2**31
        # binary => decimal => binary math is why we do this check
        assert abs(difficulty * 2**31 - 1) < 0.0001

    def _test_getnetworkhashps(self):
        self.log.info("Test getnetworkhashps")
        hashes_per_second = self.nodes[0].getnetworkhashps()
        # This should be 2 hashes every 10 minutes or 1/300
        assert abs(hashes_per_second * 300 - 1) < 0.0001

    def _test_stopatheight(self):
        self.log.info("Test stopatheight")
        assert_equal(self.nodes[0].getblockcount(), 201)
        self.nodes[0].generate(5)
        assert_equal(self.nodes[0].getblockcount(), 206)
        self.log.debug('Node should not stop at this height')
        assert_raises(subprocess.TimeoutExpired,
                      lambda: self.nodes[0].process.wait(timeout=3))
        try:
            self.nodes[0].generate(1)
        except (ConnectionError, http.client.BadStatusLine):
            pass  # The node already shut down before response
        self.log.debug('Node should stop at this height...')
        self.nodes[0].wait_until_stopped()
        self.start_node(0)
        assert_equal(self.nodes[0].getblockcount(), 207)

    def _test_getblock_num_tx(self):
        self.log.info("Test num_tx is valid for getblock, getblockbyheight, getblockheader")
        num_tx_to_create = 10
        addr = self.nodes[0].getnewaddress()
        for _ in range(num_tx_to_create):
            self.nodes[0].sendtoaddress(addr, 0.1)
        self.sync_all()
        blockhash = self.nodes[0].generate(1)[0]
        blockheight = self.nodes[0].getblockcount()
        # getblock
        blockjson = self.nodes[0].getblock(blockhash, 1)
        assert_equal(len(blockjson['tx']), blockjson['num_tx'])
        blockjson = self.nodes[0].getblock(blockhash, 2)
        assert_equal(len(blockjson['tx']), blockjson['num_tx'])
        blockjson = self.nodes[0].getblock(blockhash, 3)
        assert_equal(len(blockjson['tx']), 1)
        assert_equal(blockjson['num_tx'], num_tx_to_create + 1)
        # getblockbyheight
        blockjson = self.nodes[0].getblockbyheight(blockheight, 1)
        assert_equal(len(blockjson['tx']), blockjson['num_tx'])
        blockjson = self.nodes[0].getblockbyheight(blockheight, 2)
        assert_equal(len(blockjson['tx']), blockjson['num_tx'])
        blockjson = self.nodes[0].getblockbyheight(blockheight, 3)
        assert_equal(len(blockjson['tx']), 1)
        assert_equal(blockjson['num_tx'], num_tx_to_create + 1)
        # getblockhash
        blockjson = self.nodes[0].getblockheader(blockhash)
        assert_equal(blockjson['num_tx'], num_tx_to_create + 1)


if __name__ == '__main__':
    BlockchainTest().main()
