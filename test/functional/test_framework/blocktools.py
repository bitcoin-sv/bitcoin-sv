#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""Utilities for manipulating blocks and transactions."""

from .mininode import *
from .script import CScript, OP_TRUE, OP_CHECKSIG, OP_RETURN, OP_EQUAL, OP_HASH160
from .util import assert_equal, assert_raises_rpc_error, hash256
from test_framework.cdefs import (ONE_MEGABYTE, LEGACY_MAX_BLOCK_SIZE, MAX_BLOCK_SIGOPS_PER_MB, MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS)

from collections import deque

# Create a block (with regtest difficulty)


def create_block(hashprev, coinbase, nTime=None):
    block = CBlock()
    if nTime is None:
        import time
        block.nTime = int(time.time() + 600)
    else:
        block.nTime = nTime
    block.hashPrevBlock = hashprev
    block.nBits = 0x207fffff  # Will break after a difficulty adjustment...
    block.vtx.append(coinbase)
    block.hashMerkleRoot = block.calc_merkle_root()
    block.calc_sha256()
    return block

# Do incorrect POW for block
def solve_bad(block):
    block.rehash()
    target = uint256_from_compact(block.nBits)
    while block.sha256 < target:
        block.nNonce += 1
        block.rehash()

def serialize_script_num(value):
    r = bytearray(0)
    if value == 0:
        return r
    neg = value < 0
    absvalue = -value if neg else value
    while (absvalue):
        r.append(int(absvalue & 0xff))
        absvalue >>= 8
    if r[-1] & 0x80:
        r.append(0x80 if neg else 0)
    elif neg:
        r[-1] |= 0x80
    return r


# Calculate the merkle root for a block
def merkle_root_from_merkle_proof(coinbase_hash, merkle_proof):
    merkleRootBytes = ser_uint256(coinbase_hash)
    for mp in merkle_proof:
        mp = int(mp, 16)
        mpBytes = ser_uint256(mp)
        merkleRootBytes = hash256(merkleRootBytes + mpBytes)
        merkleRootBytes = merkleRootBytes[::-1] # Python stores these the wrong way round
    return uint256_from_str(merkleRootBytes)

# Create a valid submittable block (and coinbase) from a mining candidate
def create_block_from_candidate(candidate, get_coinbase):
    block = CBlock()
    block.nVersion = candidate["version"]
    block.hashPrevBlock = int(candidate["prevhash"], 16)
    block.nTime = candidate["time"]
    block.nBits = int(candidate["nBits"], 16)
    block.nNonce = 0

    if(get_coinbase):
        coinbase_tx = FromHex(CTransaction(), candidate["coinbase"])
    else:
        coinbase_tx = create_coinbase(height=int(candidate["height"]) + 1)
    coinbase_tx.rehash()
    block.vtx = [coinbase_tx]

    # Calculate merkle root & solve
    block.hashMerkleRoot = merkle_root_from_merkle_proof(coinbase_tx.sha256, candidate["merkleProof"])
    block.solve()
    block.rehash()

    return block, coinbase_tx


# Create a coinbase transaction, assuming no miner fees.
# If pubkey is passed in, the coinbase output will be a P2PK output;
# otherwise an anyone-can-spend output.


def create_coinbase(height, pubkey=None, outputValue=50):
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff),
                              ser_string(serialize_script_num(height)), 0xffffffff))
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = outputValue * COIN
    halvings = int(height / 150)  # regtest
    coinbaseoutput.nValue >>= halvings
    if (pubkey != None):
        coinbaseoutput.scriptPubKey = CScript([pubkey, OP_CHECKSIG])
    else:
        coinbaseoutput.scriptPubKey = CScript([OP_TRUE])
    coinbase.vout = [coinbaseoutput]
    coinbase.calc_sha256()
    return coinbase

# Create a coinbase transaction containing P2SH, assuming no miner fees.


def create_coinbase_P2SH(height, scriptHash, outputValue=50):
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff),
                              ser_string(serialize_script_num(height)), 0xffffffff))
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = outputValue * COIN
    coinbaseoutput.scriptPubKey = CScript([OP_HASH160, hex_str_to_bytes(scriptHash), OP_EQUAL])
    halvings = int(height / 150)  # regtest
    coinbaseoutput.nValue >>= halvings
    coinbase.vout = [coinbaseoutput]
    coinbase.calc_sha256()
    return coinbase

# Create a transaction.
# If the scriptPubKey is not specified, make it anyone-can-spend.


def create_transaction(prevtx, n, sig, value, scriptPubKey=CScript()):
    tx = CTransaction()
    assert(n < len(prevtx.vout))
    tx.vin.append(CTxIn(COutPoint(prevtx.sha256, n), sig, 0xffffffff))
    tx.vout.append(CTxOut(value, scriptPubKey))
    tx.calc_sha256()
    return tx


