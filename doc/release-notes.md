# Bitcoin SV version x.y.z Release Notes

## Important Changes

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

## Scaling Test Network Reset
NA

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
