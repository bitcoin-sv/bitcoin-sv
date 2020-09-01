# Bitcoin SV version 1.0.5 Release Notes

## List of Changes since 1.0.4
* Make Journaling block assembler the default. The legacy block assembler is scheduled for removal in a future release.
* Journaling block assembler now respects -blockmintxfee
* New consolidation transaction policy. Consolidation transactions are now processed for free.
* New RPC function sendrawtransactions - support submitting multiple transactions in a single call.
* PTV: optimise txn lookup
* Allow size values in bitcoin.conf to be set with human readable values
* Update dust relay fee
* Increase size limit for GETBLOCKTXN messages.
* Stop NetworkThread when functional test ends
* Split the network properly in functional tests
* example_test.py inconsistency
* bsv-genesis-mempool-scriptcache.py functional test fails
* Fix bsv-genesis-general.py by increasing timeout for tx to be rejected in height_based_framework
* Increase/fix timeout in comptool sync_blocks and sync_transactions
* sendheaders.py functional test fails
* Fix bsv-genesis-activation-gracefull-period.py by using wait_until() instead of sleep()
* NodeConn constructor can fail if NetworkThread is already running
* Fix disconnect_ban.py by increasing ban time
* Fix for sync_all() "Mempool sync failed" assertion error in mempool_reorg.py test
* IsPayToScriptHash decoupled from CScript
* Prevent blocking the socket handling thread.
* Fix potential deadlock in rpc walletpassphrase
* Fixed values for extra_args: -maxmempool and -maxtxsizepolicy
* Remove redundant code CScript::IsWitnessProgram
* Fix incorrect and misleading comment.
* Fix executable status on tests.
* Failure of bsv-trigger-safe-mode-by-invalid-chain.py
* Fix bsv block stalling test.py
* Fix functional test net.py fail on ping pong
* Reduce number of lines logger stores in memory
* Fix spelling in error message
* Fix_dbcrash.py
* Fix whitespace in RPC help output
* Move opcodes to separate files
* Fix casting alignments in bitcoin-miner.cpp
* Shutdown JBA while unit test deletes blocks.
* Increase timeout for p2p accept to 60
* Update dust threshold amount in transactions test
* Remove use of deprecated zmq api functions
* Functional test conversions.
* Reduce number of chain tip copies
* Race condition in comptool.py
* p2p_compactblocks.py functional test fails with timeout
* bsv-2Mb-excessiveblock.py functional test fails
* Support CMake 3.17 and python 3.8
* Fix verify binaries script
* Increase timeout for _new_block_check_accept in height_based_test_framework.py
* Improve logging in python test framework
* Improve test runner to print currently running jobs
* Refactor Python functional test framework
* Make functional test clean up running bitcoind instances
* Increase timeout for p2p accept to 60
* Add missing #include <array>
* Enable -maxmempool config setting, to accept zero value.
* Fix inconsistency between policy and consensus limits for maxopsperscript
* Add a thread-safe function to clear messages with mininode
* Failing script tests caching invalid signatures
* Update configure script to require version of libzmq >= 4.3.2
* Fix memory buildup if log file doesnt have write permissions
* Journal Rebuild After Block
* Remove redundant code CScript Find
* Clean up comments and dead code from when BIP 125 was removed
* Backport of Core PR#18806 to clarify CVE fix
* Make SOCKET of type int on UNIX
* Refactor CNode
* Remove support for upgrading database from old format.
* Handle exception in ActivateBestChainStep
* Instruction iterator fix for OP_INVALIDOPCODE traversal.
* Optimise relaying of transactions.

## Scaling Test Network (STN) Reset
N/A

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
* [Version 0.2.2.beta](release-notes-v0.2.2-beta.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.2.beta/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.2.beta) - 2019-10-30
* [Version 1.0.0](release-notes-v1.0.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.0) - 2020-01-15
* [Version 1.0.1](release-notes-v1.0.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.1) - 2020-01-28
* [Version 1.0.2](release-notes-v1.0.2.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.2/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.2) - 2020-02-17
* [Version 1.0.3](release-notes-v1.0.3.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.3/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.3) - 2020-04-28
* [Version 1.0.4](release-notes-v1.0.4.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.4/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.4) - 2020-07-01
