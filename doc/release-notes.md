# Bitcoin SV version 1.0.0-beta Release Notes

## List of Changes
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

## Scaling Test Network (STN) Reset
NA

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
* [Version 0.2.2.beta](release-notes-v0.2.2-beta.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.2.beta/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.2.beta) - 2019-10-30
