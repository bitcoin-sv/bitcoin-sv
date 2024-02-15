#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
from collections import defaultdict

from test_framework.blocktools import create_block, create_coinbase, create_transaction
from test_framework.mininode import msg_block, msg_tx, ToHex, CTransaction, CTxIn, COutPoint, CTxOut, COIN
from test_framework.script import CScript, OP_CHECKSIG, OP_TRUE, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, \
    OP_FALSE, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, try_rpc, loghash
from test_framework.cdefs import DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION

# For Release build with sanitizers enabled (TSAN / ASAN / UBSAN), recommended timeoutfactor is 2.
# For Debug build, recommended timeoutfactor is 2.
# For Debug build with sanitizers enabled, recommended timeoutfactor is 3.


class TxCollection:

    def __init__(self, height, label):
        self._height = height
        self._label = label
        self.block_valid_txs = []
        self.block_invalid_txs = []
        self.p2p_valid_txs = []
        self.p2p_invalid_txs = []
        self.mempool_txs = []

    @property
    def height(self):
        return self._height

    @property
    def label(self):
        return self._label

    def add_tx(self, tx, p2p_reject_reason=None, block_reject_reason=None):
        if p2p_reject_reason:
            self.p2p_invalid_txs.append((tx, p2p_reject_reason))
        else:
            self.p2p_valid_txs.append(tx)

        if block_reject_reason:
            assert p2p_reject_reason is not None, "If the tx is rejected by block it should be rejected by p2p as well!"
            self.block_invalid_txs.append((tx, block_reject_reason))
        else:
            self.block_valid_txs.append(tx)

    def add_mempool_tx(self, tx):
        self.mempool_txs.append(tx)


class HeightBasedTestsCase:
    RUN = False
    NAME = None

    COINBASE_KEY = None
    ADDITIONAL_CONNECTIONS = []
    ARGS = []
    P2P_ACCEPT_TIMEOUT = 20

    _UTXO_KEY = None
    _NUMBER_OF_UTXOS_PER_HEIGHT = 24
    TESTING_HEIGHTS = [(150,   None, "PREPARE"),
                       (150, "TEST", None),]

    def __init__(self):
        self.utxos = defaultdict(list)

    def prepare_for_test(self, height, label, coinbases, connections):
        transactions = []
        n_generated_utxos = 0
        while n_generated_utxos < self._NUMBER_OF_UTXOS_PER_HEIGHT:
            tx_to_spend = coinbases()

            tx = CTransaction()
            transactions.append(tx)
            tx.vin.append(CTxIn(COutPoint(tx_to_spend.sha256, 0), b''))

            locking_key = self._UTXO_KEY
            cscript = CScript([locking_key.get_pubkey(), OP_CHECKSIG]) if locking_key else CScript([OP_TRUE])
            for x in range(24):
                coinbaseoutput = CTxOut(2 * COIN, cscript)
                tx.vout.append(coinbaseoutput)
                n_generated_utxos += 1

            if self.COINBASE_KEY:
                tx.rehash()
                sighash = SignatureHashForkId(tx_to_spend.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID,
                                              tx_to_spend.vout[0].nValue)
                sig = self._coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))
                tx.vin[0].scriptSig = CScript([sig])
            else:
                tx.vin[0].scriptSig = CScript([OP_TRUE])

            tx.rehash()
            self.log.info(f"Created UTXO Tx {loghash(tx.hash)} with {n_generated_utxos} outputs")

        return transactions, None

    def prepare_utxos_for_test(self, height, label, coinbases, connections):
        utxos = []
        txs, additional_data = self.prepare_for_test(height, label, coinbases, connections)
        for tx in txs:
            for inp in tx.vin:
                pass # TODO CHECK IF INPUTS are in utxos, remove them if they are
            utxos.extend((ndx, tx) for ndx in range(len(tx.vout)))
        self.utxos[label] = utxos, additional_data
        return txs

    def pre_test(self, height, label, coinbases, connections):
        pass

    def get_transactions_for_test(self, tx_collection, coinbases):
        raise Exception("OVERRIDE THIS")

    def post_test(self, height, label, coinbases, connections):
        pass


