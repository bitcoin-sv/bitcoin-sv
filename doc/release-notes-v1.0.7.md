# Bitcoin SV version 1.0.7 Release Notes

## Headline changes since 1.0.6
* Removed performance penalty for long chains. Mempool performance does not depend on the transaction's chain length anymore.
* Legacy block assembler removed; support for low priority / free transactions removed.
* Refactoring of CCoinView.
* P2P improvements.

## Full list of changes between 1.0.6 and 1.0.7.beta
* Increased default maximum ancestors count for paying transactions to 1000.
* Removing transactions from the mempool when a new block is mined is much faster. As a consequence, block propagation is faster.
* More efficient mempool transaction eviction algorithm.
* Removed the Legacy block assembler; Journaling block assembler is now the only block assembler available.
* Efficient detection of the CPFP (child pays for the parent) groups.
* Limiting the total size of the low-paying transactions in the mempool as the percent of the mempool size.
* Limiting the ancestors count for the low-paying transactions to 25, and as consequence, limiting the maximal size of the CPFP groups.
* Stopped limitting transaction chains by ancestors size in the mempool.
* Stopped limitting transaction chains by descedant statistics, both size and count.
* Removed concept of the "high priority / free transactions". We are not accepting nor relaying them any more.
* Added command line switches:
    - mempoolmaxpercentcpfp, Percentage of total mempool size to allow for low paying transactions. Default: 10%
    - limitcpfpgroupmemberscount, Maximal number of non-paying ancestors a single transaction can have. Thus, the maximal size of a CPFP group. Default 25.
    - disablebip30checks Disables BIP30 checks when connecting a block. Not active on the mainnet.
* Removed command line switches:
    - limitancestorsize, Not tracking ancestors size in the new implementation.
    - limitdescendantcount, limitdescendantsize Not keeping descendant statistics in the new implementation.
    - limitfreerelay relaypriority printpriority blockprioritypercentage Removed concept of the "free / high priority" transactions
* The prioritisetransaction RPC method ignores the first positional argument (modified priority), which must be 0, and uses only the second argument (modified fee) into account.
* Other changes to the RPC methods, added and removed JSON entries:

| RPC Method | Entries Added | Entries Removed |
|:----------------- |:------------------- |:----------------------- |
| getrawmempool, getmempoolancestors, getmempooldescendants, getmempoolentry |  | startingpriority, currentpriority, descendantcount, descendantsize, descendantfees, ancestorcount, ancestorsize, ancestorfees |
| getmempoolinfo | usagedisk, usagecpfp, maxmempooldisksize, maxmempoolsizecpfp | |
| getmininginfo | | blockprioritypercentage |
| getblocktemplate | | sigops, sigoplimit |