def get_legacy_sigopcount_block(block, fAccurate=True):
    count = 0
    for tx in block.vtx:
        count += get_legacy_sigopcount_tx(tx, fAccurate)
    return count


def get_legacy_sigopcount_tx(tx, fAccurate=True):
    count = 0
    for i in tx.vout:
        count += i.scriptPubKey.GetSigOpCount(fAccurate)
    for j in tx.vin:
        # scriptSig might be of type bytes, so convert to CScript for the moment
        count += CScript(j.scriptSig).GetSigOpCount(fAccurate)
    return count

def calc_needed_data_size(script_op_codes, target_size):
    def pushdata_size(sz):
        if sz < 0x4c:
            return 1  # OP_PUSHDATA
        elif sz <= 0xff:
            return 2  # OP_PUSHDATA1
        elif sz <= 0xffff:
            return 3  # OP_PUSHDATA2
        elif sz <= 0xffffffff:
            return 5  # OP_PUSHDATA4
        else:
            raise ValueError("Data too long to encode in a PUSHDATA op")

    return target_size - (len(script_op_codes) + pushdata_size(target_size))

def make_block(connection, parent_block=None, makeValid=True, last_block_time=0):
    if parent_block is not None:
        parent_hash = parent_block.sha256
        parent_time = parent_block.nTime
        parent_height = parent_block.height

    else:
        tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
        parent_hash = int(tip["hash"], 16)
        parent_time = tip["time"]
        parent_height = tip["height"]

    coinbase_tx = create_coinbase(parent_height + 1)
    if not makeValid:
        coinbase_tx.vout.append(CTxOut(50, CScript([OP_TRUE])))
    coinbase_tx.rehash()

    block = create_block(parent_hash, coinbase_tx, max(last_block_time, parent_time) + 1)

    block.height = parent_height + 1
    block.solve()
    return block, block.nTime

def send_by_headers(conn, blocks, do_send_blocks):
    hash_block_map = {b.sha256: b for b in blocks}

    def on_getdata(c, msg):
        for i in msg.inv:
            bl = hash_block_map.get(i.hash, None)
            if not bl:
                continue
            del hash_block_map[i.hash]
            conn.send_message(msg_block(bl))

    with conn.cb.temporary_override_callback(on_getdata=on_getdata):
        headers_message = msg_headers()
        headers_message.headers = [CBlockHeader(b) for b in blocks]
        conn.send_message(headers_message)
        if do_send_blocks:
            wait_until(lambda: len(hash_block_map)==0, label="wait until all blocks are sent")

def chain_tip_status_equals(conn, hash, status):
    chain_tips = conn.rpc.getchaintips()
    for tip in chain_tips:
        if tip["hash"] == hash and tip["status"] == status:
            return True
    return False
    
def wait_for_tip(conn, hash):
    wait_until(lambda: conn.rpc.getbestblockhash() == hash, timeout=10, check_interval=0.2,
                label=f"waiting until {hash} become tip")

def wait_for_tip_status(conn, hash, status):
    wait_until(lambda: chain_tip_status_equals(conn, hash, status), timeout=10, check_interval=0.2,
                label=f"waiting until {hash} is tip with status {status}")

### Helper to build chain

class PreviousSpendableOutput():

    def __init__(self, tx=CTransaction(), n=-1):
        self.tx = tx
        self.n = n  # the output we're spending

