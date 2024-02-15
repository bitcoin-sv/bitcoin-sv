#!/usr/bin/env python3
# Copyright (c) 2019  Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Check different scenarios on how reorg affects contents of mempool and journal.

  # chain reorg as a set operation on the chains of blocks containing sets of transactions

  #     (set(old_tip)) + set(mempool)) - set(new_tip) = new_mempool
  # 0   (     A        +       {}    ) -      {}      =     A
  # 1   (     {}       +       B     ) -      {}      =     B
  # 2   (     C1       +       C2    ) -      {}      =     C1+C2
  # 3   (     D        +       {}    ) -      D       =     {}
  # 4   (     {}       +       E     ) -      E       =     {}
  # 5   (     F1       +       F2    ) -      F1      =     F2
  # 6   (     G1       +       G2    ) -      G1+G2   =     {}
  # 7   (     Hx       +       {}    ) -      Hy      =     {}
  # 8   (     Ix1      +       Ix2   ) -      Iy      =     {}

  Where:
   -  Each letter is a separate (valid) transaction chain
   -  suffixes `x` and `y` are doublespend variants chains starting at the same UTXO
   -  suffixes `1` and `2` are first and second part of the same transaction chain

Two mechanisms for forcing a reorg are tested:
- new_tip is made better(longer) than old_tip
- old_tip is invalidated and so the equally good new_tip is chosen.


