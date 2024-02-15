#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""Utilities for manipulating blocks and transactions."""
from test_framework.script import SignatureHashForkId, SIGHASH_ALL, SIGHASH_FORKID
from test_framework.comptool import TestInstance
from .cdefs import (MAX_BLOCK_SIGOPS_PER_MB, MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS, ONE_MEGABYTE,
                    MAX_TX_SIZE_POLICY_BEFORE_GENESIS, DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS)
from .key import CECKey
from .mininode import *
from .script import CScript, hash160, OP_FALSE, OP_TRUE, OP_CHECKSIG, OP_DUP, OP_RETURN, OP_EQUAL, OP_EQUALVERIFY, OP_HASH160, OP_0
from .util import assert_equal, assert_raises_rpc_error, hash256, satoshi_round, hex_str_to_bytes

from collections import deque
from decimal import Decimal


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


# Mine a block of the specified size
def mine_block_of_size(node, size, utxos=None, fee=Decimal("0.00001"), genesisActivated=True):
    utxos = utxos if utxos is not None else []
    if not utxos:
        utxos.extend(node.listunspent())
    while size > 0:
        utxo = utxos.pop()
        if utxo['amount'] < fee:
            continue

        addr = node.getnewaddress()
        inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
        outputs = {}
        change = utxo['amount'] - fee
        outputs[addr] = satoshi_round(change)

        rawtx = node.createrawtransaction(inputs, outputs)
        largetx = CTransaction()
        largetx.deserialize(BytesIO(hex_str_to_bytes(rawtx)))

        maxtxnsize = DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS if genesisActivated else DEFAULT_MAX_TX_SIZE_POLICY_BEFORE_GENESIS
        maxtxnsize -= 180 # Existing txn size
        txnsize = maxtxnsize if size >= maxtxnsize else size
        largetx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN, bytearray([0] * txnsize)])))
        size -= txnsize

        signed = node.signrawtransaction(ToHex(largetx))
        node.sendrawtransaction(signed["hex"], True)
    node.generate(1)


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


# Calculate merkle root from a branch
def merkle_root_from_branch(leaf_hash, index, branch):
    root = ser_uint256(leaf_hash)
    for node in branch:
        if node == 0:
            # Duplicated node
            root = hash256(root + root)
        elif index & 1:
            root = hash256(ser_uint256(node) + root)
        else:
            root = hash256(root + ser_uint256(node))

        root = root[::-1]
        index >>= 1
    return uint256_from_str(root)


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


# a little handier version of create_transaction
def create_tx(spend_tx, n, value, script=CScript([OP_TRUE])):
    tx = create_transaction(spend_tx, n, b"", value, script)
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


# sign a transaction, using the key we know about
# this signs input 0 in tx, which is assumed to be spending output n in
# spend_tx
def sign_tx(tx, spend_tx, n, private_key):
    scriptPubKey = bytearray(spend_tx.vout[n].scriptPubKey)
    if (scriptPubKey[0] == OP_TRUE):  # an anyone-can-spend
        tx.vin[0].scriptSig = CScript()
        return
    sighash = SignatureHashForkId(
        spend_tx.vout[n].scriptPubKey, tx, 0, SIGHASH_ALL | SIGHASH_FORKID, spend_tx.vout[n].nValue)
    tx.vin[0].scriptSig = CScript(
        [private_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID]))])


def create_and_sign_transaction(spend_tx, n, value, script=CScript([OP_TRUE]), private_key=None):
    tx = create_tx(spend_tx, n, value, script)
    sign_tx(tx, spend_tx, n, private_key)
    tx.rehash()
    return tx


