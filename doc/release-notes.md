# Bitcoin SV Node software – Upgrade to v1.0.10 Release

Overview
--------

Version 1.0.10 release is a recommended upgrade from version 1.0.9; This new version brings a number of improvements to both functionality and performance.

Parallel Transaction Validation (PTV) Scheduler Improvements
------------------------------------------------------------

Performance improvements have been made to the processing of long complex transaction graphs where transactions arrive out of order.

Modification to processing of user agent strings
------------------------------------------------

Currently it is possible that a node may connect to BCH nodes (There is advice on how to avoid this on bitcoin-sv github). Connecting to a non-BSV node is not fatal to the operation of the node but leads to wasted bandwidth and unnecessary block processing. Invalid blocks generate error messages which pollute the log files and make it difficult to see what is going on. The following steps have been taken to make it more likely that a node only connects to other BSV nodes.

*   **banclientua** \- contains the list of banned user agent substrings (the node will ban any peer that returns one of these user agent strings).  The default list is set to: "abc", "cash", "bch"
*   **allowclientua** \- a newly introduced config option that contains a list of allowed user agent substrings that overrides **banclientua**.  The default list is empty.

Matching is case insensitive. Note that the default list is cleared if a single **banclientua** parameter is set in the bitcoin config file or command line.  
  
Example config setting: 

**banclientua**\=XYZ  
**banclientua**\=ABC  
**allowclientua**\=its-not-abc

This will allow/disallow the following user agent strings:

'ThisAbcClient'   # banned, matches _ABC_  
'ThisBchClient'   # allowed because the default has been cleared, so connections to nodes that return "cash" or "bch" are allowed
'I-cant-believe-its-not-ABC'   # allowed, _ABC_ matches but _its-not-abc_ also matches and has precedence

Update default maxscriptsizepolicy, maxscriptnumlengthpolicy
------------------------------------------------------------

The default values for **maxscriptsizepolicy**, **maxscriptnumlengthpolicy** config options have been updated.

*   Default value for **maxscriptsizepolicy** = 500,000 (up from 10,000)
*   Default value **maxscriptnumlengthpolicy** = 10,000 (down from 250000)

P2P Header Update
-----------------

Every P2P message on the network has the same basic structure; a 24 byte header followed by some payload data. One of the fields within the header describes the length of that payload, and is currently encoded as a **uint32_t**. This therefore limits the maximum size of any message payload to 4GB. In order to support block sizes greater than 4GB, a change has been made to the P2P message structure to overcome this limitation.  

### Versioning

As of this release the P2P protocol version number has been bumped from 70015 to 70016. Doing this allows a node to know in advance whether a connected peer will understand the new extended message format and therefore avoid sending such messages to that peer. Conforming nodes must not send messages in the extended format to peers with a version number lower than 70016, or they will be banned.  

### Change Description

In summary, the changes to the P2P message involve the use of special values of fields within the existing P2P header as flags that can be recognised by a peer that supports such changes to indicate that this is a message with a large payload. These special values also allow a peer that doesn't understand them to reject such a message and fail cleanly if it were to come across one.  
  
The existing P2P header contains a 12 byte message type field. We introduce a new message type in this field **extmsg** (short for extended message) that when seen will indicate to the receiver that following this message header are a series of new extended message header fields before the real payload begins.  
  
The proposed full extended message format is shown below:  

| Field_Size | Name | Data_Type | Description |
| --- | --- | --- | --- |
| 4   | magic | uint32\_t | Existing network magic value. Unchanged here. |
| 12  | command | char\[12\] | Existing network message type identifier (NULL terminated). For new extended messages this would take the value **extmsg**. |
| 4   | length | uint32\_t | Existing payload length field. Currently limited to a maximum payload size of 4GB. For new extended messages this will be set to the value **0xFFFFFFFF**. The real  payload length will be read from the extended payload length field below. |
| 4   | checksum | uint32\_t | Checksum over the payload. For extended format messages this will be set to **0x00000000** and not checked by receivers. This is due to the long time required to  calculate and verify the checksum for very large data sets, and the limited utility of such a checksum. |
| 12  | ext\_command | char\[12\] | The extended message type identifier (NULL terminated). The real contained message type, for example **block** for a > 4GB block, or could also conceivably  be **tx** if we decide in future to support > 4GB transactions, or any other message type we need to be large. |
| 8   | ext\_length | uint64\_t | The extended payload length. The real length of the following message payload. |
| ?   | payload | uint8\_t\[\] | The actual message payload. |

