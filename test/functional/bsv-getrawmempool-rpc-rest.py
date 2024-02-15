#!/usr/bin/env python3
# Copyright (c) 2020  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import create_transaction
from test_framework.test_framework import BitcoinTestFramework, ChainManager
from test_framework.mininode import msg_block, msg_tx, CTransaction, CTxIn, COutPoint, CTxOut
from test_framework.util import assert_equal, wait_until, json
from test_framework.script import CScript, OP_TRUE, OP_RETURN

import http.client
import urllib.parse


def http_get_call(host, port, path, response_object=0):
    conn = http.client.HTTPConnection(host, port)
    conn.request('GET', path)

    if response_object:
        return conn.getresponse()

    return conn.getresponse().read().decode('utf-8')


class GetRawMempoolTest(BitcoinTestFramework):
    FORMAT_SEPARATOR = "."

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-genesisactivationheight=1']]
        self.chain = ChainManager()

    def check_mempool(self, rpc, should_be_in_mempool):
        wait_until(lambda: {t.hash for t in should_be_in_mempool}.issubset(set(rpc.getrawmempool())), timeout=6000)

    def createLargeTransaction(self, size, depends):
        tx = CTransaction()
        for depend in depends:
            tx.vin.append(CTxIn(COutPoint(depend.sha256, 0), b''))
        tx.vout.append(CTxOut(int(100), CScript([OP_RETURN,  b"a" * size])))
        tx.rehash()
        return tx

    def check_fieldsMempoolEntry(self, mempoolEntry):
        assert 'size' in mempoolEntry
        assert 'fee' in mempoolEntry
        assert 'modifiedfee' in mempoolEntry
        assert 'time' in mempoolEntry
        assert 'height' in mempoolEntry
        assert_equal(self.nodes[0].getblock(self.nodes[0].getbestblockhash())['height'], mempoolEntry['height'])
        assert 'depends' in mempoolEntry

    def check_getRawMempool(self, mempool, transactions):
        for transaction in transactions:
            assert transaction.hash in mempool
            self.check_fieldsMempoolEntry(mempool[transaction.hash])

    def run_test(self):

        url = urllib.parse.urlparse(self.nodes[0].url)

        self.stop_node(0)
        with self.run_node_with_connections("test getrawMempool RPC call", 0, [], 1) as connections:

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
            txSize = 1000
            largeTx = self.createLargeTransaction(txSize, transactions)
            connection.cb.send_message(msg_tx(largeTx))
            self.check_mempool(self.nodes[0], [largeTx])

            # getrawmempool, verbosity = False
            assert_equal(set(self.nodes[0].getrawmempool()), set([tx.hash for tx in transactions] + [largeTx.hash]))

            # getrawmempool, verbosity = True
            mempool = self.nodes[0].getrawmempool(True)
            self.check_getRawMempool(mempool, transactions + [largeTx])
            assert_equal(mempool[transactions[0].hash]["depends"], [])
            assert_equal(mempool[largeTx.hash]["depends"], [tx.hash for tx in transactions])
            assert_equal(mempool[largeTx.hash]["size"] > txSize, True)

            # /rest/mempool/contents REST call
            json_string = http_get_call(url.hostname,
                                        url.port,
                                        '/rest/mempool/contents' + self.FORMAT_SEPARATOR + 'json')
            json_obj = json.loads(json_string)
            self.check_getRawMempool(json_obj, transactions + [largeTx])
            assert_equal(mempool[transactions[0].hash]["depends"], [])
            assert_equal(mempool[largeTx.hash]["depends"], [tx.hash for tx in transactions])
            assert_equal(mempool[largeTx.hash]["size"] > txSize, True)

            # getmempooldescendants, verbosity = False
            assert_equal(self.nodes[0].getmempooldescendants(transactions[0].hash), [largeTx.hash])
            # getmempooldescendants, verbosity = True
            mempoolDescendants = self.nodes[0].getmempooldescendants(transactions[0].hash, True)
            self.check_getRawMempool(mempoolDescendants, [largeTx])
            assert_equal(mempool[largeTx.hash], mempoolDescendants[largeTx.hash])

            # getmempoolancestors, verbosity = False
            assert_equal(set(self.nodes[0].getmempoolancestors(largeTx.hash)), set([tx.hash for tx in transactions]))
            # getmempoolancestors, verbosity = True
            mempoolAncestors = self.nodes[0].getmempoolancestors(largeTx.hash, True)
            self.check_getRawMempool(mempoolAncestors, transactions)
            assert_equal(mempool[transactions[0].hash], mempoolAncestors[transactions[0].hash])

            # getmempoolentry
            largeTxEntry = self.nodes[0].getmempoolentry(largeTx.hash)
            self.check_fieldsMempoolEntry(largeTxEntry)
            assert_equal(mempool[largeTx.hash], largeTxEntry)

            # Test those calls in a batch
            batch = self.nodes[0].batch([
                self.nodes[0].getrawmempool.get_request(True),
                self.nodes[0].getrawmempool.get_request(),
                self.nodes[0].getmempooldescendants.get_request(transactions[0].hash, True),
                self.nodes[0].getmempoolancestors.get_request(largeTx.hash),
                self.nodes[0].getmempoolentry.get_request(largeTx.hash)])

            assert_equal(batch[0]["error"], None)
            assert_equal(batch[0]["result"], mempool)
            assert_equal(batch[1]["error"], None)
            assert_equal(batch[2]["result"][largeTx.hash], mempool[largeTx.hash])
            assert_equal(batch[2]["error"], None)
            assert_equal(batch[3]["error"], None)
            assert_equal(batch[4]["error"], None)


if __name__ == '__main__':
    GetRawMempoolTest().main()
