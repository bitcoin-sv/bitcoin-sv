#!/usr/bin/env python3
# Copyright (c) 2022 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

'''
Bip32 Keys for MinerId and MinerIdInfo creation
Usage:
    minerIdKeys = MinerIdKeys("0001")
    revocationKeys = MinerIdKeys("0002")
    minerInfo = create_miner_info(minerIdKeys, revocationKeys)
    minerInfoTx = create_miner_info_tx(minerInfo)
    signedMinerInfoTx = sign_minerInfoTx(minerInfoTx)
    block.append(signedMinerInfoTx)
'''
import json
import ecdsa
from pathlib import Path
from bip32utils import BIP32Key
from io import BytesIO
from .mininode import sha256, hex_str_to_bytes, bytes_to_hex_str, ser_uint256, COutPoint, ToHex, CTransaction
from .script import SignatureHashForkId, CScript, SIGHASH_ALL, SIGHASH_FORKID, OP_0, OP_FALSE, OP_TRUE, OP_RETURN, CTxOut
from .util import hashToHex, satoshi_round, assert_equal
from .blocktools import create_coinbase, create_block
import copy


class MinerIdKeys:
    ''' Bip32 Keys for MinerId '''
    def __init__(self, hexseed):
        hexseed = '0' * (32 - len(hexseed)) + hexseed
        while len(hexseed) < 32:
            hexseed
        self._curve = ecdsa.SECP256k1
        self._bip32_minerId_root = BIP32Key.fromEntropy(bytes.fromhex(hexseed))
        self._privateKey = self._bip32_minerId_root.ExtendedKey()
        self._privateKeyBinary = self._bip32_minerId_root.PrivateKey()
        self._publicKey = self._bip32_minerId_root.PublicKey()
        self._signingKey = ecdsa.SigningKey.from_string(self._privateKeyBinary, curve=self._curve)
        self._verifyingKey = self._signingKey.get_verifying_key().to_string("compressed")

    def privateKey(self): return self._privateKey
    def signingKey(self): return self._signingKey
    def verifyingKey(self): return self._verifyingKey
    def verifyingKeyHex(self): return bytes_to_hex_str(self._verifyingKey)
    def publicKeyBytes(self): return self._publicKey
    def publicKeyHex(self): return bytes_to_hex_str(self._publicKey)
    def verifyingKeyHex(self): return bytes_to_hex_str(self._verifyingKey)
    def signingKeyHex(self): return bytes_to_hex_str(self._signingKey.to_string())

    def store_keys(self, tmpdir, nodenum):
        datapath = tmpdir + "/node{}/regtest".format(nodenum)
        Path(datapath).mkdir(parents=True, exist_ok=True)
        with open(datapath + '/minerinfofundingkeys.dat', 'a') as f:
            f.write(self._privateKey)
            f.write('\n')
            f.write(self.publicKeyHex())

    def sign_hexmessage(self, message):
        message = hex_str_to_bytes(message)
        return self.sign_strmessage(message)

    def sign_strmessage(self, message):
        signedMessage = self.sign_strmessage_bytes(message)
        return bytes_to_hex_str(signedMessage)

    def sign_hexmessage_bytes(self, message):
        message = hex_str_to_bytes(message)
        return self.sign_strmessage_bytes(message)

    def sign_strmessage_bytes(self, message):
        hashToSign = sha256(message)
        while True:
            signedMessage = self._signingKey.sign_digest(hashToSign, sigencode=ecdsa.util.sigencode_der)
            # in miner_info.cpp there is similar check, we satisfy it here
            if len(signedMessage) >= 69 and len(signedMessage) <= 72:
                break
        return signedMessage

    def sign_tx_BIP143_with_forkid (self, tx_to_sign, txns_to_spend):
        # txns_to_send is a dictionary of txid, tx pairs
        for i, vin in enumerate(tx_to_sign.vin):
            n = vin.prevout.n
            t = txns_to_spend[hashToHex(vin.prevout.hash)]
            p = t.vout[n].scriptPubKey
            a = t.vout[n].nValue
            sighash = SignatureHashForkId(p, tx_to_sign, i, SIGHASH_ALL | SIGHASH_FORKID, a)
            signature = self.signingKey().sign_digest_deterministic(sighash, sigencode=ecdsa.util.sigencode_der)
            vin.scriptSig = CScript([signature + bytes(bytearray([SIGHASH_ALL | SIGHASH_FORKID])), self.publicKeyBytes()])


