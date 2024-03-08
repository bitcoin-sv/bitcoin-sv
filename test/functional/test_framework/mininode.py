#!/usr/bin/env python3
# Copyright (c) 2010 ArtForz -- public domain half-a-node
# Copyright (c) 2012 Jeff Garzik
# Copyright (c) 2010-2016 The Bitcoin Core developers
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""Bitcoin P2P network half-a-node.

This python code was modified from ArtForz' public domain  half-a-node, as
found in the mini-node branch of http://github.com/jgarzik/pynode.

NodeConn: an object which manages p2p connectivity to a bitcoin node
NodeConnCB: a base class that describes the interface for receiving
            callbacks with network messages from a NodeConn
CBlock, CTransaction, CBlockHeader, CTxIn, CTxOut, etc....:
    data structures that should map to corresponding structures in
    bitcoin/primitives
msg_block, msg_tx, msg_headers, etc.:
    data structures that represent network messages
ser_*, deser_*: functions that handle serialization/deserialization
"""

import asyncore
import binascii
from codecs import encode
from collections import defaultdict
import copy
import hashlib
from contextlib import contextmanager
import ecdsa
from io import BytesIO
import logging
import random
import socket
import struct
import sys
import time
from itertools import chain
from math import ceil
from threading import RLock, Thread
import uuid

from test_framework.siphash import siphash256
from test_framework.util import hex_str_to_bytes, bytes_to_hex_str, wait_until
from test_framework.streams import StreamType

BIP0031_VERSION = 60000
MY_VERSION = 70016 # Large payload (>4GB) version
MY_SUBVERSION = b"/python-mininode-tester:0.0.3/"
# from version 70001 onwards, fRelay should be appended to version messages (BIP37)
MY_RELAY = 1

MAX_INV_SZ = 50000
MAX_PROTOCOL_RECV_PAYLOAD_LENGTH = 2 * 1024 * 1024
LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH = 1 * 1024 * 1024

COIN = 100000000  # 1 btc in satoshis

NODE_NETWORK = (1 << 0)
NODE_GETUTXO = (1 << 1)
NODE_BLOOM = (1 << 2)
NODE_WITNESS = (1 << 3)
NODE_XTHIN = (1 << 4)
NODE_BITCOIN_CASH = (1 << 5)

# Howmuch data will be read from the network at once
READ_BUFFER_SIZE = 1024 * 256

logger = logging.getLogger("TestFramework.mininode")

# Keep our own socket map for asyncore, so that we can track disconnects
# ourselves (to workaround an issue with closing an asyncore socket when
# using select)
mininode_socket_map = dict()

# One lock for synchronizing all data access between the networking thread (see
# NetworkThread below) and the thread running the test logic.  For simplicity,
# NodeConn acquires this lock whenever delivering a message to a NodeConnCB,
# and whenever adding anything to the send buffer (in send_message()).  This
# lock should be acquired in the thread running the test logic to synchronize
# access to any data shared with the NodeConnCB or NodeConn.
mininode_lock = RLock()

# Lock used to synchronize access to data required by loop running in NetworkThread.
# It must be locked, for example, when adding new NodeConn object, otherwise loop in
# NetworkThread may try to access partially constructed object.
network_thread_loop_lock = RLock()

# Network thread acquires network_thread_loop_lock at start of each iteration and releases
# it at the end. Since the next iteration is run immediately after that, lock is acquired
# almost all of the time making it difficult for other threads to also acquire this lock.
# To work around this problem, NetworkThread first acquires network_thread_loop_intent_lock
# and immediately releases it before acquiring network_thread_loop_lock.
# Other threads (e.g. the ones calling NodeConn constructor) acquire both locks before
# proceeding. The end result is that other threads wait at most one iteration of loop in
# NetworkThread.
network_thread_loop_intent_lock = RLock()

# ports used by chain type
NETWORK_PORTS = {
    "mainnet" : 8333,
    "testnet3" : 18333,
    "stn" : 9333,
    "regtest" : 18444
}

# Serialization/deserialization tools


def sha256(s):
    return hashlib.new('sha256', s).digest()


def ripemd160(s):
    return hashlib.new('ripemd160', s).digest()


def hash256(s):
    return sha256(sha256(s))


def ser_compact_size(l):
    r = b""
    if l < 253:
        r = struct.pack("B", l)
    elif l < 0x10000:
        r = struct.pack("<BH", 253, l)
    elif l < 0x100000000:
        r = struct.pack("<BI", 254, l)
    else:
        r = struct.pack("<BQ", 255, l)
    return r


def generator_based_serializator(fn):
    def decorated(object_collection, *args, **kwargs):
        first_elem = ser_compact_size(len(object_collection))
        obj_generator = fn(object_collection, *args, **kwargs)
        return b"".join(chain((first_elem,), obj_generator))

    return decorated


def deser_compact_size(f):
    nit = struct.unpack("<B", f.read(1))[0]
    if nit == 253:
        nit = struct.unpack("<H", f.read(2))[0]
    elif nit == 254:
        nit = struct.unpack("<I", f.read(4))[0]
    elif nit == 255:
        nit = struct.unpack("<Q", f.read(8))[0]
    return nit


def ser_varint(v):
    r = b""
    length = 0
    while True:
        r += struct.pack("<B", (v & 0x7F) | (0x80 if length > 0 else 0x00))
        if(v <= 0x7F):
            return r[::-1] # Need as little-endian
        v = (v >> 7) - 1
        length += 1


def deser_varint(f):
    ntot = 0
    while True:
        n = struct.unpack("<B", f.read(1))[0]
        ntot = (n << 7) | (n & 0x7F)
        if((n & 0x80) == 0):
            return ntot


def deser_string(f):
    nit = deser_compact_size(f)
    return f.read(nit)


@generator_based_serializator
def ser_string(s):
    return (s,) # return tuple with single member


def deser_uint256(f):
    r = 0
    for i in range(8):
        t = struct.unpack("<I", f.read(4))[0]
        r += t << (i * 32)
    return r


def ser_uint256(u):
    rs = b""
    for i in range(8):
        rs += struct.pack("<I", u & 0xFFFFFFFF)
        u >>= 32
    return rs


def uint256_from_str(s):
    r = 0
    t = struct.unpack("<IIIIIIII", s[:32])
    for i in range(8):
        r += t[i] << (i * 32)
    return r


def uint256_from_compact(c):
    nbytes = (c >> 24) & 0xFF
    v = (c & 0xFFFFFF) << (8 * (nbytes - 3))
    return v


def deser_vector(f, c):
    nit = deser_compact_size(f)
    r = []
    for i in range(nit):
        t = c()
        t.deserialize(f)
        r.append(t)
    return r


# ser_function_name: Allow for an alternate serialization function on the
# entries in the vector.
@generator_based_serializator
def ser_vector(l, ser_function_name=""):
    # using generator because of need for lazy evaluation
    return (getattr(i, ser_function_name, i.serialize)() for i in l)


def deser_uint256_vector(f):
    nit = deser_compact_size(f)
    r = []
    for i in range(nit):
        t = deser_uint256(f)
        r.append(t)
    return r


@generator_based_serializator
def ser_uint256_vector(l):
    return (ser_uint256(i) for i in l)


def deser_string_vector(f):
    nit = deser_compact_size(f)
    r = []
    for i in range(nit):
        t = deser_string(f)
        r.append(t)
    return r


@generator_based_serializator
def ser_string_vector(l):
    return (ser_string(sv) for sv in l)


def deser_int_vector(f):
    nit = deser_compact_size(f)
    r = []
    for i in range(nit):
        t = struct.unpack("<i", f.read(4))[0]
        r.append(t)
    return r


@generator_based_serializator
def ser_int_vector(l):
    return (struct.pack("<i", i) for i in l)


def deser_varint_vector(f):
    nit = deser_varint(f)
    r = []
    for i in range(nit):
        t = deser_varint(f)
        r.append(t)
    return r


def ser_varint_vector(l):
    r = ser_varint(len(l))
    for v in l:
        r += ser_varint(v)
    return r


def deser_byte_array(s):
    b = bytearray()
    b.extend(s[1:])
    return b


def ser_byte_array(s):
    return b"".join((
        struct.pack("<B", 0),
        s.bytes,
    ))


def deser_optional(typename, f):
    hasVal = struct.unpack("<b", f.read(1))[0]
    if(hasVal):
        val = typename()
        val.deserialize(f)
        return val
    else:
        return None


def ser_optional(o):
    r = b""
    if(o):
        r += struct.pack("<b", 1)
        r += o.serialize()
    else:
        r += struct.pack("<b", 0)
    return r


# Deserialize from a hex string representation (eg from RPC)
def FromHex(obj, hex_string):
    obj.deserialize(BytesIO(hex_str_to_bytes(hex_string)))
    return obj


# Convert a binary-serializable object to hex (eg for submission via RPC)
def ToHex(obj):
    return bytes_to_hex_str(obj.serialize())


# Serialise a UUID association ID as a stream of bytes for sending over the network
def serialise_uuid_associd(assocId):
    assocIdBytes = bytes()
    if(assocId):
        assocIdPlusType = b"".join((
            struct.pack("<B", 0),
            assocId.bytes
        ))
        assocIdBytes = ser_string(assocIdPlusType)
    return assocIdBytes


# Deserialise an association ID from the network into a UUID
def deserialise_uuid_associd(raw):
    return uuid.UUID(bytes=raw[1:])


# Create a new random association ID
def create_association_id():
    return uuid.uuid4()


# Objects that map to bitcoind objects, which can be serialized/deserialized


# Because the nVersion field has not been passed before the VERSION message the protocol uses an old format for the CAddress (missing nTime)
# This class handles that old format
class CAddressInVersion(object):
    def __init__(self, ip="0.0.0.0", port=0):
        self.nServices = 1
        self.pchReserved = b"\x00" * 10 + b"\xff" * 2  # ip is 16 bytes on wire to handle v6
        self.ip = ip
        self.port = port

    def deserialize(self, f):
        self.nServices = struct.unpack("<Q", f.read(8))[0]
        self.pchReserved = f.read(12)
        self.ip = socket.inet_ntoa(f.read(4))
        self.port = struct.unpack(">H", f.read(2))[0]

    def serialize(self):
        r = b"".join((
            struct.pack("<Q", self.nServices),
            self.pchReserved,
            socket.inet_aton(self.ip),
            struct.pack(">H", self.port),))
        return r

    def __repr__(self):
        return "CAddressInVersion(nServices=%i ip=%s port=%i)" % (self.nServices, self.ip, self.port)


# Handle new-style CAddress objects (with nTime)
class CAddress():
    def __init__(self, ip="0.0.0.0", port=0):
        self.nServices = 1
        self.nTime = int(time.time())
        self.pchReserved = b"\x00" * 10 + b"\xff" * 2  # ip is 16 bytes on wire to handle v6
        self.ip = ip
        self.port = port

    def deserialize(self, f):
        self.nTime = struct.unpack("<L", f.read(4))[0]
        self.nServices = struct.unpack("<Q", f.read(8))[0]
        self.pchReserved = f.read(12)
        self.ip = socket.inet_ntoa(f.read(4))
        self.port = struct.unpack(">H", f.read(2))[0]

    def serialize(self):
        r = b""
        r += struct.pack("<L", self.nTime)
        r += struct.pack("<Q", self.nServices)
        r += self.pchReserved
        r += socket.inet_aton(self.ip)
        r += struct.pack(">H", self.port)
        return r

    def __repr__(self):
        return "CAddress(nServices=%i ip=%s port=%i time=%d)" % (self.nServices, self.ip, self.port, self.nTime)


class CInv():

    ERROR = 0
    TX = 1
    BLOCK = 2
    COMPACT_BLOCK = 4
    DATAREF_TX = 5

    typemap = {
        ERROR: "Error",
        TX: "TX",
        BLOCK: "Block",
        COMPACT_BLOCK: "CompactBlock",
        DATAREF_TX: "DatarefTx"
    }

    def __init__(self, t=ERROR, h=0):
        self.type = t
        self.hash = h

    def deserialize(self, f):
        self.type = struct.unpack("<i", f.read(4))[0]
        self.hash = deser_uint256(f)

    def serialize(self):
        r = b"".join((
            struct.pack("<i", self.type),
            ser_uint256(self.hash),))
        return r

    def __repr__(self):
        return "CInv(type=%s hash=%064x)" \
            % (self.typemap[self.type], self.hash)

    def estimateMaxInvElements(max_payload_length=MAX_PROTOCOL_RECV_PAYLOAD_LENGTH):
        return int((max_payload_length - 8) / (4 + 32))


class CProtoconf():
    def __init__(self, number_of_fields=2, max_recv_payload_length=0, stream_policies=b"Default"):
        self.number_of_fields = number_of_fields
        self.max_recv_payload_length = max_recv_payload_length
        self.stream_policies = stream_policies

    def deserialize(self, f):
        self.number_of_fields = deser_compact_size(f)
        self.max_recv_payload_length = struct.unpack("<i", f.read(4))[0]
        if self.number_of_fields > 1:
            self.stream_policies = deser_string(f)

    def serialize(self):
        r = b""
        r += ser_compact_size(self.number_of_fields)
        r += struct.pack("<i", self.max_recv_payload_length)
        if self.number_of_fields > 1:
            r += ser_string(self.stream_policies)
        return r

    def __repr__(self):
        return "CProtoconf(number_of_fields=%064x max_recv_payload_length=%064x stream_policies=%s)" \
            % (self.number_of_fields, self.max_recv_payload_length, self.stream_policies)


class CBlockLocator():
    def __init__(self, have=[]):
        self.nVersion = MY_VERSION
        self.vHave = have

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        self.vHave = deser_uint256_vector(f)

    def serialize(self):
        r = b"".join((
            struct.pack("<i", self.nVersion),
            ser_uint256_vector(self.vHave),))
        return r

    def __repr__(self):
        return "CBlockLocator(nVersion=%i vHave=%s)" \
            % (self.nVersion, repr(self.vHave))


class COutPoint():
    def __init__(self, hash=0, n=0):
        self.hash = hash
        self.n = n

    def deserialize(self, f):
        self.hash = deser_uint256(f)
        self.n = struct.unpack("<I", f.read(4))[0]

    def serialize(self):
        r = b"".join((
            ser_uint256(self.hash),
            struct.pack("<I", self.n),))
        return r

    def __hash__(self):
        return self.hash + self.n

    def __eq__(self, other):
        return self.n == other.n and self.hash == other.hash

    def __repr__(self):
        return "COutPoint(hash=%064x n=%i)" % (self.hash, self.n)


class CTxIn():
    def __init__(self, outpoint=None, scriptSig=b"", nSequence=0):
        if outpoint is None:
            self.prevout = COutPoint()
        else:
            self.prevout = outpoint
        self.scriptSig = scriptSig
        self.nSequence = nSequence

    def deserialize(self, f):
        self.prevout = COutPoint()
        self.prevout.deserialize(f)
        self.scriptSig = deser_string(f)
        self.nSequence = struct.unpack("<I", f.read(4))[0]

    def serialize(self):
        r = b"".join((
            self.prevout.serialize(),
            ser_string(self.scriptSig),
            struct.pack("<I", self.nSequence),))
        return r

    def __repr__(self):
        return "CTxIn(prevout=%s scriptSig=%s nSequence=%i)" \
            % (repr(self.prevout), bytes_to_hex_str(self.scriptSig),
               self.nSequence)


class CTxOut():
    def __init__(self, nValue=0, scriptPubKey=b""):
        self.nValue = nValue
        self.scriptPubKey = scriptPubKey

    def deserialize(self, f):
        self.nValue = struct.unpack("<q", f.read(8))[0]
        self.scriptPubKey = deser_string(f)

    def serialize(self):
        r = b"".join((
            struct.pack("<q", self.nValue),
            ser_string(self.scriptPubKey),))
        return r

    def __repr__(self):
        return "CTxOut(nValue=%i.%08i scriptPubKey=%s)" \
            % (self.nValue // COIN, self.nValue % COIN,
               bytes_to_hex_str(self.scriptPubKey))


class CTransaction():
    def __init__(self, tx=None):
        if tx is None:
            self.nVersion = 1
            self.vin = []
            self.vout = []
            self.nLockTime = 0
            self.sha256 = None
            self.hash = None
        else:
            self.nVersion = tx.nVersion
            self.vin = copy.deepcopy(tx.vin)
            self.vout = copy.deepcopy(tx.vout)
            self.nLockTime = tx.nLockTime
            self.sha256 = tx.sha256
            self.hash = tx.hash

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        self.vin = deser_vector(f, CTxIn)
        self.vout = deser_vector(f, CTxOut)
        self.nLockTime = struct.unpack("<I", f.read(4))[0]
        self.sha256 = None
        self.hash = None

    def serialize(self):
        r = b"".join((
            struct.pack("<i", self.nVersion),
            ser_vector(self.vin),
            ser_vector(self.vout),
            struct.pack("<I", self.nLockTime),))
        return r

    # Recalculate the txid
    def rehash(self):
        self.sha256 = None
        self.calc_sha256()

    # self.sha256 and self.hash -- those are expected to be the txid.
    def calc_sha256(self):
        if self.sha256 is None:
            self.sha256 = uint256_from_str(hash256(self.serialize()))
        self.hash = encode(
            hash256(self.serialize())[::-1], 'hex_codec').decode('ascii')

    def is_valid(self):
        self.calc_sha256()
        for tout in self.vout:
            if tout.nValue < 0 or tout.nValue > 21000000 * COIN:
                return False
        return True

    def __repr__(self):
        self.rehash()
        return "CTransaction(hash=%s nVersion=%i vin=%s vout=%s nLockTime=%i)" \
            % (self.hash, self.nVersion, repr(self.vin), repr(self.vout), self.nLockTime)


class CBlockHeader():
    def __init__(self, header=None, json_notification=None):
        if json_notification is None:
            if header is None:
                self.set_null()
            else:
                self.nVersion = header.nVersion
                self.hashPrevBlock = header.hashPrevBlock
                self.hashMerkleRoot = header.hashMerkleRoot
                self.nTime = header.nTime
                self.nBits = header.nBits
                self.nNonce = header.nNonce
                self.sha256 = header.sha256
                self.hash = header.hash
                self.calc_sha256()
        else:
            self.nVersion = json_notification["version"]
            self.hashPrevBlock = uint256_from_str(hex_str_to_bytes(json_notification["hashPrevBlock"])[::-1])
            self.hashMerkleRoot = uint256_from_str(hex_str_to_bytes(json_notification["hashMerkleRoot"])[::-1])
            self.nTime = json_notification["time"]
            self.nBits = json_notification["bits"]
            self.nNonce = json_notification["nonce"]
            self.rehash()

    def set_null(self):
        self.nVersion = 1
        self.hashPrevBlock = 0
        self.hashMerkleRoot = 0
        self.nTime = 0
        self.nBits = 0
        self.nNonce = 0
        self.sha256 = None
        self.hash = None

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        self.hashPrevBlock = deser_uint256(f)
        self.hashMerkleRoot = deser_uint256(f)
        self.nTime = struct.unpack("<I", f.read(4))[0]
        self.nBits = struct.unpack("<I", f.read(4))[0]
        self.nNonce = struct.unpack("<I", f.read(4))[0]
        self.sha256 = None
        self.hash = None

    def serialize(self):
        r = b"".join((
            struct.pack("<i", self.nVersion),
            ser_uint256(self.hashPrevBlock),
            ser_uint256(self.hashMerkleRoot),
            struct.pack("<I", self.nTime),
            struct.pack("<I", self.nBits),
            struct.pack("<I", self.nNonce),))
        return r

    def calc_sha256(self):
        if self.sha256 is None:
            r = b"".join((
                struct.pack("<i", self.nVersion),
                ser_uint256(self.hashPrevBlock),
                ser_uint256(self.hashMerkleRoot),
                struct.pack("<I", self.nTime),
                struct.pack("<I", self.nBits),
                struct.pack("<I", self.nNonce),))
            self.sha256 = uint256_from_str(hash256(r))
            self.hash = encode(hash256(r)[::-1], 'hex_codec').decode('ascii')

    def rehash(self):
        self.sha256 = None
        self.calc_sha256()
        return self.sha256

    def __repr__(self):
        self.rehash()
        return "CBlockHeader(hash=%s nVersion=%i hashPrevBlock=%064x hashMerkleRoot=%064x nTime=%s nBits=%08x nNonce=%08x)" \
            % (self.hash, self.nVersion, self.hashPrevBlock, self.hashMerkleRoot,
               time.ctime(self.nTime), self.nBits, self.nNonce)


class TSCMerkleProof():
    def __init__(self):
        self.set_null()

    class Node():
        def __init__(self):
            self.set_null()

        def set_null(self):
            self.type = 0
            self.value = 0

        def deserialize(self, f):
            self.type = struct.unpack("<b", f.read(1))[0]
            if self.type != 0:
                raise ValueError("Unsupported value of type field (%i) in TSCMerkleProof.Node" % self.type)
            self.value = deser_uint256(f)

        def serialize(self):
            r = b"".join((
                struct.pack("<b", self.type),
                ser_uint256(self.value)
            ))
            return r

        def __repr__(self):
            return "(%i, %064x)" % (self.type, self.value)

    def set_null(self):
        self.flags = 0
        self.index = 0
        self.txOrId = 0
        self.target = 0
        self.nodes = None

    def deserialize(self, f):
        self.flags = struct.unpack("<b", f.read(1))[0]
        if self.flags != 0:
            raise ValueError("Unsupported value of flags field (%i) in TSCMerkleProof" % self.flags)
        self.index = deser_compact_size(f)
        self.txOrId = deser_uint256(f)
        self.target = deser_uint256(f)
        self.nodes = deser_vector(f, TSCMerkleProof.Node)

    def serialize(self):
        r = b"".join((
            struct.pack("<b", self.flags),
            ser_compact_size(self.index),
            ser_uint256(self.txOrId),
            ser_uint256(self.target),
            ser_vector(self.nodes)
        ))
        return r

    def __repr__(self):
        return "TSCMerkleProof(flags=%i index=%i txOrId=%064x target=%064x nodes=%s)" \
            % (self.flags, self.index, self.txOrId, self.target, repr(self.nodes))


class CBlockHeaderEnriched(CBlockHeader):

    class TxnAndProof():
        def __init__(self, txnproof=None):
            if txnproof is None:
                self.set_null()
            else:
                self.tx = txnproof.tx
                self.proof = txnproof.proof

        def set_null(self):
            self.tx = None
            self.proof = None

        def deserialize(self, f):
            self.tx = CTransaction()
            self.tx.deserialize(f)
            self.proof = TSCMerkleProof()
            self.proof.deserialize(f)

        def serialize(self):
            r = b""
            r += self.tx.serialize()
            r += self.proof.serialize()
            return r

        def __repr__(self):
            return "TxnAndProof(tx=%s proof=%s)" % (repr(self.tx), repr(self.proof))

    def __init__(self, header=None):
        super(CBlockHeaderEnriched, self).__init__(header)
        if header is None:
            self.set_null()
        else:
            self.nTx = header.nTx
            self.noMoreHeaders = header.noMoreHeaders
            self.coinbaseTxProof = header.coinbaseTxProof
            self.minerInfoProof = header.minerInfoProof

    def set_null(self):
        super(CBlockHeaderEnriched, self).set_null()
        self.nTx = 0
        self.noMoreHeaders = False
        self.coinbaseTxProof = None
        self.minerInfoProof = None

    def deserialize(self, f):
        super(CBlockHeaderEnriched, self).deserialize(f)
        self.nTx = deser_compact_size(f)
        self.noMoreHeaders = struct.unpack("<b", f.read(1))[0]
        self.coinbaseTxProof = deser_optional(self.TxnAndProof, f)
        self.minerInfoProof = deser_optional(self.TxnAndProof, f)

    def serialize(self):
        r = b"".join((super(CBlockHeaderEnriched, self).serialize(),
                      ser_compact_size(self.nTx),
                      struct.pack("<b", self.noMoreHeaders),
                      ser_optional(self.coinbaseTxProof),
                      ser_optional(self.minerInfoProof)))
        return r

    def __repr__(self):
        return "CBlockHeaderEnriched(nVersion=%i hashPrevBlock=%064x hashMerkleRoot=%064x nTime=%s nBits=%08x nNonce=%08x nTx=%i noMoreHeaders=%i coinbaseTxProof=%s minerInfoProof=%s)" \
            % (self.nVersion, self.hashPrevBlock, self.hashMerkleRoot,
               time.ctime(self.nTime), self.nBits, self.nNonce, self.nTx, self.noMoreHeaders, repr(self.coinbaseTxProof), repr(self.minerInfoProof))


class CBlock(CBlockHeader):

    def __init__(self, header=None):
        super(CBlock, self).__init__(header)
        self.vtx = []

    def deserialize(self, f):
        super(CBlock, self).deserialize(f)
        self.vtx = deser_vector(f, CTransaction)

    def serialize(self):
        r = b"".join((
            super(CBlock, self).serialize(),
            ser_vector(self.vtx),))
        return r

    # Calculate the merkle root given a vector of transaction hashes
    def get_merkle_root(self, hashes):
        while len(hashes) > 1:
            newhashes = []
            for i in range(0, len(hashes), 2):
                i2 = min(i + 1, len(hashes) - 1)
                newhashes.append(hash256(hashes[i] + hashes[i2]))
            hashes = newhashes
        return uint256_from_str(hashes[0])

    def calc_merkle_root(self):
        hashes = []
        for tx in self.vtx:
            tx.calc_sha256()
            hashes.append(ser_uint256(tx.sha256))
        return self.get_merkle_root(hashes)

    def is_valid(self):
        self.calc_sha256()
        target = uint256_from_compact(self.nBits)
        if self.sha256 > target:
            return False
        for tx in self.vtx:
            if not tx.is_valid():
                return False
        if self.calc_merkle_root() != self.hashMerkleRoot:
            return False
        return True

    def solve(self):
        self.rehash()
        target = uint256_from_compact(self.nBits)
        while self.sha256 > target:
            self.nNonce += 1
            self.rehash()

    def __repr__(self):
        self.rehash()
        return "CBlock(hash=%s nVersion=%i hashPrevBlock=%064x hashMerkleRoot=%064x nTime=%s nBits=%08x nNonce=%08x vtx=%s)" \
            % (self.hash, self.nVersion, self.hashPrevBlock, self.hashMerkleRoot,
               time.ctime(self.nTime), self.nBits, self.nNonce, repr(self.vtx))


class CUnsignedAlert():
    def __init__(self):
        self.nVersion = 1
        self.nRelayUntil = 0
        self.nExpiration = 0
        self.nID = 0
        self.nCancel = 0
        self.setCancel = []
        self.nMinVer = 0
        self.nMaxVer = 0
        self.setSubVer = []
        self.nPriority = 0
        self.strComment = b""
        self.strStatusBar = b""
        self.strReserved = b""

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        self.nRelayUntil = struct.unpack("<q", f.read(8))[0]
        self.nExpiration = struct.unpack("<q", f.read(8))[0]
        self.nID = struct.unpack("<i", f.read(4))[0]
        self.nCancel = struct.unpack("<i", f.read(4))[0]
        self.setCancel = deser_int_vector(f)
        self.nMinVer = struct.unpack("<i", f.read(4))[0]
        self.nMaxVer = struct.unpack("<i", f.read(4))[0]
        self.setSubVer = deser_string_vector(f)
        self.nPriority = struct.unpack("<i", f.read(4))[0]
        self.strComment = deser_string(f)
        self.strStatusBar = deser_string(f)
        self.strReserved = deser_string(f)

    def serialize(self):
        r = b"".join((
            struct.pack("<i", self.nVersion),
            struct.pack("<q", self.nRelayUntil),
            struct.pack("<q", self.nExpiration),
            struct.pack("<i", self.nID),
            struct.pack("<i", self.nCancel),
            ser_int_vector(self.setCancel),
            struct.pack("<i", self.nMinVer),
            struct.pack("<i", self.nMaxVer),
            ser_string_vector(self.setSubVer),
            struct.pack("<i", self.nPriority),
            ser_string(self.strComment),
            ser_string(self.strStatusBar),
            ser_string(self.strReserved),))
        return r

    def __repr__(self):
        return "CUnsignedAlert(nVersion %d, nRelayUntil %d, nExpiration %d, nID %d, nCancel %d, nMinVer %d, nMaxVer %d, nPriority %d, strComment %s, strStatusBar %s, strReserved %s)" \
            % (self.nVersion, self.nRelayUntil, self.nExpiration, self.nID,
               self.nCancel, self.nMinVer, self.nMaxVer, self.nPriority,
               self.strComment, self.strStatusBar, self.strReserved)


class CAlert():
    def __init__(self):
        self.vchMsg = b""
        self.vchSig = b""

    def deserialize(self, f):
        self.vchMsg = deser_string(f)
        self.vchSig = deser_string(f)

    def serialize(self):
        r = b"".join((
            ser_string(self.vchMsg),
            ser_string(self.vchSig),))
        return r

    def __repr__(self):
        return "CAlert(vchMsg.sz %d, vchSig.sz %d)" \
            % (len(self.vchMsg), len(self.vchSig))


class PrefilledTransaction():
    def __init__(self, index=0, tx=None):
        self.index = index
        self.tx = tx

    def deserialize(self, f):
        self.index = deser_compact_size(f)
        self.tx = CTransaction()
        self.tx.deserialize(f)

    def serialize(self):
        r = b"".join((
            ser_compact_size(self.index),
            self.tx.serialize(),))
        return r

    def __repr__(self):
        return "PrefilledTransaction(index=%d, tx=%s)" % (self.index, repr(self.tx))

# This is what we send on the wire, in a cmpctblock message.


class P2PHeaderAndShortIDs():
    def __init__(self):
        self.header = CBlockHeader()
        self.nonce = 0
        self.shortids_length = 0
        self.shortids = []
        self.prefilled_txn_length = 0
        self.prefilled_txn = []

    def deserialize(self, f):
        self.header.deserialize(f)
        self.nonce = struct.unpack("<Q", f.read(8))[0]
        self.shortids_length = deser_compact_size(f)
        for i in range(self.shortids_length):
            # shortids are defined to be 6 bytes in the spec, so append
            # two zero bytes and read it in as an 8-byte number
            self.shortids.append(
                struct.unpack("<Q", f.read(6) + b'\x00\x00')[0])
        self.prefilled_txn = deser_vector(f, PrefilledTransaction)
        self.prefilled_txn_length = len(self.prefilled_txn)

    def serialize(self):
        r = b"".join((
            self.header.serialize(),
            struct.pack("<Q", self.nonce),
            ser_compact_size(self.shortids_length),
            b"".join(struct.pack("<Q", x)[0:6] for x in self.shortids), # We only want the first 6 bytes
            ser_vector(self.prefilled_txn),))
        return r

    def __repr__(self):
        return "P2PHeaderAndShortIDs(header=%s, nonce=%d, shortids_length=%d, shortids=%s, prefilled_txn_length=%d, prefilledtxn=%s" % (repr(self.header), self.nonce, self.shortids_length, repr(self.shortids), self.prefilled_txn_length, repr(self.prefilled_txn))

# Calculate the BIP 152-compact blocks shortid for a given transaction hash


def calculate_shortid(k0, k1, tx_hash):
    expected_shortid = siphash256(k0, k1, tx_hash)
    expected_shortid &= 0x0000ffffffffffff
    return expected_shortid


# This version gets rid of the array lengths, and reinterprets the differential
# encoding into indices that can be used for lookup.
class HeaderAndShortIDs():
    def __init__(self, p2pheaders_and_shortids=None):
        self.header = CBlockHeader()
        self.nonce = 0
        self.shortids = []
        self.prefilled_txn = []

        if p2pheaders_and_shortids != None:
            self.header = p2pheaders_and_shortids.header
            self.nonce = p2pheaders_and_shortids.nonce
            self.shortids = p2pheaders_and_shortids.shortids
            last_index = -1
            for x in p2pheaders_and_shortids.prefilled_txn:
                self.prefilled_txn.append(
                    PrefilledTransaction(x.index + last_index + 1, x.tx))
                last_index = self.prefilled_txn[-1].index

    def to_p2p(self):
        ret = P2PHeaderAndShortIDs()
        ret.header = self.header
        ret.nonce = self.nonce
        ret.shortids_length = len(self.shortids)
        ret.shortids = self.shortids
        ret.prefilled_txn_length = len(self.prefilled_txn)
        ret.prefilled_txn = []
        last_index = -1
        for x in self.prefilled_txn:
            ret.prefilled_txn.append(
                PrefilledTransaction(x.index - last_index - 1, x.tx))
            last_index = x.index
        return ret

    def get_siphash_keys(self):
        header_nonce = self.header.serialize()
        header_nonce += struct.pack("<Q", self.nonce)
        hash_header_nonce_as_str = sha256(header_nonce)
        key0 = struct.unpack("<Q", hash_header_nonce_as_str[0:8])[0]
        key1 = struct.unpack("<Q", hash_header_nonce_as_str[8:16])[0]
        return [key0, key1]

    # Version 2 compact blocks use wtxid in shortids (rather than txid)
    def initialize_from_block(self, block, nonce=0, prefill_list=[0]):
        self.header = CBlockHeader(block)
        self.nonce = nonce
        self.prefilled_txn = [PrefilledTransaction(i, block.vtx[i])
                              for i in prefill_list]
        self.shortids = []
        [k0, k1] = self.get_siphash_keys()
        for i in range(len(block.vtx)):
            if i not in prefill_list:
                tx_hash = block.vtx[i].sha256
                self.shortids.append(calculate_shortid(k0, k1, tx_hash))

    def __repr__(self):
        return "HeaderAndShortIDs(header=%s, nonce=%d, shortids=%s, prefilledtxn=%s" % (repr(self.header), self.nonce, repr(self.shortids), repr(self.prefilled_txn))


# callback message for dsnt-enabled transactions
class CallbackMessage():

    # 127.0.0.1 as network-order bytes
    LOCAL_HOST_IP = 0x7F000001
    MAX_INT64 = 0xFFFFFFFFFFFFFFFF
    IPv6_version = 129
    IPv4_version = 1

    def __init__(self, version=1, ip_addresses=[LOCAL_HOST_IP], inputs=[0]):
        self.version = version
        self.ip_addresses = ip_addresses
        self.ip_address_count = len(ip_addresses)
        self.inputs = inputs

    def ser_addrs(self, addrs):
        rs = b""
        for addr in addrs:
            if (self.version == self.IPv6_version):
                rs += struct.pack('>QQ', (addr >> 64) & self.MAX_INT64, addr & self.MAX_INT64)
            else:
                rs += struct.pack("!I", addr)
        return rs

    def deser_addrs(self, f):
        addrs = []
        for i in range(self.ip_address_count):
            if (self.version == self.IPv6_version):
                a, b = struct.unpack('>QQ', f.read(16))
                unpacked = (a << 64) | b
                addrs.append(unpacked)
            else:
                addrs.append(struct.unpack("!I", f.read(4))[0])
        return addrs

    def deserialize(self, f):
        self.version = struct.unpack("<B", f.read(1))[0]
        self.ip_address_count = deser_compact_size(f)
        self.ip_addresses = self.deser_addrs(f)
        self.inputs = deser_varint_vector(f)

    def serialize(self):
        r = b""
        r += struct.pack("<B", self.version)
        r += ser_compact_size(self.ip_address_count)
        r += self.ser_addrs(self.ip_addresses)
        r += ser_varint_vector(self.inputs)
        return r


class BlockTransactionsRequest():

    def __init__(self, blockhash=0, indexes=None):
        self.blockhash = blockhash
        self.indexes = indexes if indexes != None else []

    def deserialize(self, f):
        self.blockhash = deser_uint256(f)
        indexes_length = deser_compact_size(f)
        for i in range(indexes_length):
            self.indexes.append(deser_compact_size(f))

    def serialize(self):
        r = b"".join((
            ser_uint256(self.blockhash),
            ser_compact_size(len(self.indexes)),
            b"".join(ser_compact_size(x) for x in self.indexes)))
        return r

    # helper to set the differentially encoded indexes from absolute ones
    def from_absolute(self, absolute_indexes):
        self.indexes = []
        last_index = -1
        for x in absolute_indexes:
            self.indexes.append(x - last_index - 1)
            last_index = x

    def to_absolute(self):
        absolute_indexes = []
        last_index = -1
        for x in self.indexes:
            absolute_indexes.append(x + last_index + 1)
            last_index = absolute_indexes[-1]
        return absolute_indexes

    def __repr__(self):
        return "BlockTransactionsRequest(hash=%064x indexes=%s)" % (self.blockhash, repr(self.indexes))


class BlockTransactions():

    def __init__(self, blockhash=0, transactions=None):
        self.blockhash = blockhash
        self.transactions = transactions if transactions != None else []

    def deserialize(self, f):
        self.blockhash = deser_uint256(f)
        self.transactions = deser_vector(f, CTransaction)

    def serialize(self):
        r = b"".join((
            ser_uint256(self.blockhash),
            ser_vector(self.transactions),))
        return r

    def __repr__(self):
        return "BlockTransactions(hash=%064x transactions=%s)" % (self.blockhash, repr(self.transactions))


# Objects that correspond to messages on the wire
class msg_version():
    command = b"version"

    def __init__(self, versionNum=MY_VERSION):
        self.nVersion = versionNum
        self.nServices = 1
        self.nTime = int(time.time())
        self.addrTo = CAddressInVersion()
        self.addrFrom = CAddressInVersion()
        self.nNonce = random.getrandbits(64)
        self.strSubVer = MY_SUBVERSION
        self.nStartingHeight = -1
        self.nRelay = MY_RELAY
        self.assocID = create_association_id()

    def deserialize(self, f):
        self.nVersion = struct.unpack("<i", f.read(4))[0]
        if self.nVersion == 10300:
            self.nVersion = 300
        self.nServices = struct.unpack("<Q", f.read(8))[0]
        self.nTime = struct.unpack("<q", f.read(8))[0]
        self.addrTo = CAddressInVersion()
        self.addrTo.deserialize(f)

        if self.nVersion >= 106:
            self.addrFrom = CAddressInVersion()
            self.addrFrom.deserialize(f)
            self.nNonce = struct.unpack("<Q", f.read(8))[0]
            self.strSubVer = deser_string(f)
        else:
            self.addrFrom = None
            self.nNonce = None
            self.strSubVer = None
            self.nStartingHeight = None

        if self.nVersion >= 209:
            self.nStartingHeight = struct.unpack("<i", f.read(4))[0]
        else:
            self.nStartingHeight = None

        if self.nVersion >= 70001:
            # Relay field is optional for version 70001 onwards
            try:
                self.nRelay = struct.unpack("<b", f.read(1))[0]
                try:
                    uuidBytes = deser_string(f)
                    self.assocID = deserialise_uuid_associd(uuidBytes)
                except:
                    self.assocID = None
            except:
                self.nRelay = 0
        else:
            self.nRelay = 0
            self.assocID = None

    def serialize(self):
        r = b"".join((
            struct.pack("<i", self.nVersion),
            struct.pack("<Q", self.nServices),
            struct.pack("<q", self.nTime),
            self.addrTo.serialize(),
            self.addrFrom.serialize(),
            struct.pack("<Q", self.nNonce),
            ser_string(self.strSubVer),
            struct.pack("<i", self.nStartingHeight),
            struct.pack("<b", self.nRelay),
            serialise_uuid_associd(self.assocID),
        ))
        return r

    def __repr__(self):
        return 'msg_version(nVersion=%i nServices=%i nTime=%s addrTo=%s addrFrom=%s nNonce=0x%016X strSubVer=%s nStartingHeight=%i nRelay=%i assocID=%s)' \
            % (self.nVersion, self.nServices, time.ctime(self.nTime),
               repr(self.addrTo), repr(self.addrFrom), self.nNonce,
               self.strSubVer, self.nStartingHeight, self.nRelay, str(self.assocID))


class msg_verack():
    command = b"verack"

    def __init__(self):
        pass

    def deserialize(self, f):
        pass

    def serialize(self):
        return b""

    def __repr__(self):
        return "msg_verack()"


class msg_createstream():
    command = b"createstrm"

    def __init__(self, stream_type, stream_policy=b"", assocID=None):
        self.assocID = assocID
        self.stream_type = stream_type
        self.stream_policy = stream_policy

    def deserialize(self, f):
        uuidBytes = deser_string(f)
        self.assocID = deserialise_uuid_associd(uuidBytes)
        self.stream_type = struct.unpack("<B", f.read(1))[0]
        self.stream_policy = deser_string(f)

    def serialize(self):
        return b"".join((
            serialise_uuid_associd(self.assocID),
            struct.pack("<B", self.stream_type),
            ser_string(self.stream_policy),
        ))

    def __repr__(self):
        return "msg_createstream(assocID=%s stream_type=%i stream_policy=%s)" % (str(self.assocID),
                                                                                 self.stream_type,
                                                                                 str(self.stream_policy))


class msg_streamack():
    command = b"streamack"

    def __init__(self, assocID=None, stream_type=StreamType.UNKNOWN.value):
        self.assocID = assocID
        self.stream_type = stream_type

    def deserialize(self, f):
        uuidBytes = deser_string(f)
        self.assocID = deserialise_uuid_associd(uuidBytes)
        self.stream_type = struct.unpack("<B", f.read(1))[0]

    def serialize(self):
        return b"".join((
            serialise_uuid_associd(self.assocID),
            struct.pack("<B", self.stream_type),
        ))

    def __repr__(self):
        return "msg_streamack(assocID=%s stream_type=%i)" % (str(self.assocID), self.stream_type)


class msg_revokemid():
    command = b"revokemid"

    def __init__(self, revocationKey=None, revocationPubKey=None, minerIdKey=None, minerIdPubKey=None, idToRevoke=None):
        self.revocationPubKey = None
        self.minerIdKey = None
        self.idToRevoke = None
        self.sig1 = None
        self.sig2 = None

        if(revocationKey and revocationPubKey and minerIdKey and minerIdPubKey and idToRevoke):
            self.revocationPubKey = revocationPubKey
            self.minerIdPubKey = minerIdPubKey
            self.idToRevoke = idToRevoke

            # Produce revocation message signatures
            hashRevocationMessage = sha256(self.idToRevoke)
            self.sig1 = revocationKey.sign_digest(hashRevocationMessage, sigencode=ecdsa.util.sigencode_der)
            self.sig2 = minerIdKey.sign_digest(hashRevocationMessage, sigencode=ecdsa.util.sigencode_der)

    def deserialize(self, f):
        version = struct.unpack("<i", f.read(4))[0]
        if(version != 0):
            raise ValueError("Bad version {0} in revokemid msg".format(version))
        self.revocationPubKey = f.read(33)
        self.minerIdPubKey = f.read(33)
        self.idToRevoke = f.read(33)

        revocationMessage = deser_string(f)
        sig1Len = revocationMessage[0]
        self.sig1 = revocationMessage[1:sig1Len]
        sig2Len = revocationMessage[1+sig1Len]
        self.sig2 = revocationMessage[1+sig1Len+1:]

    def serialize(self):
        r = b""
        r += struct.pack("<i", 0)   # version
        r += self.revocationPubKey
        r += self.minerIdPubKey
        r += self.idToRevoke

        revocationMessageSig = b""
        revocationMessageSig += struct.pack("B", len(self.sig1))
        revocationMessageSig += self.sig1
        revocationMessageSig += struct.pack("B", len(self.sig2))
        revocationMessageSig += self.sig2
        r += ser_string(revocationMessageSig)

        return r


class msg_protoconf():
    command = b"protoconf"

    def __init__(self, protoconf=None):
        if protoconf is None:
            self.protoconf = CProtoconf(2,0,b"")
        else:
            self.protoconf = protoconf

    def deserialize(self, f):
        self.inv = self.protoconf.deserialize(f)

    def serialize(self):
        r = b""
        r += self.protoconf.serialize()
        return r

    def __repr__(self):
        return "msg_protoconf(protoconf=%s)" % (repr(self.protoconf))


class msg_authch():
    command = b"authch"

    def __init__(self):
        self.nVersion = 0
        self.nMsgLen = 0
        self.msg = 0

    def deserialize(self, f):
        self.nVersion = struct.unpack("<I", f.read(4))[0]
        self.nMsgLen = struct.unpack("<I", f.read(4))[0]
        self.msg = deser_uint256(f)

    def serialize(self):
        return b"".join((
            struct.pack("<I", self.nVersion),
            struct.pack("<I", self.nMsgLen),
            ser_uint256(self.msg),
        ))

    def __repr__(self):
        return "msg_authch(nVersion=%d nMsgLen=%d msg=%064x)" % (self.nVersion, self.nMsgLen, self.msg)


class msg_authresp():
    command = b"authresp"

    def __init__(self):
        self.nPubKeyLen = 0
        self.pubKey = None
        self.nClientNonce = 0
        self.nSignLen = 0
        self.sign = None

    def deserialize(self, f):
        self.nPubKeyLen = struct.unpack("<I", f.read(1))[0]
        self.pubKey = deser_byte_array(deser_string(f))
        self.nClientNonce = struct.unpack("<Q", f.read(8))[0]
        self.nSignLen = struct.unpack("<I", f.read(1))[0]
        self.sign = deser_byte_array(deser_string(f))

    def serialize(self):
        return b"".join((
            struct.pack("<I", self.nPubKeyLen),
            ser_uint8_vector(self.PubKey),
            struct.pack("<Q", self.nClientNonce),
            struct.pack("<I", self.nSignLen),
            ser_uint8_vector(self.sign),
        ))

    def __repr__(self):
        return "msg_authresp(nPubKeyLen=%d pubKey=%s nClientNonce=%08x nSignLen=%d sign=%s)" % (self.nPubKeyLen, str(self.pubKey), self.nClientNonce, self.nSignLen, str(self.sign))


class msg_addr():
    command = b"addr"

    def __init__(self):
        self.addrs = []

    def deserialize(self, f):
        self.addrs = deser_vector(f, CAddress)

    def serialize(self):
        return ser_vector(self.addrs)

    def __repr__(self):
        return "msg_addr(addrs=%s)" % (repr(self.addrs))


class msg_alert():
    command = b"alert"

    def __init__(self):
        self.alert = CAlert()

    def deserialize(self, f):
        self.alert = CAlert()
        self.alert.deserialize(f)

    def serialize(self):
        return self.alert.serialize()

    def __repr__(self):
        return "msg_alert(alert=%s)" % (repr(self.alert),)


class msg_inv():
    command = b"inv"

    def __init__(self, inv=None):
        if inv is None:
            self.inv = []
        else:
            self.inv = inv

    def deserialize(self, f):
        self.inv = deser_vector(f, CInv)

    def serialize(self):
        return ser_vector(self.inv)

    def __repr__(self):
        return "msg_inv(inv=%s)" % (repr(self.inv))


class msg_getdata():
    command = b"getdata"

    def __init__(self, inv=None):
        self.inv = inv if inv != None else []

    def deserialize(self, f):
        self.inv = deser_vector(f, CInv)

    def serialize(self):
        return ser_vector(self.inv)

    def __repr__(self):
        return "msg_getdata(inv=%s)" % (repr(self.inv))


class msg_getblocks():
    command = b"getblocks"

    def __init__(self):
        self.locator = CBlockLocator()
        self.hashstop = 0

    def deserialize(self, f):
        self.locator = CBlockLocator()
        self.locator.deserialize(f)
        self.hashstop = deser_uint256(f)

    def serialize(self):
        r = b"".join((
            self.locator.serialize(),
            ser_uint256(self.hashstop),))
        return r

    def __repr__(self):
        return "msg_getblocks(locator=%s hashstop=%064x)" \
            % (repr(self.locator), self.hashstop)


class msg_tx():
    command = b"tx"

    def __init__(self, tx=CTransaction()):
        self.tx = tx

    def deserialize(self, f):
        self.tx.deserialize(f)

    def serialize(self):
        return self.tx.serialize()

    def __repr__(self):
        return "msg_tx(tx=%s)" % (repr(self.tx))


class msg_block():
    command = b"block"

    def __init__(self, block=None):
        if block is None:
            self.block = CBlock()
        else:
            self.block = block

    def deserialize(self, f):
        self.block.deserialize(f)

    def serialize(self):
        return self.block.serialize()

    def __repr__(self):
        return "msg_block(block=%s)" % (repr(self.block))


# for cases where a user needs tighter control over what is sent over the wire
# note that the user must supply the name of the command, and the data
class msg_generic():
    def __init__(self, command, data=None):
        self.command = command
        self.data = data

    def serialize(self):
        return self.data

    def __repr__(self):
        return "msg_generic()"


class msg_getaddr():
    command = b"getaddr"

    def __init__(self):
        pass

    def deserialize(self, f):
        pass

    def serialize(self):
        return b""

    def __repr__(self):
        return "msg_getaddr()"


class msg_ping_prebip31():
    command = b"ping"

    def __init__(self):
        pass

    def deserialize(self, f):
        pass

    def serialize(self):
        return b""

    def __repr__(self):
        return "msg_ping() (pre-bip31)"


class msg_ping():
    command = b"ping"

    def __init__(self, nonce=0):
        self.nonce = nonce

    def deserialize(self, f):
        self.nonce = struct.unpack("<Q", f.read(8))[0]

    def serialize(self):
        return struct.pack("<Q", self.nonce)

    def __repr__(self):
        return "msg_ping(nonce=%08x)" % self.nonce


class msg_pong():
    command = b"pong"

    def __init__(self, nonce=0):
        self.nonce = nonce

    def deserialize(self, f):
        self.nonce = struct.unpack("<Q", f.read(8))[0]

    def serialize(self):
        return struct.pack("<Q", self.nonce)

    def __repr__(self):
        return "msg_pong(nonce=%08x)" % self.nonce


class msg_mempool():
    command = b"mempool"

    def __init__(self):
        pass

    def deserialize(self, f):
        pass

    def serialize(self):
        return b""

    def __repr__(self):
        return "msg_mempool()"


class msg_sendheaders():
    command = b"sendheaders"

    def __init__(self):
        pass

    def deserialize(self, f):
        pass

    def serialize(self):
        return b""

    def __repr__(self):
        return "msg_sendheaders()"


class msg_sendhdrsen():
    command = b"sendhdrsen"

    def __init__(self):
        pass

    def deserialize(self, f):
        pass

    def serialize(self):
        return b""

    def __repr__(self):
        return "msg_sendhdrsen()"


# getheaders message has
# number of entries
# vector of hashes
# hash_stop (hash of last desired block header, 0 to get as many as possible)
class msg_getheaders():
    command = b"getheaders"

    def __init__(self, locator_have=[]):
        self.locator = CBlockLocator(locator_have)
        self.hashstop = 0

    def deserialize(self, f):
        self.locator = CBlockLocator()
        self.locator.deserialize(f)
        self.hashstop = deser_uint256(f)

    def serialize(self):
        r = b"".join((
            self.locator.serialize(),
            ser_uint256(self.hashstop),))
        return r

    def __repr__(self):
        return "msg_getheaders(locator=%s, stop=%064x)" \
            % (repr(self.locator), self.hashstop)


# headers message has
# <count> <vector of block headers>
class msg_headers():
    command = b"headers"

    def __init__(self):
        self.headers = []

    def deserialize(self, f):
        # comment in bitcoind indicates these should be deserialized as blocks
        blocks = deser_vector(f, CBlock)
        for x in blocks:
            self.headers.append(CBlockHeader(x))

    def serialize(self):
        blocks = [CBlock(x) for x in self.headers]
        return ser_vector(blocks)

    def __repr__(self):
        return "msg_headers(headers=%s)" % repr(self.headers)


# gethdrsen message has
# number of entries
# vector of hashes
# hash_stop (hash of last desired block header, 0 to get as many as possible)
class msg_gethdrsen():
    command = b"gethdrsen"

    def __init__(self, locator_have=[], hashstop=0):
        self.locator = CBlockLocator(locator_have)
        self.hashstop = hashstop

    def deserialize(self, f):
        self.locator = CBlockLocator()
        self.locator.deserialize(f)
        self.hashstop = deser_uint256(f)

    def serialize(self):
        r = b"".join((
            self.locator.serialize(),
            ser_uint256(self.hashstop),))
        return r

    def __repr__(self):
        return "msg_gethdrsen(locator=%s, stop=%064x)" \
            % (repr(self.locator), self.hashstop)


# hdrsen message has
# <count> <vector of block headers>
class msg_hdrsen():
    command = b"hdrsen"

    def __init__(self):
        self.headers = []

    def deserialize(self, f):
        # comment in bitcoind indicates these should be deserialized as blocks
        self.headers = deser_vector(f, CBlockHeaderEnriched)

    def serialize(self):
        blocks = [CBlockHeaderEnriched(x) for x in self.headers]
        return ser_vector(blocks)

    def __repr__(self):
        return "msg_hdrsen(hdrsen=%s)" % repr(self.headers)


class msg_reject():
    command = b"reject"
    REJECT_MALFORMED = 1

    def __init__(self, message=b"", code=0, reason=b"", data=0):
        self.message = message
        self.code = code
        self.reason = reason
        self.data = data

    def deserialize(self, f):
        self.message = deser_string(f)
        self.code = struct.unpack("<B", f.read(1))[0]
        self.reason = deser_string(f)
        if (self.code != self.REJECT_MALFORMED and
                (self.message == b"block" or self.message == b"tx")):
            self.data = deser_uint256(f)

    def serialize(self):
        r = ser_string(self.message)
        r += struct.pack("<B", self.code)
        r += ser_string(self.reason)
        if (self.code != self.REJECT_MALFORMED and
                (self.message == b"block" or self.message == b"tx")):
            r += ser_uint256(self.data)
        return r

    def __repr__(self):
        return "msg_reject: %s %d %s [%064x]" \
            % (self.message, self.code, self.reason, self.data)


class msg_feefilter():
    command = b"feefilter"

    def __init__(self, feerate=0):
        self.feerate = feerate

    def deserialize(self, f):
        self.feerate = struct.unpack("<Q", f.read(8))[0]

    def serialize(self):
        return struct.pack("<Q", self.feerate)

    def __repr__(self):
        return "msg_feefilter(feerate=%08x)" % self.feerate


class msg_sendcmpct():
    command = b"sendcmpct"

    def __init__(self, announce=False):
        self.announce = announce
        self.version = 1

    def deserialize(self, f):
        self.announce = struct.unpack("<?", f.read(1))[0]
        self.version = struct.unpack("<Q", f.read(8))[0]

    def serialize(self):
        r = b"".join((
            struct.pack("<?", self.announce),
            struct.pack("<Q", self.version),))
        return r

    def __repr__(self):
        return "msg_sendcmpct(announce=%s, version=%lu)" % (self.announce, self.version)


class msg_cmpctblock():
    command = b"cmpctblock"

    def __init__(self, header_and_shortids=None):
        self.header_and_shortids = header_and_shortids

    def deserialize(self, f):
        self.header_and_shortids = P2PHeaderAndShortIDs()
        self.header_and_shortids.deserialize(f)

    def serialize(self):
        return self.header_and_shortids.serialize()

    def __repr__(self):
        return "msg_cmpctblock(HeaderAndShortIDs=%s)" % repr(self.header_and_shortids)


class msg_getblocktxn():
    command = b"getblocktxn"

    def __init__(self):
        self.block_txn_request = None

    def deserialize(self, f):
        self.block_txn_request = BlockTransactionsRequest()
        self.block_txn_request.deserialize(f)

    def serialize(self):
        return self.block_txn_request.serialize()

    def __repr__(self):
        return "msg_getblocktxn(block_txn_request=%s)" % (repr(self.block_txn_request))


class msg_blocktxn():
    command = b"blocktxn"

    def __init__(self):
        self.block_transactions = BlockTransactions()

    def deserialize(self, f):
        self.block_transactions.deserialize(f)

    def serialize(self):
        return self.block_transactions.serialize()

    def __repr__(self):
        return "msg_blocktxn(block_transactions=%s)" % (repr(self.block_transactions))


class msg_notfound():
    command = b"notfound"

    def __init__(self, inv=None):
        if inv is None:
            self.inv = []
        else:
            self.inv = inv

    def deserialize(self, f):
        self.inv = deser_vector(f, CInv)

    def serialize(self):
        return ser_vector(self.inv)

    def __repr__(self):
        return "msg_notfound(inv=%s)" % (repr(self.inv))


# Data for the merkle proof node part of a merkle proof
class MerkleProofNode():
    def __init__(self, node=0):
        self.nodeType = 0
        self.node = node

    def deserialize(self, f):
        self.nodeType = struct.unpack("<B", f.read(1))[0]
        # Currently only type 0 is supported (it means node is always uint256)
        assert(self.nodeType == 0)
        self.node = deser_uint256(f)

    def serialize(self):
        r = b"".join((
            struct.pack("<B", self.nodeType),
            ser_uint256(self.node),))
        return r

    def __repr__(self):
        return "MerkleProofNode(type=%i node=%064x)" % (self.nodeType, self.node)


# Base class for merkle proofs required in P2P messages
class MerkleProof():
    def __init__(self, txIndex=0, tx=CTransaction(), merkleRoot=0, proof=None):
        self.flags = 0
        self.txIndex = txIndex
        self.tx = tx
        self.merkleRoot = merkleRoot
        if proof is None:
            self.proof = []
        else:
            self.proof = proof

    def deserialize(self, f):
        self.flags = struct.unpack("<B", f.read(1))[0]
        self.txIndex = deser_compact_size(f)
        # Length of transaction bytes is deserialized as required by the specification, but we don't actually need it to deserialize the transaction
        deser_compact_size(f)
        self.tx = CTransaction()
        self.tx.deserialize(f)
        self.merkleRoot = deser_uint256(f)
        self.proof = deser_vector(f, MerkleProofNode)

    def serialize(self):
        txSerialized = self.tx.serialize()
        r = b"".join((
            struct.pack("<B", self.flags),
            ser_compact_size(self.txIndex),
            ser_compact_size(len(txSerialized)),
            txSerialized,
            ser_uint256(self.merkleRoot),
            ser_vector(self.proof),))
        return r

    def __repr__(self):
        return "DSMerkleProof(txIndex=%i tx=%s merkleRoot=%064x proof=%s)" % (self.txIndex, repr(self.tx), self.merkleRoot, repr(self.proof))


# Data for the merkle proof part of the double-spend detected P2P message
class DSMerkleProof(MerkleProof):
    def __init__(self, txIndex=0, tx=CTransaction(), merkleRoot=0, proof=None, json_notification=None):
        super().__init__(txIndex, tx, merkleRoot, proof)
        self.flags = 5

        if json_notification:
            self.txIndex = json_notification["index"]
            self.tx = FromHex(CTransaction(), json_notification["txOrId"])
            # Only merkleRoot target type is currently supported
            assert(json_notification["targetType"] == "merkleRoot")
            self.merkleRoot = uint256_from_str(hex_str_to_bytes(json_notification["target"])[::-1])
            self.proof = []
            for node in json_notification["nodes"]:
                self.proof.append(MerkleProofNode(uint256_from_str(hex_str_to_bytes(node)[::-1])))

    def deserialize(self, f):
        super().deserialize(f)
        # Flags should always be 5
        assert(self.flags == 5)

    def __repr__(self):
        return "DSMerkleProof(txIndex=%i tx=%s merkleRoot=%064x proof=%s)" % (self.txIndex, repr(self.tx), self.merkleRoot, repr(self.proof))


# Data for the block details part of the double-spend detected P2P message
class BlockDetails():
    def __init__(self, blockHeaders=None, merkleProof=DSMerkleProof(), json_notification=None):
        if json_notification is None:
            if blockHeaders is None:
                self.blockHeaders = []
            else:
                self.blockHeaders = blockHeaders
            self.merkleProof = merkleProof
        else:
            self.blockHeaders = []
            for blockHeader in json_notification["headers"]:
                self.blockHeaders.append(CBlockHeader(json_notification=blockHeader))
            self.merkleProof = DSMerkleProof(json_notification=json_notification["merkleProof"])

    def deserialize(self, f):
        self.blockHeaders = deser_vector(f, CBlockHeader)
        self.merkleProof = DSMerkleProof()
        self.merkleProof.deserialize(f)

    def serialize(self):
        r = b"".join((
            ser_vector(self.blockHeaders),
            self.merkleProof.serialize(),))
        return r

    def __repr__(self):
        return "BlockDetails(blockHeaders=%s merkleProof=%s)" % (repr(self.blockHeaders), repr(self.merkleProof))


# Double-spend detected P2P message
class msg_dsdetected():
    command = b"dsdetected"

    def __init__(self, version=1, blocksDetails=None, json_notification=None):
        if (json_notification is None):
            self.version = version
            if blocksDetails is None:
                self.blocksDetails = []
            else:
                self.blocksDetails = blocksDetails
        else:
            self.version = json_notification["version"]
            self.blocksDetails = []
            for json_blockDetails in json_notification["blocks"]:
                self.blocksDetails.append(BlockDetails(json_notification=json_blockDetails))

    def deserialize(self, f):
        self.version = struct.unpack("<H", f.read(2))[0]
        self.blocksDetails = deser_vector(f, BlockDetails)

    def serialize(self):
        r = b"".join((
            struct.pack("<H", self.version),
            ser_vector(self.blocksDetails),))
        return r

    def __repr__(self):
        return "msg_dsdetected(version=%i blocksDetails=%s)" % (self.version, repr(self.blocksDetails))


class msg_datareftx():
    command = b"datareftx"

    def __init__(self, tx=CTransaction(), proof=TSCMerkleProof()):
        self.tx = tx
        self.proof = proof

    def deserialize(self, f):
        self.tx.deserialize(f)
        self.proof = TSCMerkleProof()
        self.proof.deserialize(f)

    def serialize(self):
        r = b"".join((
            self.tx.serialize(),
            self.proof.serialize(),))
        return r

    def __repr__(self):
        return "msg_datareftx(tx=%s proof=%s)" % (repr(self.tx), repr(self.proof))


class NodeConnCB():
    """Callback and helper functions for P2P connection to a bitcoind node.

    Individual testcases should subclass this and override the on_* methods
    if they want to alter message handling behaviour.
    """

    def __init__(self):
        # Track whether we have a P2P connection open to the node
        self.connected = False
        self.connection = None

        # Track number of messages of each type received and the most recent
        # message of each type
        self.message_count = defaultdict(int)
        self.msg_timestamp = {}
        self.last_message = {}
        self.time_index = 0
        self.msg_index = defaultdict(int)

        # A count of the number of ping messages we've sent to the node
        self.ping_counter = 1

        # deliver_sleep_time is helpful for debugging race conditions in p2p
        # tests; it causes message delivery to sleep for the specified time
        # before acquiring the global lock and delivering the next message.
        self.deliver_sleep_time = None

        # Remember the services our peer has advertised
        self.peer_services = None

    # Message receiving methods

    def deliver(self, conn, message):
        """Receive message and dispatch message to appropriate callback.

        We keep a count of how many of each message type has been received
        and the most recent message of each type.

        Optionally waits for deliver_sleep_time before dispatching message.
        """

        deliver_sleep = self.get_deliver_sleep_time()
        if deliver_sleep is not None:
            time.sleep(deliver_sleep)
        with mininode_lock:
            try:
                command = message.command.decode('ascii')
                self.message_count[command] += 1
                self.last_message[command] = message
                self.msg_timestamp[command] = time.time()
                self.msg_index[command] = self.time_index
                self.time_index +=1
                getattr(self, 'on_' + command)(conn, message)
            except:
                print("ERROR delivering %s (%s)" % (repr(message),
                                                    sys.exc_info()[0]))
                raise

    def set_deliver_sleep_time(self, value):
        with mininode_lock:
            self.deliver_sleep_time = value

    def get_deliver_sleep_time(self):
        with mininode_lock:
            return self.deliver_sleep_time

    # Callback methods. Can be overridden by subclasses in individual test
    # cases to provide custom message handling behaviour.

    def on_open(self, conn):
        self.connected = True

    def on_close(self, conn):
        self.connected = False
        self.connection = None

    def on_addr(self, conn, message): pass

    def on_alert(self, conn, message): pass

    def on_block(self, conn, message): pass

    def on_blocktxn(self, conn, message): pass

    def on_cmpctblock(self, conn, message): pass

    def on_feefilter(self, conn, message): pass

    def on_getaddr(self, conn, message): pass

    def on_getblocks(self, conn, message): pass

    def on_getblocktxn(self, conn, message): pass

    def on_getdata(self, conn, message): pass

    def on_getheaders(self, conn, message): pass

    def on_headers(self, conn, message): pass

    def on_gethdrsen(self, conn, message): pass

    def on_hdrsen(self, conn, message): pass

    def on_mempool(self, conn): pass

    def on_pong(self, conn, message): pass

    def on_reject(self, conn, message): pass

    def on_sendcmpct(self, conn, message): pass

    def on_sendheaders(self, conn, message): pass

    def on_sendhdrsen(self, conn, message): pass

    def on_tx(self, conn, message): pass

    def on_datareftx(self, conn, message): pass

    def on_inv(self, conn, message):
        want = msg_getdata()
        for i in message.inv:
            if i.type != 0:
                want.inv.append(i)
        if len(want.inv):
            conn.send_message(want)

    def on_ping(self, conn, message):
        if conn.ver_send > BIP0031_VERSION:
            conn.send_message(msg_pong(message.nonce))

    def on_verack(self, conn, message):
        conn.ver_recv = conn.ver_send
        self.verack_received = True

    def on_streamack(self, conn, message): pass

    def on_revokemid(self, conn, message): pass

    def on_protoconf(self, conn, message): pass

    def on_version(self, conn, message):
        if message.nVersion >= 209:
            conn.send_message(msg_verack())
            self.send_protoconf(conn)
        conn.ver_send = min(MY_VERSION, message.nVersion)
        if message.nVersion < 209:
            conn.ver_recv = conn.ver_send
        conn.nServices = message.nServices

    def on_authch(self, conn, message): pass

    def on_authresp(self, conn, message): pass

    def on_notfound(self, conn, message): pass

    def send_protoconf(self, conn):
        conn.send_message(msg_protoconf(CProtoconf(2, MAX_PROTOCOL_RECV_PAYLOAD_LENGTH, b"BlockPriority,Default")))

    # Connection helper methods

    def add_connection(self, conn):
        self.connection = conn

    def wait_for_disconnect(self, timeout=60):
        def test_function(): return not self.connected
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    # Message receiving helper methods

    def clear_messages(self):
        with mininode_lock:
            self.message_count.clear()

    def wait_for_block(self, blockhash, timeout=60):
        def test_function(): return self.last_message.get(
            "block") and self.last_message["block"].block.rehash() == blockhash
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_getdata(self, timeout=60):
        def test_function(): return self.last_message.get("getdata")
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_getheaders(self, timeout=60):
        def test_function(): return self.last_message.get("getheaders")
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_headers(self, timeout=60):
        def test_function(): return self.last_message.get("headers")
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_hdrsen(self, timeout=60):
        def test_function(): return self.last_message.get("hdrsen")
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_inv(self, expected_inv, timeout=60, check_interval=0.05):
        """Waits for an INV message and checks that the first inv object in the message was as expected."""
        if len(expected_inv) > 1:
            raise NotImplementedError(
                "wait_for_inv() will only verify the first inv object")

        def test_function(): return self.last_message.get("inv") and \
            self.last_message["inv"].inv[0].type == expected_inv[0].type and \
            self.last_message["inv"].inv[0].hash == expected_inv[0].hash
        wait_until(test_function, timeout=timeout, lock=mininode_lock, check_interval=check_interval)

    def wait_for_verack(self, timeout=60):
        def test_function(): return self.message_count["verack"]
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_reject(self, timeout=60):
        def test_function(): return self.message_count["reject"]
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_protoconf(self, timeout=60):
        def test_function(): return self.message_count["protoconf"]
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    def wait_for_streamack(self, timeout=60):
        def test_function(): return self.message_count["streamack"]
        wait_until(test_function, timeout=timeout, lock=mininode_lock)

    # Message sending helper functions

    def send_message(self, message):
        if self.connection:
            self.connection.send_message(message)
        else:
            logger.error("Cannot send message. No connection to node!")

    def send_and_ping(self, message):
        self.send_message(message)
        self.sync_with_ping()

    # Sync up with the node
    def sync_with_ping(self, timeout=60):
        # use ping to guarantee that previously sent p2p messages were processed
        self.send_message(msg_ping(nonce=self.ping_counter))

        def test_function():
            if not self.last_message.get("pong"):
                return False
            if self.last_message["pong"].nonce != self.ping_counter:
                return False
            # after we receive pong we need to check that there are no async
            # block/transaction processes still running
            activity = self.connection.rpc.getblockchainactivity()
            return sum(activity.values()) == 0

        wait_until(test_function, timeout=timeout, lock=mininode_lock)
        self.ping_counter += 1

    @contextmanager
    def temporary_override_callback(self, **callbacks):
        old_callbacks = {cb_name: getattr(self, cb_name) for cb_name in callbacks.keys()}
        for cb_name, cb in callbacks.items():
            setattr(self, cb_name, cb)

        yield

        for cb_name, cb in old_callbacks.items():
            setattr(self, cb_name, cb)


class RateLimiter:
    """ A class that can rate limit processing. Currently used to limit speed of reading and writing to socket."""

    MEASURING_PERIOD = 1.0  # seconds
    MAX_FRACTION_TO_PROCESS_IN_ONE_GO = 0.1

    def __init__(self, rate_limit):
        self.rate_limit = rate_limit  # in amount per second
        self.history = []
        self.processed_in_last_measuring_period = 0
        self.max_chunk = ceil(self.rate_limit * RateLimiter.MAX_FRACTION_TO_PROCESS_IN_ONE_GO * RateLimiter.MEASURING_PERIOD)
        self.amount_per_measuring_period = ceil(self.rate_limit * self.MEASURING_PERIOD)

    def calculate_next_chunk(self, time_now):

        # ignore history older than one measuring period
        prune_to_ndx = None
        for ndx, (time_processed, amount_processed) in enumerate(self.history):
            if time_now - time_processed > self.MEASURING_PERIOD:
                prune_to_ndx = ndx
                self.processed_in_last_measuring_period -= amount_processed
            else:
                break
        if prune_to_ndx is not None:
            del self.history[:prune_to_ndx + 1]

        if self.processed_in_last_measuring_period == 0:
            assert len(self.history) == 0, "History not empty when processed data is 0"
        else:
            assert len(self.history) != 0, "History empty when processed data is not 0"
        assert self.processed_in_last_measuring_period <= self.amount_per_measuring_period, "Too much of processed data"

        # max amount of data that can still be processed in this time period
        can_be_processed = self.amount_per_measuring_period - self.processed_in_last_measuring_period

        return min(can_be_processed, self.max_chunk)

    def update_amount_processed(self, time_processed, amount_processed, sleep_expected_fraction=0):
        if amount_processed:
            self.history.append((time_processed, amount_processed))
            self.processed_in_last_measuring_period += amount_processed
        if sleep_expected_fraction:
            expected_time_to_send = amount_processed / self.rate_limit
            time.sleep(sleep_expected_fraction * expected_time_to_send)


# The actual NodeConn class
# This class provides an interface for a p2p connection to a specified node
class NodeConn(asyncore.dispatcher):
    messagemap = {
        b"version": msg_version,
        b"protoconf": msg_protoconf,
        b"verack": msg_verack,
        b"createstrm": msg_createstream,
        b"streamack": msg_streamack,
        b"revokemid": msg_revokemid,
        b"authch": msg_authch,
        b"authresp": msg_authresp,
        b"addr": msg_addr,
        b"alert": msg_alert,
        b"inv": msg_inv,
        b"getdata": msg_getdata,
        b"getblocks": msg_getblocks,
        b"tx": msg_tx,
        b"block": msg_block,
        b"getaddr": msg_getaddr,
        b"ping": msg_ping,
        b"pong": msg_pong,
        b"headers": msg_headers,
        b"getheaders": msg_getheaders,
        b"hdrsen": msg_hdrsen,
        b"gethdrsen": msg_gethdrsen,
        b"reject": msg_reject,
        b"mempool": msg_mempool,
        b"feefilter": msg_feefilter,
        b"sendheaders": msg_sendheaders,
        b"sendhdrsen": msg_sendhdrsen,
        b"sendcmpct": msg_sendcmpct,
        b"cmpctblock": msg_cmpctblock,
        b"getblocktxn": msg_getblocktxn,
        b"blocktxn": msg_blocktxn,
        b"notfound": msg_notfound,
        b"datareftx": msg_datareftx
    }

    MAGIC_BYTES = {
        "mainnet": b"\xe3\xe1\xf3\xe8",
        "testnet3": b"\xf4\xe5\xf3\xf4",
        "stn": b"\xfb\xce\xc4\xf9",
        "regtest": b"\xda\xb5\xbf\xfa",
    }

    # Extended message command
    EXTMSG_COMMAND = b'extmsg\x00\x00\x00\x00\x00\x00'
    assert(len(EXTMSG_COMMAND) == 12)

    def __init__(self, dstaddr, dstport, rpc, callback, net="regtest", services=NODE_NETWORK, send_version=True,
                 versionNum=MY_VERSION, strSubVer=None, assocID=None, nullAssocID=False):
        # Lock must be acquired when new object is added to prevent NetworkThread from trying
        # to access partially constructed object or trying to call callbacks before the connection
        # is established.

        self.send_data_rate_limiter = None
        self.receive_data_rate_limiter = None

        with network_thread_loop_intent_lock, network_thread_loop_lock:
            asyncore.dispatcher.__init__(self, map=mininode_socket_map)
            self.dstaddr = dstaddr
            self.dstport = dstport
            self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sendbuf = bytearray()
            self.recvbuf = bytearray()
            self.ver_send = 209
            self.ver_recv = 209
            self.protoVersion = versionNum
            self.last_sent = 0
            self.state = "connecting"
            self.network = net
            self.cb = callback
            self.disconnect = False
            self.nServices = 0
            self.maxInvElements = CInv.estimateMaxInvElements(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH)
            self.strSubVer = strSubVer
            self.assocID = assocID

            if(assocID):
                send_version = False

            if send_version:
                # stuff version msg into sendbuf
                vt = msg_version(self.protoVersion)
                vt.nServices = services
                vt.addrTo.ip = self.dstaddr
                vt.addrTo.port = self.dstport
                vt.addrFrom.ip = "0.0.0.0"
                vt.addrFrom.port = 0
                if(strSubVer):
                    vt.strSubVer = strSubVer
                if(nullAssocID):
                    vt.assocID = None
                self.send_message(vt, True)
                self.assocID = vt.assocID

            logger.info('Connecting to Bitcoin Node: %s:%d' %
                        (self.dstaddr, self.dstport))

            try:
                self.connect((dstaddr, dstport))
            except:
                self.handle_close()
            self.rpc = rpc

    def rate_limit_sending(self, limit):
        """The maximal rate of sending data in bytes/second, send rate will never exceed the limit while it will be somewhat slower.
         If None there will be no limit."""
        with mininode_lock:
            self.send_data_rate_limiter = RateLimiter(limit) if limit else None

    def rate_limit_receiving(self, limit):
        """The maximal rate of receiving in bytes/second, receive rate will never exceed the limit while it will be somewhat slower.
         If None there will be no limit."""
        with mininode_lock:
            self.receive_data_rate_limiter = RateLimiter(limit) if limit else None

    def handle_connect(self):
        if self.state != "connected":
            logger.debug("Connected & Listening: %s:%d" %
                         (self.dstaddr, self.dstport))
            self.state = "connected"
            self.cb.on_open(self)

    def handle_close(self):
        logger.debug("Closing connection to: %s:%d" %
                     (self.dstaddr, self.dstport))
        self.state = "closed"
        self.recvbuf = bytearray()
        self.sendbuf = bytearray()
        try:
            self.close()
        except:
            pass
        self.cb.on_close(self)

    def handle_read(self):
        with mininode_lock:
            if self.receive_data_rate_limiter is None:
                t = self.recv(READ_BUFFER_SIZE)
            else:
                time_now = time.time()
                read_chunk = self.receive_data_rate_limiter.calculate_next_chunk(time_now)
                max_read = min(READ_BUFFER_SIZE, read_chunk)
                t = self.recv(max_read)
                self.receive_data_rate_limiter.update_amount_processed(time_now, len(t), 0.1)
            if len(t) > 0:
                self.recvbuf += t

        while True:
            msg = self.got_data()
            if msg == None:
                break
            self.got_message(msg)

    def readable(self):
        return True

    def writable(self):
        with mininode_lock:
            pre_connection = self.state == "connecting"
            length = len(self.sendbuf)
        return (length > 0 or pre_connection)

    def handle_write(self):
        with mininode_lock:
            # asyncore does not expose socket connection, only the first read/write
            # event, thus we must check connection manually here to know when we
            # actually connect
            if self.state == "connecting":
                self.handle_connect()
            if not self.writable():
                return

            try:
                if self.send_data_rate_limiter is None:
                    sent = self.send(self.sendbuf)
                else:
                    time_now = time.time()
                    next_chunk = self.send_data_rate_limiter.calculate_next_chunk(time_now)
                    next_chunk = min(next_chunk, len(self.sendbuf))
                    sent = self.send(self.sendbuf[:next_chunk])
                    self.send_data_rate_limiter.update_amount_processed(time_now, sent, 0.1)
            except:
                self.handle_close()
                return
            del self.sendbuf[:sent]

    def got_data(self):
        try:
            with mininode_lock:
                if len(self.recvbuf) < 4:
                    return None
                if self.recvbuf[:4] != self.MAGIC_BYTES[self.network]:
                    raise ValueError("got garbage %s" % repr(self.recvbuf))
                if self.ver_recv < 209:
                    if len(self.recvbuf) < 4 + 12 + 4:
                        return None
                    command = self.recvbuf[4:4 + 12].split(b"\x00", 1)[0]
                    payloadlen = struct.unpack(
                        "<i", self.recvbuf[4 + 12:4 + 12 + 4])[0]
                    checksum = None
                    if len(self.recvbuf) < 4 + 12 + 4 + payloadlen:
                        return None
                    msg = self.recvbuf[4 + 12 + 4:4 + 12 + 4 + payloadlen]
                    self.recvbuf = self.recvbuf[4 + 12 + 4 + payloadlen:]
                else:
                    hdrsize = 4 + 12 + 4 + 4
                    if len(self.recvbuf) < hdrsize:
                        return None
                    extended = self.recvbuf[4:4 + 12] == self.EXTMSG_COMMAND

                    command = ''
                    payloadlen = 0
                    if extended:
                        hdrsize_ext = hdrsize + 12 + 8
                        if len(self.recvbuf) < hdrsize_ext:
                            return None
                        command = self.recvbuf[hdrsize:hdrsize + 12].split(b"\x00", 1)[0]
                        payloadlen = struct.unpack("<Q", self.recvbuf[hdrsize + 12:hdrsize + 12 + 8])[0]
                        hdrsize = hdrsize_ext
                    else:
                        command = self.recvbuf[4:4 + 12].split(b"\x00", 1)[0]
                        payloadlen = struct.unpack("<L", self.recvbuf[4 + 12:4 + 12 + 4])[0]

                    if len(self.recvbuf) < hdrsize + payloadlen:
                        return None

                    msg = self.recvbuf[hdrsize:hdrsize + payloadlen]
                    h = sha256(sha256(msg))
                    checksum = self.recvbuf[4 + 12 + 4:4 + 12 + 4 + 4]
                    if extended:
                        if checksum != b'\x00\x00\x00\x00':
                            raise ValueError("extended format msg checksum should be 0: {}".format(checksum))
                    elif checksum != h[:4]:
                        raise ValueError(
                            "got bad checksum " + repr(self.recvbuf))
                    self.recvbuf = self.recvbuf[hdrsize + payloadlen:]
                command = bytes(command)
                if command not in self.messagemap:
                    logger.warning("Received unknown command from %s:%d: '%s' %s" % (
                        self.dstaddr, self.dstport, command, repr(msg)))
                    raise ValueError("Unknown command: '%s'" % (command))
                f = BytesIO(msg)
                m = self.messagemap[command]()
                m.deserialize(f)
                return m

        except Exception as e:
            logger.exception('got_data:', repr(e))
            raise

    def send_message(self, message, pushbuf=False):
        if self.state != "connected" and not pushbuf:
            raise IOError('Not connected, no pushbuf')
        self._log_message("send", message)
        command = message.command
        data = message.serialize()
        tmsg = self.MAGIC_BYTES[self.network]
        tmsg += command
        tmsg += b"\x00" * (12 - len(command))
        tmsg += struct.pack("<I", len(data))
        if self.ver_send >= 209:
            th = sha256(data)
            h = sha256(th)
            tmsg += h[:4]
        tmsg += data
        with mininode_lock:
            self.sendbuf += tmsg
            self.last_sent = time.time()

    def got_message(self, message):
        if message.command == b"version":
            if message.nVersion <= BIP0031_VERSION:
                self.messagemap[b'ping'] = msg_ping_prebip31
        if self.last_sent + 30 * 60 < time.time():
            self.send_message(self.messagemap[b'ping']())
        self._log_message("receive", message)
        self.cb.deliver(self, message)

    def _log_message(self, direction, msg):
        if direction == "send":
            log_message = "Send message to "
        elif direction == "receive":
            log_message = "Received message from "
        log_message += "%s:%d: %s" % (self.dstaddr,
                                      self.dstport, repr(msg)[:500])
        if len(log_message) > 500:
            log_message += "... (msg truncated)"
        logger.debug(log_message)

    def disconnect_node(self):
        self.disconnect = True


NetworkThread_should_stop = False


def StopNetworkThread():
    global NetworkThread_should_stop
    NetworkThread_should_stop = True


class NetworkThread(Thread):

    poll_timeout = 0.1

    def run(self):
        while mininode_socket_map and not NetworkThread_should_stop:
            with network_thread_loop_intent_lock:
                # Acquire and immediately release lock.
                # This allows other threads to more easily acquire network_thread_loop_lock by
                # acquiring (and holding) network_thread_loop_intent_lock first since NetworkThread
                # will block on trying to acquire network_thread_loop_intent_lock in the line above.
                # If this was not done, other threads would need to wait for a long time (>10s) for
                # network_thread_loop_lock since it is released only briefly between two loop iterations.
                pass
            with network_thread_loop_lock:
                # We check for whether to disconnect outside of the asyncore
                # loop to workaround the behavior of asyncore when using
                # select
                disconnected = []
                for fd, obj in mininode_socket_map.items():
                    if obj.disconnect:
                        disconnected.append(obj)
                [obj.handle_close() for obj in disconnected]
                try:
                    asyncore.loop(NetworkThread.poll_timeout, use_poll=True, map=mininode_socket_map, count=1)
                except Exception as e:
                    # All exceptions are caught to prevent them from taking down the network thread.
                    # Since the error cannot be easily reported, it is just logged assuming that if
                    # the error is relevant, the test will detect it in some other way.
                    logger.warning("mininode NetworkThread: asyncore.loop() failed! " + str(e))
        logger.debug("Network thread closing")


# An exception we can raise if we detect a potential disconnect
# (p2p or rpc) before the test is complete
class EarlyDisconnectError(Exception):

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return repr(self.value)