class SimpleTestDefinition:

    def __init__(self, utxo_label, locking_script, label, unlocking_script,
                 p2p_reject_reason=None, block_reject_reason=None, test_tx_locking_script=None,
                 post_utxo_tx_creation=lambda tx: tx, post_test_tx_creation=lambda tx: tx):
        self.scenario = None
        self.label = label
        self.locking_script = locking_script
        self.unlocking_script = unlocking_script
        self.utxo_label = utxo_label
        self.p2p_reject_reason = p2p_reject_reason
        self.block_reject_reason = block_reject_reason
        self.test_tx_locking_script = test_tx_locking_script or CScript([OP_FALSE, OP_RETURN])
        self.funding_tx = None
        self.test_tx = None
        self.post_utxo_tx_creation = post_utxo_tx_creation
        self.post_test_tx_creation = post_test_tx_creation

    def make_utxo(self, parent_tx, output_ndx):
        self.funding_tx = create_transaction(parent_tx,
                                             output_ndx,
                                             CScript(),
                                             parent_tx.vout[output_ndx].nValue - (500 + len(self.locking_script)),
                                             self.locking_script)
        return self.post_utxo_tx_creation(self.funding_tx)

    def make_test_tx(self):
        unlocking_script = b'' if callable(self.unlocking_script) else self.unlocking_script
        self.test_tx = create_transaction(self.funding_tx,
                                          0,
                                          unlocking_script,
                                          self.funding_tx.vout[0].nValue - len(unlocking_script) - len(self.test_tx_locking_script) - 2000,
                                          self.test_tx_locking_script)
        if callable(self.unlocking_script):
            self.test_tx.vin[0].scriptSig = self.unlocking_script(self.test_tx, self.funding_tx)
        return self.post_test_tx_creation(self.test_tx)


class SimpleTestScenarioDefinition(SimpleTestDefinition):
    def __init__(self, scenario, utxo_label, locking_script, label, unlocking_script,
                 p2p_reject_reason=None, block_reject_reason=None, test_tx_locking_script=None,
                 post_utxo_tx_creation=lambda tx: tx, post_test_tx_creation=lambda tx: tx):
        super(SimpleTestScenarioDefinition, self).__init__(utxo_label, locking_script, label, unlocking_script,
                                                           p2p_reject_reason, block_reject_reason, test_tx_locking_script,
                                                           post_utxo_tx_creation, post_test_tx_creation)
        self.scenario = scenario


class HeightBasedSimpleTestsCase(HeightBasedTestsCase):

    def prepare_for_test(self, height, label, coinbases, connections):
        txs = []
        for t in self.TESTS:
            if t.utxo_label is not None and t.utxo_label == label:
                tx = t.make_utxo(coinbases(), 0)
                tx.rehash()
                self.log.info(f"Created UTXO Tx {loghash(tx.hash)} for scenario: \"{t.scenario or self.NAME}\" and for utxo_label: {t.utxo_label}")
                txs.append(tx)
        return txs, None

    def get_transactions_for_test(self, tx_collection, coinbases):
        for t in self.TESTS:
            if t.label == tx_collection.label:
                if t.utxo_label is None:
                    utxo_tx = t.make_utxo(coinbases(), 0)
                    utxo_tx.rehash()
                    tx_collection.add_mempool_tx(utxo_tx)
                tx = t.make_test_tx()
                tx.rehash()
                self.log.info(f"Created Test Tx {loghash(tx.hash)} for scenario: \"{t.scenario or self.NAME}\" and for test_label: {t.label}")
                tx_collection.add_tx(tx, t.p2p_reject_reason, t.block_reject_reason)


