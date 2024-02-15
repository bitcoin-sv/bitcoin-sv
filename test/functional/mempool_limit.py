#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

# Test mempool limiting together/eviction with the wallet

# 1. Send transaction (cca 5MB) with smaller fee rate (0.00000399 BSV/kB).
# 2. Send 29 big transactions (cca 10MB per one) with higher fee rate (0.00000999 BSV/kB).
# 3. Send another transaction with the same size as the first one and higher fee rate (0.00001999 BSV/kB).
# 4. Mempool is full when the last transaction arrives to the mempool - the first one should be evicted (replaced with the last one) because of the smaller fee rate.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import create_confirmed_utxos, satoshi_round, bytes_to_hex_str, assert_equal
import decimal


def send_tx_with_data(node, utxo, fee, data_size):
    send_value = utxo['amount'] - fee
    inputs = []
    inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
    outputs = {}
    addr = node.getnewaddress()
    outputs[addr] = satoshi_round(send_value)
    outputs["data"] = bytes_to_hex_str(bytearray(data_size))
    rawTxn = node.createrawtransaction(inputs, outputs)
    signedTxn = node.signrawtransaction(rawTxn)["hex"]

    return node.sendrawtransaction(signedTxn)


class MempoolLimitTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-maxmempool=300", "-maxmempoolsizedisk=0", "-spendzeroconfchange=0", "-genesisactivationheight=1", "-maxtxsizepolicy=0", "-mindebugrejectionfee=0.0000025"]]

    def run_test(self):
        relayfee = decimal.Decimal("0.0000025")
        utxos = create_confirmed_utxos(relayfee, self.nodes[0], 40)
        total_number_of_transactions = 30

        # create a mempool transaction that will be evicted (smaller fee rate)
        # size: 5000211B, fee: 2000000 satoshi (0.02 BSV) --> fee rate: 0.399 sat/byte which is 0.00000399 BSV/kB
        small_fee = decimal.Decimal('0.02')
        small_data_size = 5000000
        firstTxId = send_tx_with_data(self.nodes[0], utxos.pop(), small_fee, small_data_size)

        assert(firstTxId in self.nodes[0].getrawmempool())
        self.log.info("First transaction %s successfully accepted to mempool.", firstTxId)

        # transactions with higher fee rate
        # size: 10000211B, fee: 10000000 satoshi (0.1 BSV) --> fee rate: 0.999 sat/byte which is 0.00000999 BSV/kB
        big_fee = decimal.Decimal('0.1')
        big_data_size = 10000000
        for i in range(total_number_of_transactions - 1):
            send_tx_with_data(self.nodes[0], utxos.pop(), big_fee, big_data_size)

        assert_equal(len(self.nodes[0].getrawmempool()), total_number_of_transactions)
        self.log.info("%d big transactions successfully arrived to mempool.", total_number_of_transactions - 1)

        # At this point, mempool size is something more than 295 000 000 bytes.
        # If we send another transaction with size more than 5 MB and the highest fee rate, it should be replaced with the first one.

        # transaction with the highest fee rate, the same size as the first one
        # size: 5000211B, fee: 10000000 satoshi (0.1 BSV) --> fee rate: 1.999 sat/byte which is 0.00001999 BSV/kB
        lastTxId = send_tx_with_data(self.nodes[0], utxos.pop(), big_fee, small_data_size)

        # by now, the first transaction should be evicted, check confirmation state
        assert(firstTxId not in self.nodes[0].getrawmempool())
        # last transaction should be in mempool because it has the highest fee
        assert(lastTxId in self.nodes[0].getrawmempool())

        self.log.info("First transaction %s evicted from mempool. Last sent transaction %s successfully accepted to mempool.", firstTxId, lastTxId)
        assert_equal(len(self.nodes[0].getrawmempool()), total_number_of_transactions)

        txdata = self.nodes[0].gettransaction(firstTxId)
        assert(txdata['confirmations'] == 0)  # confirmation should still be 0


if __name__ == '__main__':
    MempoolLimitTest().main()