sendrawtransactions - option to skip some policy checks
-------------------------------------------------------

It is now possible to override policy checks per transaction or per whole batch (transaction specific overrides have precedence over batch specific overrides) when using _sendrawtransactions_ RPC.   
These parameters do not override consensus rules. 

The following configuration parameters can be overridden: _maxtxsizepolicy, datacarriersize, maxscriptsizepolicy, maxscriptnumlengthpolicy, maxstackmemoryusagepolicy, limitancestorcount, limitcpfpgroupmemberscount, acceptnonstdoutputs, datacarrier, dustrelayfee, maxstdtxvalidationduration, maxnonstdtxvalidationduration, minconsolidationfactor, maxconsolidationinputscriptsize, minconfconsolidationinput, acceptnonstdconsolidationinput, dustlimitfactor, skipscriptflags_

Example1: Override policy for transactions individually  
sendrawtransactions(\[{'hex': signed\_tx, 'dontcheckfee': True, 'config': {"maxtxsizepolicy": 10000000, "minconfconsolidationinput": 200, "skipscriptflags": \["CLEANSTACK", "DERSIG"\]}...\])

Example2: Override policy for all transactions in the batch  
sendrawtransactions(\[{'hex': signed\_tx, 'dontcheckfee': True}, ...\], {"maxtxsizepolicy": 10000000, "minconfconsolidationinput": 200, "skipscriptflags": \["CLEANSTACK", "DERSIG"\]})

New ZMQ Topics
--------------

New ZMQ topics have been added to the node. The differences between original and new ZMQ topics are shown below.

#### **Original rawtx, hashtx, rawblock, hashblock topics**  
tx notifications:

*   publish transaction when it is accepted to mempool
*   publish transaction when block containing it is connected
*   publish transaction when block containing it is disconnected

block notifications:

*   publish block when it's connected
*   on reorg only publish the new tip

When a reorg happens, we get notified 3 times for the same transaction (from a disconnected block):

*   on block disconnect
*   when the transaction is accepted to mempool
*   when the new block, the transaction is added to, is connected

#### **New rawtx2, hashtx2, rawblock2, hashblock2 topics**  
tx notifications:

*   publish transaction when it is first seen:
*   accepted to mempool, or
*   received in a block if we didn't have it in mempool yet
*   when we mine (connect) a block, we publish transactions that weren't published before (usually only coinbase)
*   when we receive a valid block, we publish all the transactions that weren't published before (the ones in block and not in our mempool)
*   we don't publish the transaction when block containing it is disconnected

block notifications:

*   we publish every connected block

When a reorg happens, we get notified once for each transaction and connected block:

*   when transaction from a disconnected block is accepted to mempool, or
*   when the new block from the longer chain that contains the transaction is connected
*   we publish each connected block from the new, longer chain, not only the tip
*   ! coinbases from the disconnected blocks are also not published !

Configurable Timeouts
---------------------

New configuration options are available to address potential issues with download timeouts.

*   **blockdownloadtimeoutbasepercent** (default=100% = 10 minutes)
*   **blockdownloadtimeoutbaseibdpercent** (default=600% = 60 minutes)
*   **blockdownloadtimeoutbaseperpeerpercent** (default=50% = 5 minutes)

Block Download Timeout Calculation

If the block is being downloaded as part of an initial block download (IBD), then the

_base-timeout_ = **blockdownloadtimeoutbaseibdpercent,**

otherwise

_base-timeout_ **\= lockdownloadtimeoutbasepercent.**

The timeout is calculated as follows

Max. download timeout = 10 minutes x (_base-timeout_ + no-of-peers x **blockdownloadtimeoutbaseperpeerpercent**)

For example, if there are a 3 connected peers, the default IBD timeout will be 10 minutes x (600 + 3 x 50)% = 10 minutes x (750%) = 75 minutes.
