# Bitcoin SV version 1.0.0 Release Notes

## List of Changes since 0.2.2
* Increase or remove limits, final version of default values
* Update to Genesis block height
* Flexible Genesis upgrade logic
* Clear script cache around genesis
* Tx classifiaction for priority queues
* Fix pessimistic move compiler warning
* Big numbers size limits
* Add PVQ timeouts
* Fix unused variable warning
* Increase or remove MAX SCRIPT SIZE
* Update to Big number error handling
* PTV Transaction Queue Limits
* Update big numbers to support int64 min
* Fix crash - Windows Temporary Lifetime issue
* Fix big numbers constructor issue on Windows
* Increase max script element size and max stack size
* op_lshift - handle large (big number) shifts
* op_split - handle large (big number) shifts
* op_pick2 - handle large (big number) inputs
* op_pick - handle large (big number) inputs
* op_size - handle large (big number) inputs
* op_depth2 - handle large (big number) inputs
* op_within - handle large (big number) inputs
* op_depth - handle large (big number) shifts
* op_num2bin for big numbers
* op_bin2num for big numbers
* Set timeouts for std and nonstd txns
* Reject transactions with P2SH output scripts after Genesis
* Remove limit on the number of CHECKSIGS per MB of block space
* Thread queue related core and checkqueue fixes
* Max orphan tx size
* Transactions with P2SH outputs are no longer treated as non-standard and are rejected by default after Genesis.
* Max orphan tx size
* p2p - handle large (big number) inputs  fullblocktest
* Non-final transactions - Fix P2P Race
* Fix thread safety analysis warnings
* Build fixes after integration
* Increase or remove limits Max transaction size in block
* Increase efficiency of collectdependenttxnsforretry
* Fix missing entry for descendant size limit
* Big number unary ops implementation
* Increase or remove limits Max tx size
* Remove MAX PUBKEYS PER MULTISIG
* Increase or remove limits MAX TX SIGOPS COUNT
* Increase Limit Max Non Push Op Per Script
* Fix multiple entries of the same orphan in the result set
* Genesis update for nLockTime and nSequence
* Fix Unit test (Journal) Crash
* Add PVQ functional test
* Add configurable timeout for validation
* Rename Interpreter Genesis flags
* Big numbers documentation
* OP_CODESEPARATOR
* Big number serialization
* Prioritised validation queues
* Make script execution interruptible
* Fix OpenSSL build failures
* Increase timeout for bsv-block-size-activation-genesis.py
* Big number activation code
* Remove block size limit
* Sunset OP_CHECKLOCKTIMEVERIY and OP_CHECKSEQUENCEVERIFY
* Restore original SIGHASH Algorithm
* Treat disabled opcodes as invalid opcodes
* Sunset P2SH
* PTV-double spend transactions processed at same time
* Gitian build fix - big_int.h
* Reorg txns enable batch validation
* Remove CNodeState dependency on cs_main
* Parallel Block Validation
* PTV Tests for orphans and no-oprhans invalid tx
* Reinstate original OP_RETURN
* Fix big number undefined behaviour
* Not sorting txns in inv msgs
* Fix big number undefined behaviour

## List of Changes since 1.0.0.beta1
* Enable validation of non-standard txns (on the MainNet) when Genesis is enabled.
* Fix bug: Validating non-standard transactions can lock up a node for more than a minute.
* Tx validation uses original reject message and ban score is used
* Set mempool default = 1Gb
* Remove block size from user agent string
* Change default tx fees (default txfee = 0.5 sat/byte, default minrelaytxfee = 0.25 sat/byte)
* Fix undefined behaviour in bsv::deserialize
* Update SPV bloom filters re attack mitigation
* Rename acceptp2sh to acceptp2sh_ashashpuzzle
* Fix block send queue overflow
* Fix bug: bitcoin-cli help getblock not working in 0.2.2
* Improve performance of prevector deserialization
* Uncomment parts of functional tests that was depended on other now complete code.
* Parallel Block Validation
* Enhancements for Parallel Block Validation tests
* Build fix: Configure should fail when it cant detect good version of boost
* Provide unit tests for LimitedStack/vector
* Fix bug: Set the execute bit on all the new Genesis functional tests.
* Remove the limit on the number of CHECKSIGS per MB of block space
* Common Genesis activation code and command line option.
* Fix MedianTimePast
* Bug fix : Nonfinal transactions with no inputs is rejected with incorrect reason.
* Fix memory leak of asn1_integer
* Fix boost::optional warnings
* When validating mempool transactions, we should not send reject message if high priority queue timeout is exceeded
* Fix bug: hexhdr.py does not get included in packaged source
* Change logging messages to be more precise.

## List of Changes since 1.0.0.beta2
* Only allow push data in scriptSig (unlock) scripts
* Revert CORE-167 - Restore original SIGHASH algorithm
* Funtional test fixes
* fundrawtransaction RPC changePosition parameter causing out of bounds access
* Make -maxstackmemoryusageconsensus  and -excessiveblocksize required parameter for node startup
* Ignore sigop limit when verifiying Genesis blocks
* RPC;Treat transcations recevied from merchant API as trusted
* Investigation into high memory usage over long period of time.
* Disable creation of P2SH outputs after Genesis by consensus
* Fix some test issues in Genesis beta2
* Enable validation for non-standard txns (on the MainNet) when Genesis is enabled.
* Fix: Random crashes when running modified version of bsv-txnvalidator_p2p_txns.py
* Fix: OP_LSHIFT and OP_LSHIFT does not work with data larger than 2GB
* Fix: OP_LSHIFT and OP_LSHIFT does not work with shifts larger than 2^31-1 (approx)

## More Details
For a more detailed description of the new Genesis changes and an explanation on them, please
see the Bitcoin SV [website.](https://bitcoinsv.io/genesis-hard-fork/)

## Scaling Test Network (STN) Reset
NA

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
* [Version 0.2.2.beta](release-notes-v0.2.2-beta.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.2.beta/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.2.beta) - 2019-10-30
