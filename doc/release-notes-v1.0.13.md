# Bitcoin SV Node software – Upgrade to v1.0.13 Release

This pages contains notes for specific features of the release. It is aimed at potential users, Customer Services and QA. Please refer to specific Jiras for more details.

*   1 [Overview](#overview)
*   2 [Miner ID](#miner-id)
    *   2.1 [Security Features](#security-features)
        *   2.1.1 [Miner ID Key Storage](#miner-id-key-storage)
    *   2.2 [New RPCs](#new-rpcs)
*   3 [revokemid P2P message format](#revokemid-p2p-message-format)
    *   3.1 [Config Options](#config-options)
*   4 [gethdrsen and hdrsen P2P messages](#gethdrsen-and-hdrsen-p2p-messages)
    *   4.1 [Usage guidance](#usage-guidance)
*   5 [Authenticated Connections](#authenticated-connections)
    *   5.1 [authch P2P message format](#authch-p2p-message-format)
    *   5.2 [authresp P2P message format](#authresp-p2p-message-format)
    *   5.3 [getdata message](#getdata-message)
        *   5.3.1 [Inventory vector new type](#inventory-vector-new-type)
        *   5.3.2 [datareftx P2P message](#datareftx-p2p-message)
            *   5.3.2.1 [The merkle\_proof](#the-merkle_proof)
    *   5.4 [Deprecated version P2P Message Fields](#deprecated-version-p2p-message-fields)
*   6 [Removed config setting](#removed-config-setting)
*   7 [Selfish Mining](#selfish-mining)
*   8 [Support for DARA](#support-for-dara)
*   9 [leveldb](#leveldb)
*   10 [Time locked transactions](#time-locked-transactions)
*   11 [Safe-mode](#safe-mode)
*   12 [Scaling Test Network reset](#scaling-test-network-reset)
*   13 [Other changes](#other-changes)

Overview
========

The 1.0.13 node release is a recommended upgrade from version 1.0.11.

Main features of this upgrade are

*   Miner ID specification v1.0 support.  
    
*   Authenticated Connections handshake.
*   Enhanced P2P hdrsen message.
*   DARA support

To take full advantage of Miner ID features, this release should be run in conjunction with Miner ID Generator 1.0.

The changes to NOMP are not available from the official NOMP website.

Miner ID
========

Miner ID is a cryptographic way for miners to declare that they created a particular block on the blockchain by adding information to the coinbase transaction in the block header.

The miner ID information can include data such as server endpoints and contact details, information which users need to be able to trust. It is important that a 3rd party who mines a block cannot pretend to be another miner.

The specification can be found at ['](https://confluence.stressedsharks.com/pages/viewpage.action?pageId=1936753337)[https://github.com/bitcoin-sv-specs/brfc-minerid](https://github.com/bitcoin-sv-specs/brfc-minerid)'. 1.0.13 supports Miner ID specification v1.0 as well as earlier versions.

Security Features
-----------------

It is possible that the miner ID private key can become compromised (e.g. a disgruntled employee passes it onto a 3rd party). In this case, the miner ID specification provides a mechanism for rolling miner ID keys (private and public), or the complete revocation of miner ID keys (private and public). I.e. operations _include_

*   Roll miner ID to a new value.
*   Forcibly roll miner ID to a new value using the revocation key (used for example if the miner ID key was already compromised and rolled away to a value not controlled by the legitimate owner of the miner ID)
*   Forcibly revoke all miner ID keys.

The user is directed to [https://github.com/bitcoin-sv/minerid-reference](https://github.com/bitcoin-sv/minerid-reference) for other operations and further details.

### Miner ID Key Storage

*   No private key information is stored in the node.
*   The Miner ID private key is read/written and stored locally by the Miner ID Generator. Miners who have security concerns should use file permissions to restrict access to local Miner ID Generator files.
*   The Miner ID revocation key should be stored off-line so that it is not available to developers, and only supplied and used when it is needed.

New RPCs
--------

The following RPCs have been introduced to the node as part of Miner ID work.

RPCs intended to be used by the Generator are:

1.  **revokeminerid** - revokes _minerId_ public key specified in the request and sends out the P2P _revokemid_ message to known peers.
2.  **getmineridinfo** - checks if the requested _minerId_ public key is present in the Miner ID DB and returns its state along with related revocation public keys.

RPCs used by the Mining Software to communicate with Nodes from the mining pool:

1.  **createminerinfotx** - creates a miner-info transaction containing a miner-info output script specified in the request.
2.  **replaceminerinfotx** - replaces in-mempool miner-info transaction by a new one containing a new miner-info output script specified in the request.
3.  **getminerinfotxid** - returns in-mempool miner-info txid.
4.  **createdatareftx** - creates a data-ref transaction containing a dataRefs output script(s) specified in the request.
5.  **getdatareftxid** - returns in-mempool data-ref txid.

RPCs used by a mining node's operator:

1.  **rebuildminerids** - forces a rebuild of the Miner ID DB and synchronises it to the blockchain.
2.  **dumpminerids** - returns details for all currently known miner IDs.
3.  **makeminerinfotxsigningkey** - creates a private BIP32 key used to sign miner-info and data-ref transactions.
4.  **getminerinfotxfundingaddress** - returns Base58 encoded BSV address generated from the private key and used to fund miner-info and data-ref transactions.
5.  **setminerinfotxfundingoutpoint** - sets miner-info funding outpoint to be used by miner-info and data-ref transactions.
6.  **datarefindexdump** - returns details for all currently stored miner-info and data-ref transactions from the dataRef index.
7.  **datareftxndelete** - deletes the specified miner-info or data-ref transaction from the dataRef index.

RPCs used by DARA functionality:

1.  **addToConfiscationTxidWhitelist** \- adds a list of txids, of transactions to be whitelisted, as confiscation transactions.
2.  **clearConfiscationWhitelist** \- removes all txid and txo entries from confiscation whitelist.
3.  **queryConfiscationTxidWhitelist** \- returns an array containing information about whitelisted confiscation transactions.

revokemid P2P message format
============================

A _revokemid_ P2P message is used to notify other nodes that a miner ID has been revoked. The message means that a miner does not have to wait until it wins another block before it can notify other nodes.

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| 4   | version | uint32\_t | Identifies MinerID protocol version |
| 33  | revocationKey | uint8\_t\[33\] | The current revocation key ECDSA (secp256k1) public key represented in compressed form as a 33 byte hex string. It is required to verify the first signature defined in the _revocationMessageSig_ field. |
| 33  | minerId | uint8\_t\[33\] | The miner's current _minerId_ ECDSA (secp256k1) public key represented in compressed form as a 33 byte hex string. It is required to verify the second signature defined in the _revocationMessageSig_ field. |
| 33  | revocationMessage | uint8\_t\[33\] | Revocation message to be signed to certify the legitimate miner who wants to revoke past reputation<br><br>The field contains the compromised minerId public key (in the case of complete revocation it is the minerId public key defined by the initial Miner ID document). |
| variable | revocationMessageSig | uint8\_t\[\] | The field defines two different signatures on the`Hash256(revocationMessage)`message digest.<br><br>`sig1 := Sign(Hash256(revocationMessage), priv_revocationKey)`, where_priv\_revocationKey_is the private key associated with the_revocationKey_public key  <br>`sig2 := Sign(Hash256(revocationMessage), priv_minerId)`, where_priv\_minerId_is the private key associated with the miner's current_minerId_public key<br><br>`sig1`and`sig2`are 70-73 byte hex strings<br><br>`sig1_length := sig1.length()`  <br>`sig2_length := sig2.length()`<br><br>`msgsig_part1 :=concat(sig1_length, sig1)`  <br>`msgsig_part2 :=concat(sig2_length, sig2)`<br><br>`revocationMessageSig := concat(msgsig_part1, msgsig_part2)`, where the data layout is:`sig1_length\|sig1\|sig2_length\|sig2`<br><br>The above concatenations are done on the hex encoded bytes. |

Config Options
--------------

Parameters to control the miner ID database are:

*   _\-minerid_ \- Enable or disable the miner ID database (default: true).  
    
*   _\-mineridcachesize_ \- Cache size to use for the miner ID database (default: 1MB).  
    
*   _\-mineridreputation\_m_ - Miners who identify themselves using miner ID can accumulate certain privileges over time by gaining a good reputation. A good reputation is gained by having mined M of the last N blocks on the current chain. This parameter sets the M value for that test. (default: 28).  
    
*   _\-mineridreputation\_n_ - This parameter sets the N value for the above test. (default: 2016)
*   _\-mineridreputation\_mscale_ - Miners who lose their good reputation can in some circumstances recover that reputation, but at the cost of a temporarily increased M of N block target. This parameter determines how much to scale the base M value in such cases. (default: 1.5).  
    

gethdrsen and hdrsen P2P messages
=================================

Remote peers can use P2P message_gethdrsen_to request block headers together with contents of coinbase transaction and its proof of inclusion plus any referenced miner-info transaction and its proof of inclusion. Headers with those transactions and proofs of inclusion are returned to the requesting peer in P2P message_hdrsen_. Peers can also request to receive block announcements in form of_hdrsen_P2P message. Primary users of this P2P message are SPV clients which may require simplified access to MinerID provided in coinbase transaction.

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| 1+  | count | [var\_int](https://wiki.bitcoinsv.io/index.php/Peer-To-Peer_Protocol#Variable_length_integer) | Number of enriched block headers<br><br>May be 0 if no header matches parameters specified in _gethdrsen_ message.<br><br>No more than 2000 enriched block headers are returned in a single message.<br><br>Since the contents of coinbase transactions can be large, maximum size of _hdrsen_ message is limited to maximum packet size that was agreed on in _protoconf_ message with `maxRecvPayloadLength` parameter (value is specified in peer configuration). The number of returned enriched block headers is reduced as needed to stay below this limit, but not below 1. This is so that one header requested by _gethdrsen_ message can be returned even if limit imposed by `maxRecvPayloadLength` parameter is exceeded.<br><br>This limit is always honored if message is sent to announce new blocks (i.e. new blocks will not be announced with this message if the size of the message would exceed the limit). |
| _Varies_ | enriched block headers | block\_header\_en\[\] | Enriched block headers (specified below) |

Enriched block header is serialized in the format described below.

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| 81+ | _block header fields_ (_please see block header definition_) | various | Same fields as in block header returned by _headers_ message. See: [https://wiki.bitcoinsv.io/index.php/Peer-To-Peer\_Protocol#headers](https://wiki.bitcoinsv.io/index.php/Peer-To-Peer_Protocol#headers)<br><br>Note: Value of field `txn_count` (transaction count) in block header is typically set to 0 if header is not sent as part of block message (e.g. in _headers_ message). Here the value of this field is set to actual transaction count if that information is available (i.e. if the block was already validated). |
| 1   | no\_more\_headers | bool | Boolean value indicating if there are more block headers available after the current header.<br><br>This value only equals true (1) for header of the block that is currently a tip of the active chain as seen by the node that is sending the message. |
| 1   | has\_coinbase\_data | bool | Boolean value indicating if current block header has additional coinbase data following this field. If this value equals false (0), this is the last field in the message.<br><br>This value may be equal to false if the message is sent as response to message_gethdrsen_and the node does not have the required data (e.g. if requested block is not yet fully validated, or if it was already pruned).<br><br>This value is always true if message is sent to announce new blocks. |
| variable | coinbase\_tx | [tx](https://wiki.bitcoinsv.io/index.php/Peer-To-Peer_Protocol#tx) | Serialized coinbase transaction. |
| variable | coinbase\_merkle\_proof | [tsc\_merkle\_proof](https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/) | Merkle proof in binary format according to standard TS 2020.010-31. See:[https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/](https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/)<br><br>Value of`flags`field is zero.<br><br>Fields`txOrId`and`target`contain an ID of a coinbase transaction and a block hash, respectively.<br><br>Value of`index`field is zero since the proof is for coinbase transaction which is always the first transaction in a block.<br><br>Value of`type`field is zero in every`node`element in`nodes`field. |
| 1   | has\_miner\_info\_data | bool | Boolean value indicating if this block contains a miner-info transaction referenced by the coinbase transaction. |
| variable | miner\_info\_tx | [tx](https://wiki.bitcoinsv.io/index.php/Peer-To-Peer_Protocol#tx) | Serialized miner ID miner-info transaction. |
| variable | miner\_info\_merkle\_proof | [tsc\_merkle\_proof](https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/) | Merkle proof in binary format according to standard TS 2020.010-31. See:[https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/](https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/)<br><br>Value of`flags`field is zero.<br><br>Fields`txOrId`and`target`contain an ID of a miner-info transaction and a block hash, respectively.<br><br>Value of`index`field is the index of the miner-info transaction within the block.<br><br>Value of`type`field is zero in every`node`element in`nodes`field. |

Usage guidance
--------------

Existing SPV clients that wish to receive enriched headers can follow these guidelines when implementing the changes:

*   Send _gethdrsen_ where previously you would have sent _getheaders_.
*   Send a _sendhdrsen_ where previously you would have sent _sendheaders_ and be prepared to receive any of the following to announce a block:
    *   _hdrsen_
    *   _inv_
*   If receiving an _inv_, fetch the enriched headers via _gethdrsen_ from peer that sent the _inv_ until you have the additional coinbase/proof information.

Authenticated Connections
=========================

An authentication challenge/response test is now performed between mining nodes when a connection is first established. If the nodes pass the challenge/response tests, the standard connection is elevated to an authenticated conenction. Two new P2P messages have been defined for this purpose.

authch P2P message format
-------------------------

The _authch_ message starts the authentication handshake

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| 4   | version | int32\_t | The message version, should be 0x01 |
| 4   | message\_length | uint32\_t | The length of the payload |
| _Varies_ | message | uint8\_t\[\] | The payload (random text) |

authresp P2P message format
---------------------------

A recipient of the _authch_ message should respond with an _authresp_ message. See specification for further details.

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| 4   | public\_key\_length | uint32\_t | The length of the Miner ID public key |
| _Varies_ | public\_key | uint8\_t\[\] | The latest ECDSA Miner ID public key |
| 8   | client\_nonce | uint64\_t | Nonce (random text) |
| 4   | signature\_length | uint32\_t | The length of the signature |
| _Varies_ | signature | uint8\_t\[\] | Signature of message using latest Miner ID private key |

getdata message
---------------

SPV clients will become aware that they need dataref transactions to fully reconstruct a coinbase document when they first see a new block header and the associated coinbase transaction containing the coinbase document. The node that sent the SPV client the block header must have also validated that block and so will have seen any dataref transactions contained in that block (plus any dataref transactions also contained in earlier blocks), and so will have those dataref transactions stored in its dataref index. The SPV client can therefore direct its requests for dataref transactions to the same node that sent it the block header.

### Inventory vector new type

In order for SPV clients to request dataref transactions we specify here an extension to the getdata P2P message. We will introduce a new inventory type for getdata called **MSG\_DATAREF\_TX** with a value of **0x05** to indicate to the receiving node that the sender is requesting a dataref transaction. The items in the inventory vector within the getdata message are then in the standard format. I.e:

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| 4   | type | uint32\_t | The type of the object being requested. In this case the value **0x05**. |
| 32  | hash | char\[32\] | The hash of the dataref transaction requested. |

If the requested dataref transaction(s) could not be found then a standard P2P **notfound** reply should be returned. Otherwise, a series of **datareftx** P2P messages (format shown below) should be sent back to the requester, one for each of the dataref transactions specified in the getdata request.

### datareftx P2P message

Dataref transactions are returned in a new P2P **datareftx** message, the format of which is as follows:

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| variable | txn | char\[\] | The serialised dataref transaction in the standard transaction format as for the P2P **tx** message described [here](https://en.bitcoin.it/wiki/Protocol_documentation#tx). |
| variable | merkle proof | merkle\_proof | A proof that the above dataref transaction is included in a block (see below for format). |

#### The merkle\_proof

The merkle proof contents format should follow the TSC standard described [here](https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/):

| Field Size | Name | Data Type | Description |
| --- | --- | --- | --- |
| 1   | flags | char | Set to **0x00** to indicate this proof contains just the transaction ID, the target type is a block hash, and the proof type is a merkle branch. |
| 1+  | transaction index | variant | Transaction index for the transaction this proof is for. |
| 32  | txid | char\[32\] | Txid for the dataref transaction this proof is for. |
| 32  | target | char\[32\] | Hash of the block containing the dataref transaction. |
| 1+  | node count | varint | The number of following hashes from the merkle branch. |
| variable | node list | node\[\] | An array of node objects comprising the merkle branch for this proof. The format of the node object is as described in the standard (link above). |

Deprecated version P2P Message Fields
-------------------------------------

The _addr\_from_ field in the version P2P message have been deprecated. This value is now directly extracted at the socket level.

Removed config setting
======================

The config setting -_allowunsolicitedaddr_ has been removed.

Selfish Mining
==============

The Journal Block Assembler (JBA) has been modified so that once the block template exceeds a certain size, it begins to throttle the addition of further transactions and adds transactions that will be recent enough so that the block template will not be consider to be selfish.

New parameters have been added:

*   \-_detectselfishmining_ \- enables or disables selfish mining detection (default: true)
*   \-_minblockmempooltimedifferenceselfish_ \- the lowest time difference in sec between the last block and last mempool transaction for the block to be classified as selfishly mined (default: 60)
*   \-_selfishtxpercentthreshold_ \- percentage threshold of number of txs in mempool that are not included in received block for the block to be classified as selfishly mined (default: 10).
*   \-_jbathrottlethreshold_ - After a new block has been found it can take a short while for the journaling block assembler to catch up and return a new candidate containing every transaction in the mempool. If this flag is 1, calling getminingcandidate will wait until the JBA has caught up and always return a candidate with every available transaction. If it is 0, calls to getminingcandidate will always return straight away but may occasionally only contain a subset of the available transactions from the mempool.

  

Support for DARA
================

This release supports code for the reassignment of digital assets. 

A confiscation transaction is identified by the first transaction output being an OP\_RETURN output which adopts the confiscation transaction protocol (See [https://github.com/bitcoin-sv-specs/protocol/tree/master/updates](https://github.com/bitcoin-sv-specs/protocol/tree/master/updates)) and contains a reference to identify the related ConfiscationOrder document. The complete Court order document is not embedded in the OP\_RETURN data of the confiscation transaction because not all blockchains support a large payload. For example, the default data carrier size on BTC is 80 bytes.

Please refer to [https://bitcoinsv.io](https://bitcoinsv.io) for more details

leveldb
=======

The version of leveldb has been updated to version 1.23  

Time locked transactions
========================

A transaction accepted into the time-locked mempool can be replaced "for free". An attacker can send a large amount of such transactions, one replacing another, forcing us to validate and propagate each of these transactions. This could consume all of our CPU power and/or bandwidth leading to DoS.

2 new config settings have been added:

\-_mempoolnonfinalmaxreplacementrate_ \- the maximum rate at which a transaction in the non-final mempool can be replaced by another updated transaction, expressed in transactions per hour. Default = `7200`.  
\-_mempoolnonfinalmaxreplacementrateperiod_ - the period of time (in minutes) over which the maximum rate for non-final transactions is measured (see -mempoolnonfinalmaxreplacementrate above). Default = `60`.

Safe-mode
=========

The safe mode configuration settings _safemodeminforklength_ has been changed from 3 to 6. This should reduce spurious safe mode activations.

Scaling Test Network reset
==========================

The Scaling Test Network (STN) has been reset at block height 7. This block has hash **`000000001f15fe3dac966c6bb873c63348ca3d877cd606759d26bd9ad41e5545`**.

Other changes
=============

Other changes include various enhancements and fixes to functional tests, and improvements to logging including

*   logging the reason a consolidation transaction is rejected.
*   logging messages whenever the _mapAskFor_ max size limit is exceeded.