* Implement HTTP streaming for getblocktemplate
* Do not load large blocks into memory
* Performance - CCoinView refactoring
* Endomorphism is now ON by default in libsecp256k1
* New configurable connection timeout for validating blocks.
* Serialize in getblock RPC without cs_main
* Maximum size of INV messages configurable
* Change -rejectmempoolrequest default to true
* Separate P2P Control and Data Channels
* Implement HTTP streaming for getrawmempool
* Expose consensus and policy limits through RPC
* Improve interface to Stream::GetNewMsgs
* Fix: If setAskFor fills up then transaction requesting stops.
* Simplify JSonWriter
* Allow the available stream policies to be set by the config
* Message processing stall remedy
* Send GETDATA for blocks over the higher priority stream.
* Fix: Slow reading of coins from database when using leveldb iterators
* sendrawtransaction RPC - fix PrioritiseTransaction/ClearPrioritisation usage to be exception safe.
* sendrawtransaction RPC - functional test – send already known transaction and create block.
* sendrawtransaction RPC - check if transaction is already known before validation starts.
* Remove bitcoin-seeder.
* Fix: Coredump at shutdown with scriptCheckQueuePool
* Fix: Consolidation tx option 'acceptnonstdconsolidationinput' uses wrong type
* Fix: Consolidation variable name and update function to respect interface
* Fix: maxConsolidationInputScriptSize parameter should use GetArgAsBytes() function in init.cpp
* Fix: Possible XSS vulnerability
* Fix: Update Windows build instructions
* Fix: sendtransactions PRC should use JSON response streaming
* Fix: Implicit loop conversions
* Unused code removed
* False positive check for null dereferencing in merkle proof
* Explicitly define the missing special members for ZMQPublisher
* Decimal is not defined. Change its name or define it before using it
* Fix: getutxos RPC should serialize integer when serializing RPC call id
* Removed call to virtual function Flush() in dtor of text writer
* Refactor default parameters of virtual function in TextWriter
* Backport performance improvement to rolling bloom filter
* Fix: bench_bitcoin fails on build with --disable-wallet
* Use pointer or reference to avoid slicing from "CAddress" to "CService
* Fix: Uninitialized member in CWalletDBWrapper
* Revert unordered->ordered map/set for COutPoint -> coins.
* ZMQ: add method for distinct zmq ports when running functional tests in parallel
* Fix: snprintf overflows write buffer
* Remove 'bitcoin' prefix from worker thread names
* Prevent unnecessary copies in range loops
* Fix: Unhandled nullptr in GetDiskBlockStreamReader if file does not exist
* Add list of supported stream policies to the output of getnetworkinfo
* Provide an option to increase the number of open files for leveldb. Increasing number of open files improves performance of transaction validation.
* Fix: Starting bitcoind with negative value for consolidation transaction parameters is setting very high values internally
* Remove dead code that was accidentally introduced by parallel block validation task
* Fix: cURL example of gettxouts RPC help
* Add stream type enumerations to python test framework
* Test framework - make Key.py locate correct openssl implementation on Mac
* Test framework: add tx hash to log
* Make using network associations in the functional tests easy.     
* Replace time.sleep with wait_until in reindex.py
* Protect all uses of txinvs with mininode_lock in bsv-consolidation-feefilter.py
* Add an extra config params to the bsv-ptv-rpc-sendrawtransactions.py functional test.
* Fix: Functional test bsv-genesis-activation-transactions-beforeafter fails occasiobally.
* Fix: bsv-pvq-timeouts.py functional test fails occasionally
* Fix: bsv-genesis-large-blockfile-reindex.py functional test fails occasionally
* Fix: bsv-genesis-journal-reorg_2.py functional test fails occasionally
* Fix: Functional test bsv-genesis-general fails with timeout in height_based_test_framework
* Fix: Functional test bsv-sigopslimit-consensus-test.py fails occasionally.
* Fix: bsv-128Mb-blocks functional test fails occasionally
* Fix: example-test.py functional test fails occasionally
* Fix: bsv-coinbase_P2SH.py functional test fails occasionally
* Fix: bsv-sigopslimit-policy-test.py functional test fails occasionally
* Fix: p2p_inv_msg_time_order2.py functional test fails occasionally
* Fix: Functional test bsv-pvq-timeouts sometimes fails with timeout
* Fix: Functional test bsv-genesis-spendable-op-return fails occasionally.
* Fix: Functional test bsv-genesis-general (Increased max policy tx size) fails.
* Fix: Functional test mining journal fails occasionally.
* Fix: Functional test bsv-block-stalling-test.py fails occasionally.
* Fix: Functional test p2p-fullblocktest fails occasionally.
* Fix: Functional test abc-p2p-compactblock fails occasionally.
* Fix: txn_clone test fails if transaction is sent after nodes reconnect
* Fix: Functional test bsv-mempool_ancestorsizelimit can fail on a busy environment
* Fix: merkle_proof.py generate takes to long for 1500 blocks
* Fix: Functional test bsv-block-size-activation-generated-default fails occasionally.
* Fix: Functional test bsv-consolidation-p2pkh.py fails occasionally.
* Fix: Functional test bsv-journal-mempool-reorg-ordering.py fails occasionally.
* Fix: test bsv-zmq-txremovedfrommempool.py
* Race condition while running zmq_test.py
* Fix: Address sanitizer issue with unit tests.
* Fix: Failing rpc_tests/rpc_ban
* Fix: Unit tests fail to build
* bsv-protoconf.py starts to fail with large value of maxprotocolrecvpayloadlength
* Explicitly define the missing copy constructor

## List of changes since 1.0.7.beta
* Ensure correct `id` type in RPC
* New RPC dumpparameters to provide configuration settings.
* Help text - coinbaseValue parameter added to help text for getminingcandidate RPC.
* Packaged Diagnostic Service - allows remote gathering of diagnotic information.
* The default value for maxcoinsprovidercachesize = 1GB and must be at least 1MB.
* The mempoolminfee (rolling fee) is prevented from becoming greater than blockminfee while there are secondary mempool transactions.
* Fix: Heap used after free during shutdown.
* Fix: possible undefined behaviour processing block requested via getdata.
* Fix: Undefined behaviour in RIPEMD160, SHA1 and SHA256 code.
* Fix: Possible referenced null pointer triggered by help RPC command.

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
* [Version 1.0.5](release-notes-v1.0.5.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.5/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.5) - 2020-09-08
* [Version 1.0.6](release-notes-v1.0.6.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.6/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.6) - 2020-11-17