def create_miner_info_scriptPubKey(params, json_override_string=None):
    minerKeys = params.get('minerKeys')
    prev_minerKeys = params.get('prev_minerKeys')
    revocationKeys = params.get('revocationKeys')
    prev_revocationKeys = params.get('prev_revocationKeys')
    pubCompromisedMinerKeyHex = params.get('pubCompromisedMinerKeyHex')

    # if there are no previous keys available, then we replace them with the current keys.
    if not prev_minerKeys:
        prev_minerKeys = minerKeys
    if not prev_revocationKeys:
        prev_revocationKeys = revocationKeys

    infoDoc = {}
    infoDoc['version'] = "0.3"
    infoDoc['height'] = params['height'] # the height of the block we are building

    dataToSign = prev_minerKeys.publicKeyHex() + minerKeys.publicKeyHex()
    infoDoc['prevMinerId'] = prev_minerKeys.publicKeyHex()
    infoDoc['prevMinerIdSig'] = prev_minerKeys.sign_hexmessage(dataToSign)
    infoDoc['minerId'] = minerKeys.publicKeyHex()

    dataToSign = prev_revocationKeys.publicKeyHex() + revocationKeys.publicKeyHex()
    infoDoc['prevRevocationKey'] = prev_revocationKeys.publicKeyHex()
    infoDoc['prevRevocationKeySig'] = revocationKeys.sign_hexmessage(dataToSign)
    infoDoc['revocationKey'] = prev_revocationKeys.publicKeyHex()

    if pubCompromisedMinerKeyHex:
        messageSignature1 = revocationKeys.sign_hexmessage(pubCompromisedMinerKeyHex)
        messageSignature2 = prev_minerKeys.sign_hexmessage(pubCompromisedMinerKeyHex)
        infoDoc['revocationMessage'] = {"compromised_minerId": pubCompromisedMinerKeyHex}
        infoDoc['revocationMessageSig'] = {"sig1": messageSignature1, "sig2": messageSignature2}

    extensions = {}
    if all(p in params for p in ['publicIP', 'publicPort']):
        extensions['PublicIP'] = params['publicIP']
        extensions['PublicPort'] = params['publicPort']
    datarefs = params.get('datarefs')
    if datarefs:
        refs = {'refs': datarefs}
        extensions['dataRefs'] = refs
    if extensions:
        infoDoc['extensions'] = extensions

    # Convert dictionary to json string
    if json_override_string != None:
        infoDocJson = json_override_string
    else:
        infoDocJson = json.dumps(infoDoc, indent=0)
    infoDocJson = infoDocJson.replace('\n', '')
    infoDocJson = infoDocJson.replace(' ','')
    infoDocJson = infoDocJson.encode('utf8')
    infoDocJsonSig = minerKeys.sign_strmessage_bytes(infoDocJson)

    return CScript([OP_0, OP_RETURN, bytearray([0x60, 0x1d, 0xfa, 0xce]),bytearray([0x00]), infoDocJson, infoDocJsonSig])


# Create a miner-info transaction containing the miner ID document
def create_miner_info_txn(connection, params, utxo):
    # Create basic raw transaction from UTXO
    inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
    outputs = {}
    addr = connection.rpc.getnewaddress()
    outputs[addr] = satoshi_round(utxo['amount'])
    rawtx = connection.rpc.createrawtransaction(inputs, outputs)

    # Create transaction we can modify
    minerInfoTx = CTransaction()
    minerInfoTx.deserialize(BytesIO(hex_str_to_bytes(rawtx)))

    # Create miner-info document and add to output 0 of transaction
    minerInfoTx.vout.append(CTxOut(0, create_miner_info_scriptPubKey(params)))
    minerInfoTx.vout[0], minerInfoTx.vout[1] = minerInfoTx.vout[1], minerInfoTx.vout[0]

    # Sign transaction
    signed = connection.rpc.signrawtransaction(ToHex(minerInfoTx))

    # Return CTransaction
    minerInfoTx.deserialize(BytesIO(hex_str_to_bytes(signed['hex'])))
    minerInfoTx.rehash()
    return minerInfoTx


# Create dataref transaction
def create_dataref_txn(connection, dataref_json, utxo):
    # Create basic raw transaction from UTXO
    inputs = [{"txid": utxo["txid"], "vout": utxo["vout"]}]
    outputs = {}
    addr = connection.rpc.getnewaddress()
    outputs[addr] = satoshi_round(utxo['amount'])
    rawtx = connection.rpc.createrawtransaction(inputs, outputs)

    # Create transaction we can modify
    datarefTx = CTransaction()
    datarefTx.deserialize(BytesIO(hex_str_to_bytes(rawtx)))

    # Add dataref json to output 0 of transaction
    docjson = json.dumps(dataref_json, indent=0)
    docjson = docjson.replace('\n', '')
    docjson = docjson.replace(' ','')
    docjson = docjson.encode('utf8')
    datarefTx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN, bytearray([0x60, 0x1d, 0xfa, 0xce]), docjson])))
    datarefTx.vout[0], datarefTx.vout[1] = datarefTx.vout[1], datarefTx.vout[0]

    # Sign transaction
    signed = connection.rpc.signrawtransaction(ToHex(datarefTx))

    # Return CTransaction
    datarefTx.deserialize(BytesIO(hex_str_to_bytes(signed['hex'])))
    datarefTx.calc_sha256()
    return datarefTx