'''
from time import sleep
import socket
import itertools
import heapq

from test_framework.blocktools import create_block, create_coinbase
from test_framework.cdefs import ONE_GIGABYTE
from test_framework.key import CECKey
from test_framework.mininode import CTransaction, msg_tx, CTxIn, COutPoint, CTxOut, msg_block, msg_tx
from test_framework.script import CScript, SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID, OP_CHECKSIG
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until

_cntr = 1


def new_key():
    k = CECKey()
    global _cntr
    k.set_secretbytes(_cntr.to_bytes(6, byteorder='big'))
    _cntr += 1
    return k


class UTXO:
    def __init__(self, tx, ndx, key):
        self.tx = tx
        self.ndx = ndx
        self.key = key


_cntr2 = 1


def get_tip(connection):
    return connection.rpc.getblock(connection.rpc.getbestblockhash())


def get_block_hash(block):
    if isinstance(block, dict):
        return block["hash"]
    else:
        return block.hash


def knows_of_block(connection, block):
    def predicate(*, _block_hash=get_block_hash(block)):
        try:
            tmp = connection.rpc.getblock(_block_hash)
            print(f"node knows of block {tmp['hash']} by {_block_hash}")
            assert tmp["hash"] == _block_hash
            return True
        except:
            print(f"node knows noting about block {_block_hash}")
            return False
    return predicate


def block_is_tip(connection, block):
    def predicate(*, _block_hash=get_block_hash(block)):
        ret = connection.rpc.getbestblockhash() == _block_hash
        if ret:
            print(f"node tip is block {_block_hash}")
        return ret
    return predicate


def make_and_send_block_ex(connection, vtx, *, tip=None, wait_for_tip=True):
    "Create and send block with coinbase, returns conbase (tx, key) tuple"
    if tip is None:
        tip = get_tip(connection)
    else:
        tip = connection.rpc.getblock(get_block_hash(tip))

    coinbase_key = new_key()
    coinbase_tx = create_coinbase(tip["height"] + 1, coinbase_key.get_pubkey())
    coinbase_tx.rehash()

    global _cntr2
    _cntr2 += 1
    block = create_block(int(tip["hash"], 16), coinbase_tx, tip["time"] + _cntr2)
    if vtx:
        block.vtx.extend(vtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.calc_sha256()
    block.solve()

    msg = msg_block(block)
    connection.send_message(msg)

    if wait_for_tip:
        wait_until(block_is_tip(connection, block), timeout=15)
    else:
        wait_until(knows_of_block(connection, block), timeout=15)

    return UTXO(coinbase_tx, 0, coinbase_key), connection.rpc.getblock(get_block_hash(block))


def make_and_send_block(connection, vtx, *, tip=None, wait_for_tip=True):
    return make_and_send_block_ex(connection, vtx, tip, wait_for_tip)[0]


def create_tx(utxos, n_outputs, fee_delta=0):
    total_input = 0
    tx = CTransaction()
    for utxo in utxos:
        tx.vin.append(CTxIn(COutPoint(utxo.tx.sha256, utxo.ndx), b"", 0xffffffff))
        total_input += utxo.tx.vout[utxo.ndx].nValue

    amount_per_output = total_input // n_outputs - len(utxos)*300 - n_outputs*200 - 100 - fee_delta

    new_utxos = []

    for i in range(n_outputs):
        k = new_key()
        new_utxos.append(UTXO(tx, i, k))
        tx.vout.append(CTxOut(amount_per_output, CScript([k.get_pubkey(), OP_CHECKSIG])))

    for input_ndx, (utxo, input) in enumerate(zip(utxos, tx.vin)):
        sighash = SignatureHashForkId(utxo.tx.vout[utxo.ndx].scriptPubKey, tx, input_ndx, SIGHASH_ALL | SIGHASH_FORKID, utxo.tx.vout[utxo.ndx].nValue)
        input.scriptSig = CScript([utxo.key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])

    tx.rehash()

    return tx, new_utxos


def split(utxos, n_inputs, n_outputs, fee_delta=0):
    new_utxos = []
    transactions = []
    for _ in split_iter(utxos, n_inputs, n_outputs, new_utxos, transactions, fee_delta):
        pass
    return transactions, new_utxos


def split_iter(utxos, n_inputs, n_outputs, new_utxos=None, transactions=None, fee_delta=0):

    for ndx in range(0, len(utxos), n_inputs):
        tx, xx = create_tx(utxos[ndx : ndx+n_inputs], n_outputs, fee_delta)
        if new_utxos is not None:
            new_utxos.extend(xx)
        if transactions is not None:
            transactions.append(tx)
        yield tx, xx


def make_tx_chain(utxo, chain_length, fee_delta=0):
    def gen():
        utxos = [utxo]
        for a in range(chain_length):
            tx, utxos = create_tx(utxos, 1, fee_delta=fee_delta)
            yield tx
    return list(gen())


def chop(x, n=2):
    """Chop sequence into n approximately equal slices
    >>> chop(range(10), n=3)
    [[0, 1, 2], [3, 4, 5, 6], [7, 8, 9]]
    >>> chop(range(3), n=10)
    [[], [0], [], [], [1], [], [], [], [2], []]
    """
    x = list(x)
    if n < 2:
        return [x]

    def gen():
        m = len(x) / n
        i = 0
        for _ in range(n-1):
            yield x[round(i):round(i + m)]
            i += m
        yield x[round(i):]
    return list(gen())


def splice(*iters):
    """
    >>> print(*splice('abc', 'de', 'f'))
    a d f b e c
    """
    nothing = object()
    return (x
            for x in itertools.chain(
                *itertools.zip_longest(
                    *iters,
                    fillvalue=nothing))
            if x is not nothing)


def make_blocks_from(conn, root_block, nblocks, *txs_lists, wait_for_tip=True):
    def gen(root_block, nblocks):
        for i, txs in enumerate(chop(splice(*txs_lists), n=nblocks), start=1):
            _, root_block = make_and_send_block_ex(conn, txs, tip=root_block, wait_for_tip=wait_for_tip)
            yield root_block
    return list(gen(root_block, nblocks))


def submit_to_mempool(conn, *txs_lists):
    txs = list(splice(*txs_lists))
    expected_mempool_size = conn.rpc.getmempoolinfo()["size"] + len(txs)
    for tx in txs:
        conn.send_message(msg_tx(tx))
    # All planned transactions should be accepted into the mempool
    wait_until(lambda: conn.rpc.getmempoolinfo()["size"] == expected_mempool_size)


class property_dict(dict):
    def __getattr__(self, k): return self.__getitem__(k)
    def __setattr__(self, k, v): return self.__setitem__(k, v)


def tx_ids(txs):
    return [tx if isinstance(tx, str) else tx.hash for tx in txs]


class tx_set_context(dict):
    def __init__(self, context={}, **subsets):
        context = dict(context)
        context.update(subsets)
        super().__init__((k, tx_ids(v)) for k,v in context.items())


class tx_set(set):
    def __init__(self, _members=(), *, _name=None):
        if isinstance(_members, tx_set):
            _name = _name if _name is not None else _members._name
        self._name = _name if _name is not None else 'set'
        super().__init__(tx_ids(_members))

    def explain(self, other, *, context):
        if not isinstance(other, tx_set):
            other = tx_set(other)
        if not isinstance(context, tx_set_context):
            context = tx_set_context(context)
        ret = ""
        explained = set()
        for n, v in context.items():
            if not self.intersection(v):
                continue
            if other.intersection(v):
                ret += self._explain_range(n, v, other)
                ret += " "
            else:
                ret += f"no {n} "
            explained.update(other.intersection(v))
        missing = self.difference(explained)
        if missing:
            if ret:
                ret += "and "
            ret += f"missing from {self._name} are "
            for n, v in context.items():
                if not self.intersection(v):
                    continue
                if missing.intersection(v):
                    ret += self._explain_range(n, v, missing)
                    ret += " "
                missing.difference_update(v)
            if missing:
                ret += ", ".join(sorted(missing))
                ret += " "
        unexpected = other.difference(self)
        if unexpected:
            if ret:
                ret += "and "
            ret += f"unexpected "
            for n, v in context.items():
                if unexpected.intersection(v):
                    ret += self._explain_range(n, v, unexpected)
                    ret += " "
                unexpected.difference_update(v)
            if unexpected:
                ret += ", ".join(sorted(unexpected))
                ret += " "
        return f"{other._name} is {ret}"

    def _explain_range(self, n, v, elements):
        def find_slices():
            last = None
            for i in sorted(map(v.index, elements.intersection(v))):
                if last is None:
                    last = slice(i, i+1)
                elif last.stop == i:
                    last = slice(last.start, i+1)
                else:
                    yield last
                    last = None
            if last is not None:
                yield last

        def show_slices(slices):
            for s in slices:
                start = str(s.start) if s.start > 0 else ""
                stop = str(s.stop) if s.start > 0 or s.stop < len(v) else ""
                yield f"{n}[{start}:{stop}]" if s.start+1 != s.stop else f"{n}[{s.start}]"
        return " ".join(show_slices(find_slices()))


c = property_dict(A="abc", B="def", C="ghi", Z="xyz")
e = tx_set(c.A + c.B + c.C, _name="'e'")
a = tx_set("abcdegixyq", _name="'a'")


# assert e == a, e.explain(a, context=c)
class ReorgTests(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        with self.run_node_with_connections("Xxxxxxxxxxxxx",
                                            0,
                                            ["-checkmempool=1",
                                             '-whitelist=127.0.0.1',
                                             '-genesisactivationheight=1',
                                             '-jbafillafternewblock=1'],
                                            number_of_connections=1) as (conn,):
            self.log.info("making coinbase")
            fee_delta = 10
            utxo, _ = make_and_send_block_ex(conn, [])
            conn.rpc.generate(110)

            # we will mine two competing chains, old and new
            # the old one will be one block longer than the new
            # We will use several strategies to switch the chains

            def reorg_by_invalidateblock(conn, old_chain, new_chain):
                """Invalidate last two blocks of the old chain to make the new chain longer"""
                conn.rpc.invalidateblock(old_chain[-2]["hash"])
                wait_until(lambda: conn.rpc.getbestblockhash() == new_chain[-1]["hash"], timeout=10)
                return new_chain[-1]

            def reorg_by_mining(conn, old_chain, new_chain):
                """Mine two more blocks on the new chain to make the new chain longer"""
                more_chain = make_blocks_from(conn, new_chain[-1], 1, wait_for_tip=False)
                more_chain = make_blocks_from(conn, more_chain[-1], 1, wait_for_tip=True)
                return more_chain[-1]

            reorg_strategies = [reorg_by_invalidateblock, reorg_by_mining]

            txs, case_utxos = split([utxo], 1, len(reorg_strategies), fee_delta=fee_delta)
            _, root_block_data = make_and_send_block_ex(conn, txs)

            for strategy, case_utxo in zip(reorg_strategies, case_utxos):
                self.check_reorg_cases(conn, root_block_data, strategy, case_utxo=case_utxo, fee_delta=fee_delta)

    def check_reorg_cases(self, conn, root_block_data, instigate_reorg, case_utxo, fee_delta):
        self.log.info("Check reorg cases with %s", instigate_reorg.__name__)
        n_cases = 9
        txs, new_utxos = split([case_utxo], 1, n_cases, fee_delta=fee_delta)
        utxo, root_block_data = make_and_send_block_ex(conn, txs)

        # stay below 25 chain limit as the whole thing may end up in the mempool
        tx_chain_depth = 24

        # see docstring above

        self.log.info("preparing transactions")
        chains = property_dict()
        chains.A = make_tx_chain(new_utxos[0], tx_chain_depth, fee_delta=fee_delta)
        chains.B = make_tx_chain(new_utxos[1], tx_chain_depth, fee_delta=fee_delta)
        chains.C1, chains.C2 = chop(make_tx_chain(new_utxos[2], tx_chain_depth, fee_delta=fee_delta))
        chains.D = make_tx_chain(new_utxos[3], tx_chain_depth, fee_delta=fee_delta)
        chains.E = make_tx_chain(new_utxos[4], tx_chain_depth, fee_delta=fee_delta)
        chains.F1, chains.F2 = chop(make_tx_chain(new_utxos[5], tx_chain_depth, fee_delta=fee_delta))
        chains.G1, chains.G2 = chop(make_tx_chain(new_utxos[6], tx_chain_depth, fee_delta=fee_delta))
        chains.Hx = make_tx_chain(new_utxos[7], tx_chain_depth, fee_delta=fee_delta)
        chains.Hy = make_tx_chain(new_utxos[7], tx_chain_depth, fee_delta=fee_delta)
        chains.Ix1, chains.Ix2 = chop(make_tx_chain(new_utxos[8], tx_chain_depth, fee_delta=fee_delta))
        chains.Iy = make_tx_chain(new_utxos[8], tx_chain_depth, fee_delta=fee_delta)

        nblocks = 5

        self.log.info("preparing chain to be invalidated")
        chain_to_be_invalidated = make_blocks_from(conn, root_block_data, nblocks + 1,
                                                   chains.A,
                                                   chains.C1,
                                                   chains.D,
                                                   chains.F1,
                                                   chains.G1,
                                                   chains.Hx,
                                                   chains.Ix1,
                                                   wait_for_tip=True)

        self.log.info("submitting to mempool")
        submit_to_mempool(conn,
                          chains.B,
                          chains.C2,
                          chains.E,
                          chains.F2,
                          chains.G2,
                          chains.Ix2)

        self.log.info("preparing chain to be activated")
        chain_to_activate = make_blocks_from(conn, root_block_data, nblocks,
                                             chains.D,
                                             chains.E,
                                             chains.F1,
                                             chains.G1 + chains.G2,
                                             chains.Hy,
                                             chains.Iy,
                                             wait_for_tip=False)

        self.log.info("check tip before reorg")
        expected_tip = chain_to_be_invalidated[-1]
        actual_tip = get_tip(conn)
        assert expected_tip["hash"] == actual_tip["hash"]

        self.log.info("instigating a reorg by %s", instigate_reorg.__name__)
        expected_tip = instigate_reorg(conn, chain_to_be_invalidated, chain_to_activate)

        self.log.info("check tip after reorg")
        actual_tip = get_tip(conn)
        assert expected_tip["hash"] == actual_tip["hash"]

        conn.cb.sync_with_ping()
        mempool_txs = []
        # make sure that JBA has catched up
        for i in range(5):
            self.log.info("mining the mempool")
            conn.rpc.generate(1)

            tip = get_tip(conn)
            mempool_txs.extend(tip["tx"][1:])

            mempool_size = conn.rpc.getmempoolinfo()['size']
            if not mempool_size:
                break    # everything is mined

            self.log.info("give JBA some time to do it's thing")
            sleep(0.1)
        else:
            assert False, "Mempool is not empty after {i} blocks, {n} transactions remaining.".format(
                i=i, n=mempool_size)
        actual_mempool = tx_set(mempool_txs, _name='actual_mempool')
        expected_mempool = tx_set(chains.A +
                                  chains.B +
                                  chains.C1 +
                                  chains.C2 +
                                  chains.F2,
                                  _name='expected_mempool')

        assert expected_mempool == actual_mempool, expected_mempool.explain(actual_mempool, context=chains)


if __name__ == '__main__':
    ReorgTests().main()