class TxCreator:
    """
    Provides helpers that simplify creating signed transactions.
    """

    def __init__(self):
        # Create default private/public key used by sign methods
        self.set_private_key_from_secretbytes(b"horsebattery")

    def set_private_key_from_secretbytes(self, secretbytes):
        self.private_key = CECKey()
        self.private_key.set_secretbytes(secretbytes)
        self.public_key = self.private_key.get_pubkey()

    def sign_tx_p2pkh_input(self, tx, n, spend_txout, private_key, sighash_flags=SIGHASH_ALL, no_rehash=False):
        """
        Sign input n in transaction tx by setting its scriptSig member

        All members in tx that affect the signature must be set.
        E.g: If sighash_flags=SIGHASH_ALL, everything (except scriptSig) must be set and
             transaction cannot be modified later without invalidating the signature.

        tx.vin[n].prevout must correspond to id of transaction output (coin) spend_txout.
        E.g: If this output was created by transaction spend_tx (spend_tx.vout[m]==spend_txout),
             then tx.vin[n].prevout must be equal to COutPoint(spend_tx.sha256, m)

        Transaction output spend_txout must be a standard P2PKH with its public key corresponding to given private_key.

        Example:
          spend_tx = ...
          tx = CTransaction()
          tx.vin.append(CTxIn(COutPoint(spend_tx.sha256, 0), nSequence=0xffffffff))
          tx.vout.append(CTxOut(spend_tx.vout[0].nValue, CScript([OP_DUP, OP_HASH160, hash160(tx_creator.public_key), OP_EQUALVERIFY, OP_CHECKSIG])))
          tx_creator.sign_tx_p2pkh_input(tx, 0, spend_tx.vout[0], tx_creator.private_key)

        Parameters:
          tx -- Transaction whose input will be signed

          n -- Index of transaction input that will be signed

          spend_txout -- Transaction output that we want to spend

          private_key -- Private key used sign the input
                         To create a valid signature, it should correspond to public key used in spend_txout.scriptPubKey

          sighash_flags -- Flags used to calculate the hash of the signature.
                           Default: SIGHASH_ALL
                           SIGHASH_FORKID is automatically added as required by BSV nodes.

          no_rehash -- If True, hash of the transaction in not automatically recalculated after the input is signed.
                       Default: False
        """

        assert n < len(tx.vin)

        sighash_flags |= SIGHASH_FORKID

        sighash = SignatureHashForkId(spend_txout.scriptPubKey, tx, n, sighash_flags, spend_txout.nValue)

        tx.vin[n].scriptSig = CScript([
            private_key.sign(sighash) + bytes(bytearray([sighash_flags])),
            private_key.get_pubkey() # NOTE: Getting public key every time is a performance overhead but it is more convenient than having to always pass both private and corresponding public key.
        ])

        if not no_rehash:
            tx.rehash()

    def create_signed_transaction(self, inputs, *, num_outputs=1, value=None, pubkey=None, scriptPubKey=None, tx_customize_func=None, fee=None, fee_rate=None, pubkey_change=None, scriptPubKey_change=None, sighash_flags=SIGHASH_ALL) -> CTransaction:
        """
        Create a new signed transaction with specified number of outputs and spending outputs provided in inputs argument.

        Only P2PKH and trivially spendable (OP_TRUE) inputs are correctly signed.

        If the returned transaction needs to be modified afterwards, its inputs must likely (depending on the value
        of sighash_flags) be signed again by calling sign_tx_p2pkh_input() and specifying the same arguments (see helper
        function sign_all_inputs_in_tx() in this method).

        Examples:
          tx_creator = TxCreator()
          private_key_user1 = CECKey()
          private_key_user1.set_secretbytes(b"user1")
          public_key_user1 = private_key_user1.get_pubkey()
          private_key_user2 = CECKey()
          private_key_user2.set_secretbytes(b"user2")
          public_key_user2 = private_key_user2.get_pubkey()

          cbtx? = ... # coinbase transactions whose first output is P2PKH locked with key self.public_key (or trivially spendable)

          # Spends the first output of a coinbase transaction and provides one P2PKH output of 50BSV
          tx0 = tx_creator.create_signed_transaction(cbtx1, value=50*COIN)

          # Spends the first output of tx0 and provides two P2PKH outputs of 25BSV
          tx1 = tx_creator.create_signed_transaction(tx0, value=25*COIN, num_outputs=2)

          # Spends the first output of tx1 and provides 25 P2PKH outputs of 1BSV locked with user1's key
          tx2 = tx_creator.create_signed_transaction(tx1, num_outputs=25, value=1*COIN, pubkey=public_key_user1)

          # Spends the second output of tx2, provides 2 P2PKH outputs that pay each 1000 satoshi to user2, pays 50 satoshi
          # to the miner (transaction fee) and pays the rest in the third P2PKH output back to user1
          tx3 = tx_creator.create_signed_transaction((tx2, 1, private_key_user1), num_outputs=2, value=1000, fee=50, pubkey=public_key_user2, pubkey_change=public_key_user1)

          # Spends the first output of a coinbase transaction, provides one non-locked output of 1BSV and sends the rest back in the 2nd output
          tx4 = tx_creator.create_signed_transaction(cbtx2, value=1*COIN, fee=0, scriptPubKey=CScript([OP_TRUE]))

          # Similar as above except that here we spend whatever was left of a coinbase output and is now in last output in tx4
          tx5 = tx_creator.create_signed_transaction((tx4, -1), value=1*COIN, fee=0, scriptPubKey=CScript([OP_TRUE]))

          # Similar as above except that here we also pass tx_customize_func argument to add an OP_RETURN output to the front
          tx6 = tx_creator.create_signed_transaction((tx5, -1), value=1*COIN, fee=0, scriptPubKey=CScript([OP_TRUE]), tx_customize_func=lambda tx: tx.vout.insert(0, CTxOut(0, CScript([OP_FALSE, OP_RETURN]))))

          # Same as tx4 except that fee_rate is used to calculate the transaction fee based on its size and reduce the
          # value of the 2nd output accordingly.
          tx7 = tx_creator.create_signed_transaction(cbtx3, value=1*COIN, fee_rate=10000, scriptPubKey=CScript([OP_TRUE]))

          # Spends several transaction outputs and provides one P2PKH output that pays everything back to the owner of key within the TxCreator
          tx8 = tx_creator.create_signed_transaction([tx4, tx5, (tx3, 0, private_key_user2), (tx3, -1, private_key_user1, SIGHASH_NONE)], num_outputs=0, fee=0)
          assert tx8.vout[0].nValue == 1*COIN + 1*COIN + 1000 + (1*COIN - 2*1000 - 50)

        Parameters:
          inputs -- Provides transaction outputs that will be spent and is used to define inputs of created transaction
                    Value can be:
                      - A tuple with 2-4 items (spent_tx, n, private_key, sighash_flags):
                          spend_tx -- Transaction whose output will be spent
                          n -- Index of transaction output that will be spent
                               Negative values can be used to specify a reverse index (-1 corresponds to the last output).
                          private_key -- Private key used sign the input
                                         Default: self.private_key
                                         To create a valid signature, it should correspond to public key used in spend_tx.vout[n].scriptPubKey
                          sighash_flags -- Flags used to calculate the hash of the signature.
                                           Default: SIGHASH_ALL
                                           SIGHASH_FORKID is automatically added as required by BSV nodes.
                      - A transaction object: This is the same as if tuple (tx, n) was specified, where:
                          - tx is given transaction object
                          - n is the first output in tx that is not provably non-spendable (i.e. OP_FALSE, OP_RETURN)
                      - An array of the above: This can be used to specify more than one transaction input.

          num_outputs -- Desired number of outputs in created transaction
                         Default: 1
                         If fee is specified, one more output will be added (see description of fee parameter).

          value -- Value (in satoshi) in each of these outputs
                   Must be provided if num_outputs>0
                   Value is not checked to allow creating invalid transactions.

          pubkey -- Public key used to provide default value for parameter scriptPubKey
                    Default: self.public_key
          scriptPubKey -- Locking script used in these outputs
                          Default: CScript([OP_DUP, OP_HASH160, hash160(pubkey), OP_EQUALVERIFY, OP_CHECKSIG])

          tx_customize_func -- If specified, this function is called after desired number of outputs (num_outputs) have
                               already been added and before adding a 'change' output.
                               First parameter is the transaction object which contains all inputs and all outputs,
                               except 'change', which will we added later (if requested). The function can modify this
                               transaction object as needed except that inputs must not be added or removed and
                               outpoint of inputs must not be changed, since they will be signed later.

          fee -- Absolute transaction fee (in satoshi)
          fee_rate -- Fee rate (in satoshi/1000bytes)
                 If either of these two arguments is specified, an additional 'change' output is added. Value of this
                 output is computed so that transaction provides the following fee:
                    fee + fee_rate * total_tx_size / 1000

          pubkey_change -- Public key used to provide default value for parameter scriptPubKey_change
                           Default: Same as pubkey
          scriptPubKey_change -- Locking script used in 'change' output
                                 Default: CScript([OP_DUP, OP_HASH160, hash160(pubkey_change), OP_EQUALVERIFY, OP_CHECKSIG])
        """

        class SpentOutput:
            def __init__(self, tx, n, private_key, sighash_flags):
                self.tx = tx
                self.n = n
                self.private_key = private_key
                self.sighash_flags = sighash_flags

        # Convert inputs argument into a list of SpentOutput objects using defaults for missing values
        spent_outputs = []
        for input in (inputs if type(inputs) is list else [inputs]):
            # defaults
            n = 0
            private_key = self.private_key
            sighash_flags = SIGHASH_ALL

            if type(input) is tuple:
                l = len(input)
                assert l>=2 and l<=4

                spend_tx = input[0]

                n = input[1]
                assert type(n) is int
                assert n < len(spend_tx.vout) and n >= -len(spend_tx.vout)
                if n < 0:
                    n += len(spend_tx.vout)

                if l>=3:
                    private_key = input[2]

                if l>=4:
                    sighash_flags = input[3]
            else:
                spend_tx = input
                # Find the first output that is not provably non-spendable
                while n<len(spend_tx.vout) and len(spend_tx.vout[n].scriptPubKey)>=2 and spend_tx.vout[n].scriptPubKey[0]==OP_FALSE and spend_tx.vout[n].scriptPubKey[1]==OP_RETURN:
                    n+=1
                assert n < len(spend_tx.vout)

            spent_outputs.append(SpentOutput(spend_tx, n, private_key, sighash_flags))

        if pubkey is None:
            pubkey = self.public_key
        if scriptPubKey is None:
            scriptPubKey = CScript([OP_DUP, OP_HASH160, hash160(pubkey), OP_EQUALVERIFY, OP_CHECKSIG])

        if pubkey_change is None:
            pubkey_change = pubkey
        if scriptPubKey_change is None:
            scriptPubKey_change = CScript([OP_DUP, OP_HASH160, hash160(pubkey_change), OP_EQUALVERIFY, OP_CHECKSIG])

        tx = CTransaction()
        for so in spent_outputs:
            tx.vin.append(CTxIn(COutPoint(so.tx.sha256, so.n), b"", 0xffffffff))
        for i in range(num_outputs):
            assert value is not None
            tx.vout.append(CTxOut(value, scriptPubKey))

        if tx_customize_func is not None:
            tx_customize_func(tx)
            assert len(tx.vin) == len(spent_outputs)

        def sign_all_inputs_in_tx():
            assert len(tx.vin) >= len(spent_outputs)
            for i in range(len(spent_outputs)):
                so = spent_outputs[i]
                spend_txout = so.tx.vout[so.n]

                if spend_txout.scriptPubKey == CScript([OP_TRUE]):
                    tx.vin[i].scriptSig = b""
                    continue

                self.sign_tx_p2pkh_input(tx, i, spend_txout, so.private_key, so.sighash_flags, no_rehash=True)

        if fee is not None or fee_rate is not None:
            value_change = 0
            for i in range(len(spent_outputs)):
                so = spent_outputs[i]
                spend_txout = so.tx.vout[so.n]
                value_change += spend_txout.nValue

            for vout in tx.vout:
                value_change -= vout.nValue

            if fee is not None:
                value_change -= fee

            # add a 'change' output
            tx.vout.append(CTxOut(value_change, scriptPubKey_change))

            if fee_rate is not None:
                # Create dummy signatures to be able to get the correct size of transaction
                # NOTE: Correct size of a transaction could be obtained in a more efficient (and more complex) way,
                #       but since performance is not a priority here, we don't bother. Even with this, signing the
                #       transaction again later might change its size by 1 byte or so, which is also not accounted
                #       for here.
                sign_all_inputs_in_tx()
                value_change -= int(fee_rate * len(tx.serialize()) // 1000)
                tx.vout[-1].nValue = value_change

        sign_all_inputs_in_tx()

        tx.calc_sha256()

        return tx


def create_simple_chain(conn, num_blocks=120, scriptPubKey=None):
    """
    Create 'num_blocks' number of empty blocks after the current tip, send them to node via NodeConn P2P connection 'conn', wait until the last one becomes the tip and return an array of them.

    If scriptPubKey is specified, output of coinbases transaction in these blocks is locked with it, otherwise it is trivially spendable (OP_TRUE).

    This method is intended to provide a simple and fast way to create spendable outputs on demand in tests.
    """

    # Get current best block info from node
    tip_hash = conn.rpc.getbestblockhash()
    tip_time = int(conn.rpc.getblock(tip_hash)['time'])
    tip_height = int(conn.rpc.getblockcount())

    conn.rpc.log.info(f"Creating {num_blocks} blocks after current tip (height={tip_height}, hash={tip_hash})")
    blocks = []
    for i in range(num_blocks):
        txcb = create_coinbase(tip_height+1)
        if scriptPubKey != None:
            txcb.vout[0].scriptPubKey = scriptPubKey
        else:
            txcb.vout[0].scriptPubKey = CScript([OP_TRUE])
        txcb.rehash()
        block = create_block(int(tip_hash, 16), txcb, tip_time+1)
        block.nNonce = 0
        block.solve()
        conn.cb.send_message(msg_block(block))
        tip_hash = block.hash
        tip_height += 1
        tip_time = block.nTime
        blocks.append(block)

    conn.rpc.log.info(f"Waiting for node to reach new tip (height={tip_height}, hash={tip_hash})")
    conn.rpc.waitforblockheight(tip_height)
    assert conn.rpc.getbestblockhash() == tip_hash

    return blocks


# The function prepares a number of spendable outputs
# Params:
# no_blocks - number of blocks we want to mine
# no_outputs - number of outputs the fun. returns in out list
# block_0 - was block 0 already mined outside the function and we need to save output
# start_block - a number at which we want to start mining blocks
# node - a node that sends block message (not needed if using TestInstance)
# Return values:
# test - TestInstance object
# out - list of spendable outputs
# start_block+no_blocks - last mined block
def prepare_init_chain(chain, no_blocks, no_outputs, block_0=True, start_block=5000, node=None):
    # Create a new block
    block = chain.next_block
    save_spendable_output=chain.save_spendable_output
    get_spendable_output=chain.get_spendable_output
    if block_0:
        save_spendable_output()
    # Now we need that block to mature so we can spend the coinbase.
    test = None
    if not node:
        test = TestInstance(sync_every_block=False)
        for i in range(no_blocks):
            block(start_block + i)
            test.blocks_and_transactions.append([chain.tip, True])
            save_spendable_output()
    else:
        for i in range(no_blocks):
            b = block(start_block + i)
            save_spendable_output()
            node.send_message(msg_block(b))

    # collect spendable outputs now to avoid cluttering the code later on
    out = []
    for i in range(no_outputs):
        out.append(get_spendable_output())

    return test, out, start_block+no_blocks

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
        # Used for next distinct script
        self._script_number = 0

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

    def create_distinct_script(self):
        self._script_number += 1
        return CScript([self._script_number, OP_RETURN])

    def next_block(self, number, spend=None, script=CScript([OP_TRUE]), block_size=0, extra_sigops=0, extra_txns=0, additional_coinbase_value=0, do_solve_block=True, coinbase_pubkey=None, coinbase_key=None, simple_output=False, version=None):
        if self.tip == None:
            base_block_hash = self._genesis_hash
            block_time = int(time.time()) + 1
        else:
            base_block_hash = self.tip.sha256
            block_time = self.tip.nTime + 1

        if not coinbase_pubkey and coinbase_key:
            coinbase_pubkey = coinbase_key.get_pubkey()

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
            if type(spend) == list:
                for s in spend:
                    coinbase.vout[0].nValue += s.tx.vout[s.n].nValue - 1
                    coinbase.rehash()
                block = create_block(base_block_hash, coinbase, block_time)
                for s in spend:
                    # Spend 1 satoshi
                    tx = create_transaction(s.tx, s.n, b"", 1, script)
                    sign_tx(tx, s.tx, s.n, coinbase_key)
                    self.add_transactions_to_block(block, [tx])
                    block.hashMerkleRoot = block.calc_merkle_root()
            else:
                block = create_block(base_block_hash, coinbase, block_time)
                if simple_output:
                    coinbase.vout[0].nValue += spend.tx.vout[spend.n].nValue - 1
                    coinbase.rehash()
                    tx = create_transaction(spend.tx, spend.n, b"", 1, script)  # spend 1 satoshi

                    if script == CScript([OP_TRUE]):
                        tx.vout.append(CTxOut(1, self.create_distinct_script()))
                        coinbase.vout[0].nValue -= 1
                        coinbase.rehash()

                    sign_tx(tx, spend.tx, spend.n, coinbase_key)
                    self.add_transactions_to_block(block, [tx])
                    block.hashMerkleRoot = block.calc_merkle_root()
                else:
                    # Make sure we have plenty engough to spend going forward.
                    spendable_outputs = deque([spend])
                    coinbase.vout[0].nValue -= 1

                    def get_base_transaction():
                        # Create the new transaction
                        tx = CTransaction()
                        # Spend from one of the spendable outputs
                        spend = spendable_outputs.popleft()
                        # we guess generously the extra cash needed, but we will assert its correctness on use
                        extraCash = 10
                        input_value = spend.tx.vout[spend.n].nValue - extraCash
                        tx.vin.append(CTxIn(COutPoint(spend.tx.sha256, spend.n)))
                        # Add spendable outputs
                        spend_amount = int(input_value / 4)
                        fee = input_value - spend_amount * 4 + extraCash
                        coinbase.vout[0].nValue += fee
                        for i in range(4):
                            tx.vout.append(CTxOut(spend_amount, CScript([OP_TRUE])))
                            spendable_outputs.append(PreviousSpendableOutput(tx, i))
                        return tx, extraCash

                    def CreateCTxOut(toSpend_, extraCash_, script_):
                        assert (extraCash_ - toSpend_ >= 0)
                        coinbase.vout[0].nValue -= toSpend_
                        return extraCash_ - toSpend_, CTxOut(toSpend_, script_)

                    tx, extraCash = get_base_transaction()

                    # Make it the same format as transaction added for padding and save the size.
                    tx.rehash()
                    base_tx_size = len(tx.serialize())

                    # If a specific script is required, add it.
                    if script != None:
                        extraCash, txout = CreateCTxOut(1, extraCash, script)
                        tx.vout.append(txout)

                    extraCash, txout = CreateCTxOut(1, extraCash, self.create_distinct_script())
                    tx.vout.append(txout)

                    # Add the transaction to the block
                    self.add_transactions_to_block(block, [tx])

                    # Add transaction until we reach the expected transaction count
                    for _ in range(extra_txns):
                        newtx, extraCash = get_base_transaction()
                        self.add_transactions_to_block(block, [newtx])

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
                        tx, extraCash = get_base_transaction()

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

                        extraCash, txout = CreateCTxOut(1, extraCash, script_output)
                        tx.vout.append(txout)

                        # Add the tx to the list of transactions to be included
                        # in the block.
                        self.add_transactions_to_block(block, [tx])
                        current_block_size += len(tx.serialize())

                    # Now that we added a bunch of transaction, we need to recompute
                    # the merkle root.
                    coinbase.rehash()
                    block.hashMerkleRoot = block.calc_merkle_root()

        # Check that the block size is what's expected
        if block_size > 0:
            assert_equal(len(block.serialize()), block_size)

        # check that extra_sigops are included
        if extra_sigops > 0:
            raise AssertionError("Can not fit %s extra_sigops in a block size of %s" % (extra_sigops_orig, block_size))

        if version != None:
            block.nVersion = version

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