class SimplifiedTestFramework(BitcoinTestFramework):

    def __init__(self, genesis_tests):
        self._gen_tests = genesis_tests
        self._coinbases = []
        self._current_test = None
        self.coinbase_pubkey = None
        self.tip_time = None
        super(SimplifiedTestFramework, self).__init__()

    def set_test_params(self):
        self.num_nodes = 1
        self.num_peers = 0
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)
        self.start_node(0)

    def _new_block(self, connection, tip_hash, tip_height, txs=[]):
        #coinbase_pubkey = self._current_test.COINBASE_KEY.get_pubkey() if self._current_test.COINBASE_KEY else None
        coinbase_tx = create_coinbase(tip_height + 1, self.coinbase_pubkey)
        coinbase_tx.rehash()
        self._coinbases.append(coinbase_tx)

        block = create_block(int(tip_hash, 16), coinbase_tx, self.tip_time)

        self.tip_time += 1
        if txs:
            block.vtx.extend(txs)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.calc_sha256()
        block.solve()

        connection.send_message(msg_block(block))

        return block, coinbase_tx

    def _make_first_block(self, connection):
        tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
        if self.tip_time is None:
            self.tip_time = tip["time"]+1
        else:
            self.tip_time += 1

        block, coinbase_tx = self._new_block(connection, tip_hash=tip["hash"], tip_height=tip["height"])
        self.tip_time + 1
        wait_until(lambda: connection.rpc.getbestblockhash() == block.hash, timeout=10, check_interval=0.2)
        return block, coinbase_tx

    def _new_block_check_reject(self, connection, txs, reasons, label="", block_txs=[]):
        tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
        rejects = []

        def on_reject(conn, msg):
            rejects.append(msg)

        with connection.cb.temporary_override_callback(on_reject=on_reject):
            for tx, reason in zip(txs, reasons):
                del rejects[:]
                block, _ = self._new_block(connection, tip_hash=tip["hash"], tip_height=tip["height"], txs=block_txs+[tx])

                if tx:
                    blk_ht = tip["height"]
                    self.log.info(f"Created a block {loghash(block.hash)} to check rejects at height {blk_ht + 1}")
                    for txn in block_txs+[tx]:
                        self.log.info(f".... and added transactions to block {loghash(txn.hash)}")

                wait_until(lambda: len(rejects) == 1 and rejects[0].data == block.sha256,
                           timeout=10, check_interval=0.2,
                           label=f"Waiting for block with tx {tx.hash[8]}... and reject reason '{reason}' to be rejected " + label)
                if reason:
                    self.log.info(f"Expect the block {loghash(block.hash)} to be rejected")
                    assert rejects[0].reason.startswith(reason), f"mismatching rejection reason: got {rejects[0].reason} expected {reason}"

    def _new_block_check_accept(self, connection, txs=[], label="", remove_accepted_block=False):
        tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
        block, coinbase_tx = self._new_block(connection, tip_hash=tip["hash"], tip_height=tip["height"], txs=txs)
        wait_until(lambda: connection.rpc.getbestblockhash() == block.hash,
                   timeout=60, check_interval=0.2, label="Waiting for block to become current tip " + label)

        blk_ht = connection.rpc.getblock(connection.rpc.getbestblockhash())["height"]
        if txs:
            self.log.info(f"Created a block {loghash(block.hash)} to check accepts at height {blk_ht}")
            for txn in txs:
                self.log.info(f".... and added transactions to block {loghash(txn.hash)}")
        if remove_accepted_block:
            self.log.info(f"Invalidating the block {loghash(block.hash)} at height {blk_ht}")
            connection.rpc.invalidateblock(block.hash)
        return coinbase_tx

    def _advance_in_main_chain(self, n_blocks, connection):
        tip = connection.rpc.getblock(connection.rpc.getbestblockhash())

        tip_hash = tip["hash"]
        tip_height = tip["height"]

        coinbases = []

        for count in range(n_blocks):
            block, coinbase_tx = self._new_block(connection, tip_hash, tip_height)
            coinbases.append(coinbase_tx)
            tip_height += 1
            tip_hash = block.hash

        # Observed data shows that during testing in debug mode it takes about 0.1s to process one block
        # We use double the amount of 0.2s plus extra 30s for good measure
        wait_until(lambda: connection.rpc.getbestblockhash() == block.hash,
                   timeout=0.2*n_blocks+30, check_interval=0.2, label="Waiting for block to become current tip")

        return coinbases, block

    def check_mp(self):
        mempool = self.nodes[0].rpc.getrawmempool()
        for tx in mempool:
            self.log.info(f"Tx {loghash(tx)} is in mempool")

    def _process_rpc_rejects(self, connection, to_reject, reasons, error_codes, test_label, height_label):
        for tx, reason, error_code in zip(to_reject, reasons, error_codes):
            exception_rised = try_rpc(error_code, reason, connection.rpc.sendrawtransaction, ToHex(tx))
            assert exception_rised, f"Exception should be raised for {test_label}, {height_label}"

    def _process_rpc_accepts(self, connection, to_accept):
        for tx in to_accept:
            connection.rpc.sendrawtransaction(ToHex(tx)) # will raise if not successful

    def _process_p2p_rejects(self, connection, to_reject, reasons, test_label, height_label):
        rejects = []

        def on_reject(_, msg):
            rejects.append(msg)

        with connection.cb.temporary_override_callback(on_reject=on_reject):
            for tx, reason in zip(to_reject, reasons):
                self.log.info(f"Sending and processing the reject tx {loghash(tx.hash)} for expecting reason {reason}")
                del rejects[:]
                connection.send_message(msg_tx(tx))
                wait_until(lambda: (len(rejects) == 1) and rejects[0].data == tx.sha256,
                           timeout=30, check_interval=0.2,
                           label=f"Waiting tx to be rejected. Reason {reason} At {test_label} {height_label} tx:{tx.hash}")
                if reason:
                    assert rejects[0].reason == reason, f"Mismatching rejection reason: got {rejects[0].reason} expected {reason}"
                    self.log.info(f"Tx {loghash(tx.hash)} is rejected as expected for reason {reason}")

    def _process_p2p_accepts(self, connection, to_accept, test_label, height_label, p2p_accept_timeout):
        # self.log.info(f"Max timeout set={p2p_accept_timeout}")
        for tx in to_accept:
            self.log.info(f"Sending and processing the accept tx {loghash(tx.hash)}")
            connection.send_message(msg_tx(tx))

        def tt():
            mempool = connection.rpc.getrawmempool()
            return all((t.hash in mempool) for t in to_accept)

        wait_until(tt,
                   timeout=(p2p_accept_timeout * self.options.timeoutfactor), check_interval=0.2,
                   label=f"Waiting txs to be accepted. At {test_label} {height_label} tx:{','.join(tx.hash[:8]+'...' for tx in to_accept)}")
        self.check_mp()

    def _assert_height(self, connection, desired_height):
        real_height = connection.rpc.getblock(connection.rpc.getbestblockhash())["height"]
        assert real_height == desired_height, f"Unexpected height current ={real_height}, expected={desired_height}"

    def _get_new_coinbase(self):
        return self._coinbases.pop(0)

    def _do_test_at_height(self, conn, test, label, height, additional_conns, prepare_label):

        self.log.info(f"Testing at height {label} height={height}")

        test.pre_test(height=height, label=label, coinbases=self._get_new_coinbase, connections=additional_conns)

        tx_col = TxCollection(height=height, label=label)
        test.get_transactions_for_test(tx_col, self._get_new_coinbase)

        if not(tx_col.mempool_txs or tx_col.p2p_invalid_txs or tx_col.p2p_valid_txs or tx_col.block_invalid_txs or tx_col.block_valid_txs):
            self.log.info(f"No transactions to test at height {label} height={height}")

        if tx_col.mempool_txs:
            self._process_p2p_accepts(conn, tx_col.mempool_txs, test_label=test.NAME, height_label=label + " ADDING MEMPOOL UTXOS", p2p_accept_timeout=test.P2P_ACCEPT_TIMEOUT)

        if tx_col.p2p_invalid_txs:
            txs, reasons = zip(*tx_col.p2p_invalid_txs)
            self._process_p2p_rejects(conn, txs, reasons, test_label=test.NAME, height_label=label)

        if tx_col.p2p_valid_txs:
            self._process_p2p_accepts(conn, tx_col.p2p_valid_txs, test_label=test.NAME, height_label=label, p2p_accept_timeout=test.P2P_ACCEPT_TIMEOUT)

        if tx_col.block_invalid_txs:
            txs, reasons = zip(*tx_col.block_invalid_txs)
            self._new_block_check_reject(conn, txs=txs, reasons=reasons, label=f"At \"{test.NAME}\" {label}", block_txs=tx_col.mempool_txs)

        if tx_col.block_valid_txs:
            self._new_block_check_accept(conn, txs=tx_col.mempool_txs+tx_col.block_valid_txs, label=f"At \"{test.NAME}\" {label}")
            self.log.info(f"Invalidating the block {loghash(conn.rpc.getbestblockhash())} to confirm the txs are back to mempool")
            conn.rpc.invalidateblock(conn.rpc.getbestblockhash())

        test.post_test(height=height, label=label, coinbases=self._get_new_coinbase, connections=additional_conns)

        transactions_for_next_block = []
        if prepare_label:
            self.log.info(f"Preparing (inside test) for height {prepare_label} height={height}")
            transactions_for_next_block = test.prepare_utxos_for_test(height=height, label=prepare_label,
                                                                      coinbases=self._get_new_coinbase,
                                                                      connections=additional_conns)

        txs_to_be_in_the_next_block = transactions_for_next_block + tx_col.mempool_txs + tx_col.block_valid_txs
        if txs_to_be_in_the_next_block:
            self._new_block_check_accept(conn, txs=txs_to_be_in_the_next_block, label=f"At \"{test.NAME}\" {label}")

    def _do_prepare_for_height(self, conn, test, label, height, additional_conns):
        self.log.info(f"Preparing for {label} at height={height}")
        transactions_for_next_block = test.prepare_utxos_for_test(height=height, label=label,
                                                                  coinbases=self._get_new_coinbase,
                                                                  connections=additional_conns)
        self._new_block_check_accept(conn, txs=transactions_for_next_block, label=f"At \"{test.NAME}\" {label}")

    def run_test(self):

        def multiplyArg(arg, factor):
            return arg.split("=")[0] + "=" + str(int(int(arg.split("=")[1])*factor))

        self.stop_node(0)
        for test in self._gen_tests:
            test.log = self.log
            self.coinbase_pubkey = test.COINBASE_KEY.get_pubkey() if test.COINBASE_KEY else None

            # if specific tests set -maxstdtxvalidationduration, multiply set value with timeoutfactor to enable longer validation time in slower environments
            test.ARGS = ([multiplyArg(arg, self.options.timeoutfactor) if arg.startswith("-maxstdtxvalidationduration=") else arg for arg in test.ARGS])
            # if specific tests set -maxnonstdtxvalidationduration, multiply set value with timeoutfactor to enable longer validation time in slower environments
            test.ARGS = ([multiplyArg(arg, self.options.timeoutfactor) if arg.startswith("-maxnonstdtxvalidationduration=") else arg for arg in test.ARGS])
            # if specific tests set -maxtxnvalidatorasynctasksrunduration, multiply set value with timeoutfactor to enable longer validation time in slower environments
            test.ARGS = ([multiplyArg(arg, self.options.timeoutfactor) if arg.startswith("-maxtxnvalidatorasynctasksrunduration=") else arg for arg in test.ARGS])

            # maxtxnvalidatorasynctasksrunduration must be greater than maxnonstdtxvalidationduration
            # If we modiy maxnonstdtxvalidationduration due to slower circumstances, check if maxtxnvalidatorasynctasksrunduration should be implicitly increased.
            if self.options.timeoutfactor > 1 and \
               (not any("-maxtxnvalidatorasynctasksrunduration" in arg for arg in test.ARGS) and any("-maxnonstdtxvalidationduration" in arg for arg in test.ARGS)):
                new_maxnonstdtxvalidationduration = [int(arg.split("=")[1]) for arg in test.ARGS if arg.startswith("-maxnonstdtxvalidationduration=")]
                if len(new_maxnonstdtxvalidationduration) > 0:
                    new_maxnonstdtxvalidationduration = new_maxnonstdtxvalidationduration[0]
                    if (DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION * 1000 <= new_maxnonstdtxvalidationduration):
                        self.log.info(f"Setting -maxtxnvalidatorasynctasksrunduration to {new_maxnonstdtxvalidationduration+1} ms.")
                        test.ARGS.append("-maxtxnvalidatorasynctasksrunduration={}".format(new_maxnonstdtxvalidationduration+1))

            with self.run_node_with_connections(title=test.NAME,
                                                node_index=0,
                                                args=test.ARGS,
                                                number_of_connections=1+len(test.ADDITIONAL_CONNECTIONS)) as conns:

                self.log.info("")
                self.log.info("*"*100)
                self.log.info(f"Starting test \"{test.NAME}\"")
                self.log.info("*"*100)

                conn = conns[0]
                additional_conns = {name: c for name, c in zip(test.ADDITIONAL_CONNECTIONS, conns)}

                first_block, coinbase = self._make_first_block(connection=conn)

                for target_height, test_label, prep_label in test.TESTING_HEIGHTS:

                    current_height = conn.rpc.getblock(conn.rpc.getbestblockhash())["height"]
                    if current_height < target_height:
                        coinbases, _ = self._advance_in_main_chain(target_height - current_height, connection=conn)
                        self._assert_height(conn, target_height)
                    elif current_height > target_height:
                        self.log.info(f"invalidating {current_height - target_height} blocks")
                        for _ in range(current_height - target_height):
                            bhash = conn.rpc.getbestblockhash()
                            self.log.info(f"Invalidating best block {loghash(bhash)} at height {current_height}")
                            conn.rpc.invalidateblock(bhash)

                        self._assert_height(conn, target_height)

                    if test_label is not None:
                        self._do_test_at_height(conn, test, test_label, target_height, additional_conns, prep_label)
                    elif prep_label is not None:
                        self._do_prepare_for_height(conn, test, prep_label, target_height, additional_conns)

                self.log.info(f"Invalidating all blocks till first block {loghash(first_block.hash)}")

                conn.rpc.invalidateblock(first_block.hash)
                del self._coinbases[:]
                self.log.info(f"Finishing test \"{test.NAME}\"")
                self.log.info(f"="*100)
