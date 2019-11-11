# Bitcoin SV version 0.2.2 Release Notes

This is an optional beta release for the BSV mainnet; however it is a required update for the Scaling Test Network. 
Miners are encouraged to test this release carefully and maintain a v0.2.1 version on standby.

## List of Changes
* Do not load whole block into memory when responding to getblock request
* Windows build errors
* add bitcoin-miner to CMakeLists && Add Visual Studio 2017 Linux remote debug support
* Mempool journal for mining candidates.
* Use the journal to build incremental mining candidates
* Race condition in ConnMan::ParallelForEachNode
* Remove further limits on OP_RETURN data
* Do not load whole block in into memory when sending it to another peer though P2P protocol
* Compilation error with --disable-wallet (external report)
* Journal functional test tries to spend OP_RETURN
* Backport - Log difference between block header time and received time when competing blocks are received for the same chain height
* Use single copy of helper functions in functional tests
* Provide a way to communicate protocol message size and other limits to other peers
* bsv-block-size-activation-default test is failing
* fix bitcoin-util-test.py
* Use OP_FALSE OP_RETURN when generating transactions containing data
* Rest API /rest/block/BLOCK-HASH.json not working when response greater than 2 MB
* Include thread name in debug log message
* Remove Redundant Static Object CStnParams stnParams
* Fix activation Quasar activation tests that fail after 24.7.2019
* RPC call to test a block template for validity without a valid PoW
* Hardcoded activation time in functional tests
* Make maxminedblocksize configurable by RPC
* Remove BIP9 related code
* Add support for c++ run time checking tools to build scripts
* Only include thread name in debug message at the beginning of new lines
* Parallel validation of transactions
* Scaling Test Network reset

## Scaling Test Network (STN) Reset
The STN blockchain has grown to over 1TB and it has served its current purpose of producing 2GB blocks and 
1 million transactions per block. The STN is being reset to reduce the size of the blockchain (from ~1.5TB to about 
1MB). 

The Bitcoin SV Node implementation will automatically switch to the new blockchain, however this may take a large amount
of time and it will not automatically clear the blocks from the old blockchain from disk. We recommend that you manually
clear the old block data and start from scratch with the STN. For more information, see the 
[bitcoinscaling.io website](http://bitcoinscaling.io/oct-2019-stn-rollback).
  
* The Scaling Test Network has been reset at block height 1. This block has hash 
  `00000000e23f9436cc8a6d6aaaa515a7b84e7a1720fc9f92805c0007c77420c4`.
* Previous releases of Bitcoin SV node software are not compatiable with this reset. Please upgrade to continue using the STN
* Acceptance of low difficutly blocks after 20mn has been disabled to bring behaviour more in line with mainnet
* STN Blocks and Chaindata folders can be deleted before running this release.

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