class ChainManager():
    # Tool helping to manage creating blockchain
    #
    def __init__(self):
        # Holder of the genesis hash
        self._genesis_hash = None
        # Map the block hash to block heigh
        self.block_heights = {}
        # Tip of the chain (the last block)
        self.tip = None
        # Map block number to block
        self.blocks = {}
        # Store list of UTXO
        self._spendable_outputs = []

    def set_genesis_hash(self, hash):
        self._genesis_hash = hash
        self.block_heights[self._genesis_hash] = 0

    # save the current tip so it can be spent by a later block
    def save_spendable_output(self):
        self._spendable_outputs.append(self.tip)

    # get an output that we previously marked as spendable
    def get_spendable_output(self):
        return PreviousSpendableOutput(self._spendable_outputs.pop(0).vtx[0], 0)

    def add_transactions_to_block(self, block, tx_list):
        [tx.rehash() for tx in tx_list]
        block.vtx.extend(tx_list)

    # this is a little handier to use than the version in blocktools.py
    def create_tx_with_script(self, spend, value, script):
        tx = create_transaction(spend.tx, spend.n, b"", value, script)
        return tx

    # move the tip back to a previous block
    def set_tip(self, number):
        self.tip = self.blocks[number]

    def next_block(self, number, spend=None, script=CScript([OP_TRUE]), block_size=0, extra_sigops=0, extra_txns=0, additional_coinbase_value=0, do_solve_block=True, coinbase_pubkey=None):
        if self.tip == None:
            base_block_hash = self._genesis_hash
            block_time = int(time.time()) + 1
        else:
            base_block_hash = self.tip.sha256
            block_time = self.tip.nTime + 1
        # First create the coinbase
        height = self.block_heights[base_block_hash] + 1
        coinbase = create_coinbase(height=height, pubkey=coinbase_pubkey)
        coinbase.vout[0].nValue += additional_coinbase_value
        coinbase.rehash()
        if spend == None:
            # We need to have something to spend to fill the block.
            assert_equal(block_size, 0)
            block = create_block(base_block_hash, coinbase, block_time)
        else:
            # all but one satoshi to fees
            coinbase.vout[0].nValue += spend.tx.vout[spend.n].nValue - 1
            coinbase.rehash()
            block = create_block(base_block_hash, coinbase, block_time)

            # Make sure we have plenty engough to spend going forward.
            spendable_outputs = deque([spend])

            def get_base_transaction():
                # Create the new transaction
                tx = CTransaction()
                # Spend from one of the spendable outputs
                spend = spendable_outputs.popleft()
                tx.vin.append(CTxIn(COutPoint(spend.tx.sha256, spend.n)))
                # Add spendable outputs
                for i in range(4):
                    tx.vout.append(CTxOut(0, CScript([OP_TRUE])))
                    spendable_outputs.append(PreviousSpendableOutput(tx, i))
                return tx

            tx = get_base_transaction()

            # Make it the same format as transaction added for padding and save the size.            
            tx.rehash()
            base_tx_size = len(tx.serialize())

            # If a specific script is required, add it.
            if script != None:
                tx.vout.append(CTxOut(1, script))

            # Put some random data into the first transaction of the chain to randomize ids.
            tx.vout.append(
                CTxOut(0, CScript([random.randint(0, 256), OP_RETURN])))

            # Add the transaction to the block
            self.add_transactions_to_block(block, [tx])

            # Add transaction until we reach the expected transaction count
            for _ in range(extra_txns):
                self.add_transactions_to_block(block, [get_base_transaction()])

            # If we have a block size requirement, just fill
            # the block until we get there
            current_block_size = len(block.serialize())

            extra_sigops_orig = extra_sigops

            while current_block_size < block_size:
                # We will add a new transaction. That means the size of
                # the field enumerating how many transaction go in the block
                # may change.
                current_block_size -= len(ser_compact_size(len(block.vtx)))
                current_block_size += len(ser_compact_size(len(block.vtx) + 1))

                # Create the new transaction
                tx = get_base_transaction()

                # Add padding to fill the block.
                script_length = block_size - current_block_size
                num_loops = 1
                if script_length > 510000:
                    if script_length < 1000000:
                        # Make sure we don't find ourselves in a position where we
                        # need to generate a transaction smaller than what we expected.
                        script_length = script_length // 2
                        num_loops = 2
                    else:
                        num_loops = script_length // 500000
                        script_length = 500000
                script_length -= base_tx_size + 8 + len(ser_compact_size(script_length)) # <existing tx size> + <amount> + <vector<size><elements>>
                tx_sigops = min(extra_sigops, script_length, MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS)
                if tx_sigops * num_loops < extra_sigops:
                    tx_sigops = min(extra_sigops // num_loops, script_length)
                extra_sigops -= tx_sigops
                script_pad_len = script_length - tx_sigops - len(ser_compact_size(script_length - tx_sigops))
                script_output = CScript([b'\x00' * script_pad_len] + [OP_CHECKSIG] * tx_sigops)

                tx.vout.append(CTxOut(0, script_output))

                # Add the tx to the list of transactions to be included
                # in the block.
                self.add_transactions_to_block(block, [tx])
                current_block_size += len(tx.serialize())

            # Now that we added a bunch of transaction, we need to recompute
            # the merkle root.
            block.hashMerkleRoot = block.calc_merkle_root()

        # Check that the block size is what's expected
        if block_size > 0:
            assert_equal(len(block.serialize()), block_size)

        # check that extra_sigops are included
        if extra_sigops >  0:
            raise AssertionError("Can not fit %s extra_sigops in a block size of %s" % (extra_sigops_orig, block_size))

        # Do PoW, which is cheap on regnet
        if do_solve_block:
            block.solve()
        self.tip = block
        self.block_heights[block.sha256] = height
        assert number not in self.blocks
        self.blocks[number] = block
        return block

    # adds transactions to the block and updates state
    def update_block(self, block_number, new_transactions):
        block = self.blocks[block_number]
        self.add_transactions_to_block(block, new_transactions)
        old_sha256 = block.sha256
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        # Update the internal state just like in next_block
        self.tip = block
        if block.sha256 != old_sha256:
            self.block_heights[block.sha256] = self.block_heights[old_sha256]
            del self.block_heights[old_sha256]
        self.blocks[block_number] = block
        return block