def create_dataref (brfcIds, txid, vout, compress=None):

    dataref = {
        'brfcIds': brfcIds,
        'txid': txid,
        'vout': vout
    }
    if compress:
        dataref['compress'] = compress

    return dataref


# Calculate modified merkle root for blockbind
def calc_blockbind_merkle_root(block):
    # Copy coinbase so we can modify it
    coinbase = copy.deepcopy(block.vtx[0])
    coinbase.nVersion = 0x00000001
    coinbase.vin[0].scriptSig = CScript([OP_0, OP_0, OP_0, OP_0, OP_0, OP_0, OP_0, OP_0])
    coinbase.vin[0].prevout = COutPoint(0, 0xFFFFFFFF)
    coinbase.rehash()

    # Build list of transaction hashs
    hashes = []
    for tx in block.vtx:
        tx.calc_sha256()
        hashes.append(ser_uint256(tx.sha256))
    hashes[0] = ser_uint256(coinbase.sha256)

    return block.get_merkle_root(hashes)


# Make a V0.3 compliant miner ID coinbase transaction and miner-info transaction
def create_miner_id_coinbase_and_miner_info(connection, params, block, utxo, minerInfoTx, makeValid):
    # Create miner-info txn if one not provided
    if not minerInfoTx:
        minerInfoTx = create_miner_info_txn(connection, params, utxo)
    else:
        minerInfoTx.rehash()

    # Add miner-info txn to block
    block.vtx.append(minerInfoTx)

    # Get miner-info txn ID
    txid = minerInfoTx.hash
    txidbytes = hex_str_to_bytes(txid)[::-1]

    # Create partially populated coinbase
    coinbaseTx = block.vtx[0]
    assert_equal(len(coinbaseTx.vout), 1)
    coinbaseTx.vout.append(CTxOut(0, CScript([OP_FALSE, OP_RETURN, bytearray([0x60, 0x1d, 0xfa, 0xce]), bytearray([0x00]), txidbytes])))

    # If we want an invalid block, screw up the fees
    if not makeValid:
        coinbaseTx.vout[1].nValue = 50
    coinbaseTx.rehash()

    # Calculate blockbind modified merkle root
    modifiedMerkleRoot = calc_blockbind_merkle_root(block)
    modifiedMerkleRootBytes = ser_uint256(modifiedMerkleRoot)

    # Get parent block hash
    parentHash = block.hashPrevBlock
    parentHashBytes = ser_uint256(parentHash)

    # Sign concat(modifiedMerkleRoot, prevBlockHash)
    concatMerklePrevBlockBytes = modifiedMerkleRootBytes + parentHashBytes
    key = params['minerKeys']
    if callable(key):  # hack for testing with MinerID Generator
        signature = key(concatMerklePrevBlockBytes)
        signature = hex_str_to_bytes(signature)
    else:
        signature = key.sign_strmessage_bytes(concatMerklePrevBlockBytes)

    # Append blockbind and signature
    coinbaseTx.vout[1].scriptPubKey += sha256(concatMerklePrevBlockBytes)
    coinbaseTx.vout[1].scriptPubKey += signature
    coinbaseTx.rehash()

    # Update block
    block.hashMerkleRoot = block.calc_merkle_root()
    block.rehash()


# Make a V0.3 compliant miner ID block
def make_miner_id_block(connection, params, utxo=None, datarefTxns=None, minerInfoTx=None, parentBlock=None, makeValid=True, lastBlockTime=0, txns=None):
    if parentBlock is not None:
        parentHash = parentBlock.sha256
        parentTime = parentBlock.nTime
        parentHeight = parentBlock.height
    else:
        tip = connection.rpc.getblock(connection.rpc.getbestblockhash())
        parentHash = int(tip["hash"], 16)
        parentTime = tip["time"]
        parentHeight = tip["height"]

    # Create block with temporary coinbase and any passed in txns
    tmpCoinbase = create_coinbase(params['height'])
    block = create_block(parentHash, tmpCoinbase, max(lastBlockTime, parentTime) + 1)
    if txns is not None:
        block.vtx.extend(txns)

    # Make real coinbase and miner-info txn and put them in the block
    if datarefTxns:
        for datarefTxn in datarefTxns:
            block.vtx.append(datarefTxn)
    create_miner_id_coinbase_and_miner_info(connection, params, block, utxo, minerInfoTx, makeValid)

    # Update block with height and solve
    block.height = parentHeight + 1
    block.solve()
    return block
