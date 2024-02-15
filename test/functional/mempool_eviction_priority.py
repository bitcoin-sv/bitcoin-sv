#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Test mempool eviction based on transaction fee

# 1. Fill 90% of the mempool with transactions with a high fee
# 2. Fill 10% of the mempool with transactions with a lower fee
# 3. Send a large transaction (15% of mempool) that  has a lower fee than
#    most of the transactions in the pool
# 4. See what happens...

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.cdefs import ONE_MEGABYTE
from test_framework.util import bytes_to_hex_str, create_confirmed_utxos, satoshi_round
from test_framework.util import assert_equal, assert_raises_rpc_error
import decimal
import random


def send_tx_with_data(node, utxo, fee, data_size):
    assert(data_size > 24)
    send_value = utxo['amount'] - fee
    inputs = []
    inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
    outputs = {}
    addr = node.getnewaddress()
    outputs[addr] = satoshi_round(send_value)
    data = bytearray(random.getrandbits(8) for _ in range(24)) + bytearray(data_size - 24)
    outputs["data"] = bytes_to_hex_str(data)
    rawTxn = node.createrawtransaction(inputs, outputs)
    signedTxn = node.signrawtransaction(rawTxn)["hex"]
    return node.sendrawtransaction(signedTxn)


class MempoolEvictionPriorityTest(BitcoinTestFramework):
    mempool_size = 300
    total_number_of_transactions = 50

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-maxmempool={}".format(self.mempool_size),
                            "-maxmempoolsizedisk=0",
                            "-spendzeroconfchange=0",
                            "-genesisactivationheight=1",
                            "-maxtxsizepolicy=0",
                            "-mindebugrejectionfee=0.0000025",
                            '-maxtxfee=1.0']]

    def run_test(self):
        transaction_overhead = 2048
        mempool_size = self.mempool_size
        total_number_of_transactions = self.total_number_of_transactions
        number_of_good_transactions = total_number_of_transactions * 90 // 100
        number_of_cheap_transactions = total_number_of_transactions - number_of_good_transactions
        last_transaction_factor = total_number_of_transactions * 15 // 100
        transaction_size = mempool_size * ONE_MEGABYTE // total_number_of_transactions - transaction_overhead

        relayfee = decimal.Decimal("0.0000025")
        utxos = create_confirmed_utxos(relayfee, self.nodes[0], total_number_of_transactions + 1)

        # Transactions with higher fee rate
        # size: 6MiB, fee: 10,000,000 satoshi (0.1 BSV) --> fee rate: 1.6 sat/byte
        good_fee = decimal.Decimal('0.1')
        good_txids = []
        for i in range(number_of_good_transactions):
            txid = send_tx_with_data(self.nodes[0], utxos.pop(), good_fee, transaction_size)
            self.log.debug("Inserted good transaction %d %s", i + 1, txid)
            good_txids.append(txid)

        assert_equal(len(self.nodes[0].getrawmempool()), number_of_good_transactions)
        self.log.info("%d transactions successfully arrived to mempool.", number_of_good_transactions)

        # Transactions with lower fee rate
        # size: 6MiB, fee: 2,500,000 satoshi (0.025 BSV) --> fee rate: 0.4 sat/byte
        cheap_fee = good_fee / 4
        cheap_txids = []
        for i in range(number_of_cheap_transactions):
            txid = send_tx_with_data(self.nodes[0], utxos.pop(), cheap_fee, transaction_size)
            self.log.debug("Inserted cheap transaction %d %s", i + 1, txid)
            cheap_txids.append(txid)

        assert_equal(len(self.nodes[0].getrawmempool()), total_number_of_transactions)
        self.log.info("%d transactions successfully arrived to mempool.", total_number_of_transactions)

        # The mempool should now be full. Insert the last, large transaction
        # size: 42MiB, fee: 35,000,000 satoshi (0.35 BSV) --> fee rate: 0.8 sat/byte
        self.log.info("Inserting last transaction")
        last_fee = last_transaction_factor * good_fee / 2
        last_size = last_transaction_factor * transaction_size
        assert_raises_rpc_error(
            -26, 'mempool full',
            send_tx_with_data, self.nodes[0], utxos.pop(), last_fee, last_size)

        # Now let's see what happens. There should be no cheap transactions in the pool any more.
        mempool = self.nodes[0].getrawmempool()
        assert_equal(len(mempool), number_of_good_transactions)
        self.log.info("%d transactions were evicted.", total_number_of_transactions - len(mempool))

        for txid in cheap_txids:
            assert(txid not in mempool)
        self.log.info("All transactions with insufficient fee were evicted.")


if __name__ == '__main__':
    MempoolEvictionPriorityTest().main()
