#!/usr/bin/env python3
# Copyright (c) 2021  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from test_framework.blocktools import create_block, create_coinbase
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, ToHex, CTxIn, COutPoint, CTxOut, msg_block, COIN, mininode_lock
from test_framework.script import CScript, OP_DROP, OP_TRUE, OP_CHECKSIG, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, wait_for_ptv_completion, check_mempool_equals, assert_greater_than
from decimal import Decimal
import time
import threading

"""
The test aims to:

1. Test long chains of CPFP txs.
a) use chains of txs paying relay fee, where the last tx in the chain pays for itself and ancestors
   - submit txs in depth-first order
b) verify tx promotion from the secondary to the primary mempool
c) when the mempool is full verify tx eviction (by a newly received tx which pays a higher fee)
   - as a result tx chains are expelled from the primary and then from the secondary mempool

2. Measure time required to submit and process CPFP txs using the following interfaces:
a) p2p
b) rpc sendrawtransactions (bulk txs submit)
"""


class PtvCpfp(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

        self.genesisactivationheight = 150
        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(b"horsebattery")
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()
        self.locking_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])
        self.locking_script2 = CScript([b"X"*10, OP_DROP, OP_TRUE])
        self.default_args = ['-debug', '-mindebugrejectionfee=0.0000025', '-maxgenesisgracefulperiod=0', '-genesisactivationheight=%d' % self.genesisactivationheight, '-whitelist=127.0.0.1']
        self.extra_args = [self.default_args] * self.num_nodes

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def check_intersec_with_mempool(self, rpc, txs_set):
        return set(rpc.getrawmempool()).intersection(t.hash for t in txs_set)

    def check_mempool_with_subset(self, rpc, should_be_in_mempool, timeout=20):
        wait_until(lambda: {t.hash for t in should_be_in_mempool}.issubset(set(rpc.getrawmempool())), timeout=timeout)

    def send_txs(self, rpcsend, conn, txs, exp_mempool_size, timeout=300, check_interval=0.1):
        conn = None if rpcsend is not None else conn
        if conn is not None:
            req_start_time = time.time()
            for tx in txs:
                conn.send_message(msg_tx(tx))
            wait_for_ptv_completion(conn, exp_mempool_size, timeout=timeout, check_interval=check_interval)
            elapsed = time.time() - req_start_time
        elif rpcsend is not None:
            elapsed = self.rpc_send_txs(rpcsend, txs)
        else:
            raise Exception("Unspecified interface!")
        return elapsed

    def rpc_send_txs(self, rpcsend, txs):
        if "sendrawtransaction" == rpcsend._service_name:
            req_start_time = time.time()
            for tx in txs:
                rpcsend(ToHex(tx))
            elapsed = time.time() - req_start_time
        elif "sendrawtransactions" == rpcsend._service_name:
            rpc_txs_bulk_input = []
            for tx in txs:
                rpc_txs_bulk_input.append({'hex': ToHex(tx)})
            req_start_time = time.time()
            rpcsend(rpc_txs_bulk_input)
            elapsed = time.time() - req_start_time
        else:
            raise Exception("Unsupported rpc method!")
        return elapsed

    # Sign a transaction, using the key we know about.
    # This signs input 0 in tx, which is assumed to be spending output n in spend_tx
    def sign_tx(self, tx, spend_tx, n):
        sighash = SignatureHashForkId(
            spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
        tx.vin[0].scriptSig = CScript(
            [self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    def create_tx(self, outpoints, noutput, feerate, locking_script):
        tx = CTransaction()
        total_input = 0
        for parent_tx, n in outpoints:
            tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, n), b"", 0xffffffff))
            total_input += parent_tx.vout[n].nValue

        for _ in range(noutput):
            tx.vout.append(CTxOut(total_input//noutput, locking_script))

        tx.rehash()

        tx_size = len(tx.serialize())
        fee_per_output = int(tx_size * feerate // noutput)

        for output in tx.vout:
            output.nValue -= fee_per_output

        if locking_script == self.locking_script:
            for parent_tx, n in outpoints:
                self.sign_tx(tx, parent_tx, n)

        tx.rehash()
        return tx

    # Create a single tx chain.
    def create_txchain(self, spend, vout_idx, chain_length, tx_fee, locking_script):
        txs = []
        txs.append(self.create_tx([(spend, vout_idx)], 1, tx_fee, locking_script))
        for idx in range(chain_length-1):
            txs.append(self.create_tx([(txs[idx], 0)], 1, tx_fee, locking_script))
        return txs[chain_length-1], txs

    # Create a required number of tx chains (equal lengths) in parallel.
    def create_txchains(self, num_of_chains, chain_length, spend, tx_fee, locking_script):
        txchains = []
        last_descendant_from_each_txchain = []
        ths = []
        req_start_time = time.time()

        def make_chained_txs(chain_id, spend, chain_length, tx_fee, locking_script):
            self.log.info("Creating txchain[%d], length=%d", chain_id, chain_length)
            nonlocal txchains
            last_descendant_in_txchain, txchain = self.create_txchain(spend, chain_id, chain_length, tx_fee, locking_script)
            with mininode_lock:
                txchains += txchain
                last_descendant_from_each_txchain.append(last_descendant_in_txchain)
            self.log.info("txchain[%d] created", chain_id)
        # Make tx chains in parallel.
        for x in range(0, num_of_chains):
            th = threading.Thread(target=make_chained_txs, args=(x, spend, chain_length, tx_fee, locking_script,))
            ths.append(th)
            th.start()
        for _, th in enumerate(ths):
            th.join()
        self.log.info("The total time to generate all %d txchains (of length %d): %.6f sec",
                      num_of_chains,
                      chain_length,
                      time.time() - req_start_time)
        return last_descendant_from_each_txchain, txchains

    def create_fund_txn(self, conn, noutput, tx_fee, locking_script, pubkey=None):
        # create a new block with coinbase
        last_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
        coinbase = create_coinbase(height=last_block_info["height"]+1, pubkey=pubkey)
        new_block = create_block(int(last_block_info["hash"], 16), coinbase=coinbase, nTime=last_block_info["time"]+1)
        new_block.nVersion = last_block_info["version"]
        new_block.solve()
        conn.send_message(msg_block(new_block))
        wait_until(lambda: conn.rpc.getbestblockhash() == new_block.hash, check_interval=0.3)
        # mature the coinbase
        conn.rpc.generate(100)
        # create and send a funding txn
        funding_tx = self.create_tx([(coinbase, 0)], 2, 1.5, locking_script)
        conn.send_message(msg_tx(funding_tx))
        check_mempool_equals(conn.rpc, [funding_tx])
        conn.rpc.generate(1)
        # create a new txn which pays the specified tx_fee
        new_tx = self.create_tx([(funding_tx, 0)], noutput, tx_fee, locking_script)
        last_block_info = conn.rpc.getblock(conn.rpc.getbestblockhash())
        new_block = create_block(int(last_block_info["hash"], 16), coinbase=create_coinbase(height=last_block_info["height"]+1), nTime=last_block_info["time"]+1)
        new_block.nVersion = last_block_info["version"]
        new_block.vtx.append(new_tx)
        new_block.hashMerkleRoot = new_block.calc_merkle_root()
        new_block.calc_sha256()
        new_block.solve()

        conn.send_message(msg_block(new_block))
        wait_until(lambda: conn.rpc.getbestblockhash() == new_block.hash, check_interval=0.3)

        return new_tx

    # Submit all cpfp txs and measure overall time duration of this process.
    def run_cpfp_scenario1(self, conn, txchains, last_descendant_from_each_txchain, chain_length, num_of_chains, mining_fee, locking_script, rpcsend=None, timeout=240):
        #
        # Send low fee (paying relay_fee) txs to the node.
        #
        exp_mempool_size = conn.rpc.getmempoolinfo()['size'] + len(txchains)
        elapsed1 = self.send_txs(rpcsend, conn, txchains, exp_mempool_size, timeout)
        # Check if mempool contains all low fee txs.
        check_mempool_equals(conn.rpc, txchains, timeout)

        #
        # Check getminingcandidate result: There should be no cpfp txs in the block template, due to low fees.
        #
        wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == 1) # there should be coinbase tx only

        #
        # Create and send cpfp txs (paying mining_fee).
        #
        cpfp_txs_pay_for_ancestors = []
        for tx in last_descendant_from_each_txchain:
            cpfp_txs_pay_for_ancestors.append(self.create_tx([(tx, 0)], 2, (chain_length+1) * (mining_fee), locking_script))
        # Send cpfp txs.
        exp_mempool_size = conn.rpc.getmempoolinfo()['size'] + len(cpfp_txs_pay_for_ancestors)
        elapsed2 = self.send_txs(rpcsend, conn, cpfp_txs_pay_for_ancestors, exp_mempool_size, timeout)
        # Check if there is a required number of txs in the mempool.
        check_mempool_equals(conn.rpc, cpfp_txs_pay_for_ancestors + txchains, timeout)

        #
        # Check getminingcandidate result: There should be all cpfp txs (+ ancestor txs) in the block template.
        #
        wait_until(lambda: conn.rpc.getminingcandidate()["num_tx"] == len(cpfp_txs_pay_for_ancestors + txchains) + 1) # +1 a coinbase tx

        #
        # Collect stats.
        #
        interface_name = "p2p" if rpcsend is None else rpcsend._service_name
        self.log.info("[%s]: Submit and process %d txchains of length %d (%d relay_fee std txs) [time duration: %.6f sec]",
                      interface_name,
                      num_of_chains,
                      chain_length,
                      num_of_chains*chain_length,
                      elapsed1)
        self.log.info("[%s]: Submit and process %d cpfp std txs (each pays mining_fee) [time duration: %.6f sec]",
                      interface_name,
                      len(cpfp_txs_pay_for_ancestors), elapsed2)
        self.log.info("[%s]: Total time to submit and process %d std txs took %.6f sec",
                      interface_name,
                      num_of_chains*chain_length + len(cpfp_txs_pay_for_ancestors),
                      elapsed1 + elapsed2)

        return elapsed1 + elapsed2

    # Submit txs paying a higher fee than any other tx present in the mempool:
    # - at this stage it is expected that the mempool is full
    # - this process triggers mempool's eviction (for both the primary and the secondary mempools)
    # - 'mempoolminfee' is set to a non zero value
    def run_cpfp_scenario1_override_txs(self, conn, high_fee_tx, mining_fee, locking_script, rpcsend=None, timeout=240):
        txs = []
        rolling_fee = mining_fee+1000
        tx0 = self.create_tx([(high_fee_tx, 0)], 1, rolling_fee, locking_script)
        tx0_size = len(tx0.serialize())
        # send tx0 over rpc interface to trigger eviction.
        self.rpc_send_txs(conn.rpc.sendrawtransactions, [tx0])
        # calculate rolling fee to satisfy current 'mempoolminfee' requirements.
        rolling_fee = float(conn.rpc.getmempoolinfo()["mempoolminfee"] * COIN) * float(tx0_size/1024)
        # send the rest of txs.
        for idx in range(1, len(high_fee_tx.vout)):
            txs.append(self.create_tx([(high_fee_tx, idx)], 1, rolling_fee, locking_script))
        exp_min_mempool_size = conn.rpc.getmempoolinfo()['size']
        self.send_txs(rpcsend, conn, txs, exp_min_mempool_size, timeout)
        txs.append(tx0)
        # All newly submitted txs should be present in the mempool.
        self.check_mempool_with_subset(conn.rpc, txs)
        return txs

    def test_case1(self, timeout=300):
        # Node's config
        args = ['-txnvalidationasynchrunfreq=0',
                '-limitancestorcount=1001',
                '-minminingtxfee=0.00001',
                '-maxorphantxsize=600MB',
                '-maxmempoolsizedisk=0',
                '-disablebip30checks=1',
                '-checkmempool=0',
                '-persistmempool=0',
                # CPFP config params:
                '-mempoolmaxpercentcpfp=100',
                '-limitcpfpgroupmemberscount=1001']
        with self.run_node_with_connections("Scenario 1: Long cpfp std tx chains, non-whitelisted peer",
                                            0,
                                            ['-maxmempool=600MB'] + args + self.default_args,
                                            number_of_connections=1) as (conn,):

            mining_fee = 1.01 # in satoshi per byte
            relay_fee = float(Decimal("0.0000025") * COIN / 1000) + 0.15  # in satoshi per byte

            # Create a low and high fee txn.
            low_fee_std_tx = self.create_fund_txn(conn, 300, relay_fee, self.locking_script, pubkey=self.coinbase_pubkey)
            high_fee_nonstd_tx = self.create_fund_txn(conn, 30000, mining_fee, self.locking_script2)

            self.stop_node(0)
            # Prevent RPC timeout for sendrawtransactions call that take longer to return.
            self.nodes[0].rpc_timeout = 600

            # Generate low fee cpfp std txn chains:
            # - 300K txs: 300 chains of length 1000
            txchain_length = 1000
            num_of_txchains = 300
            last_descendant_from_each_txchain, txchains = self.create_txchains(num_of_txchains, txchain_length, low_fee_std_tx, relay_fee, self.locking_script)

            #
            # Check max memory usage (required by the next test cases) on the running platform.
            #
            mempool_usage = 0
            TC_1_0_msg = "TC_1_0: Check memory usage needed to store {} txs (num_of_chains= {}, chain_length= {}) in the mempool."
            with self.run_node_with_connections(TC_1_0_msg.format(txchain_length*num_of_txchains, num_of_txchains, txchain_length),
                                                0,
                                                ['-maxmempool=600MB'] + args + self.default_args,
                                                number_of_connections=1) as (conn,):
                rpc = conn.rpc
                self.run_cpfp_scenario1(conn,
                                        txchains,
                                        last_descendant_from_each_txchain,
                                        txchain_length, num_of_txchains,
                                        mining_fee,
                                        self.locking_script,
                                        rpc.sendrawtransactions,
                                        timeout=timeout)
                # The mempool usage when 300300 std txs are accepted
                mempool_usage = conn.rpc.getmempoolinfo()['usage']
                self.log.info("mempool_usage= %dB", mempool_usage)

            #
            # Send txs through P2P interface.
            #
            TC_1_1_msg = "TC_1_1: Send {} txs (num_of_txchains= {}, txchain_length= {}) through P2P interface"
            with self.run_node_with_connections(TC_1_1_msg.format(txchain_length*num_of_txchains, num_of_txchains, txchain_length),
                                                0,
                                                ['-maxmempool=%dB' % mempool_usage] + args + self.default_args,
                                                number_of_connections=1) as (conn,):
                self.run_cpfp_scenario1(conn,
                                        txchains,
                                        last_descendant_from_each_txchain,
                                        txchain_length,
                                        num_of_txchains,
                                        mining_fee,
                                        self.locking_script,
                                        timeout=timeout)
                # Uses high_fee_nonstd_tx to generate 30K high fee nonstandard txs
                self.run_cpfp_scenario1_override_txs(conn, high_fee_nonstd_tx, mining_fee, self.locking_script2, timeout=timeout)

            #
            # Send txs through sendrawtransactions rpc interface (a bulk submit).
            #
            TC_1_2_msg = "TC_1_2: Send {} txs (num_of_chains= {}, chain_length= {}) through RPC interface (a bulk submit)"
            with self.run_node_with_connections(TC_1_2_msg.format(txchain_length*num_of_txchains, num_of_txchains, txchain_length),
                                                0,
                                                ['-maxmempool=%dB' % mempool_usage] + args + self.default_args,
                                                number_of_connections=1) as (conn,):
                rpc = conn.rpc
                self.run_cpfp_scenario1(conn,
                                        txchains,
                                        last_descendant_from_each_txchain,
                                        txchain_length,
                                        num_of_txchains,
                                        mining_fee,
                                        self.locking_script,
                                        rpc.sendrawtransactions,
                                        timeout=timeout)
                # Uses high_fee_nonstd_tx to generate 30K high fee nonstandard txs
                self.run_cpfp_scenario1_override_txs(conn, high_fee_nonstd_tx, mining_fee, self.locking_script2, rpc.sendrawtransactions, timeout=timeout)

    def run_test(self):
        # Test long chains of cpfp txs.
        self.test_case1(timeout=7200)


if __name__ == '__main__':
    PtvCpfp().main()
