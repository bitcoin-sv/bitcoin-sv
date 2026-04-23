# Bitcoin SV Node software – v1.2.1 Release

Overview
========

The 1.2.1 node release is a mandatory upgrade from version 1.2.0 that contains
several important bug fixes and improvements.

Important fixes in 1.2.1
------------------------

* Fixed incorrect script stack memory usage tracking for some new Chronicle opcodes.
* Removed an assert in blocktxn message processing that could be triggered by a malicious peer sending a carefully crafted duplicate blocktxn message.
* Fixed the handling of malformed coinbase transactions with bad minerID miner info.
* Fixed inconsistent handling of an invalid minerID document during a reorg.
* Fixed a bug in REST API handling that didn't correctly parse negative ranges.
* Fixed libevent related issues that could cause out of memory in the node if a webhook endpoint sent excessively large payloads or headers.
* Fixed incorrect hashing of a dsdetected P2P message.

Other items
-----------

* Improved the reliability of a number of functional tests.
* Migrated the functional test framework from asyncore to selectors module.
* Dead code removal.
* General minor code fixes and improvements.

New or modified configuration options
-------------------------------------

* **-minerid** enable option is now off by default.
* **-dsnotifylevel** option is now 0 by default.
* **-rpcwebhookclientmaxresponsebodysize** sets the maximum response body size that the node will accept from a webhook client.
* **-rpcwebhookclientmaxresponseheadersize** sets the maximum response header size that the node will accept from a webhook client.
* **-importsync** is a new debug option that forces the node to load all data from disk synchronously, which can be useful for testing and debugging.
