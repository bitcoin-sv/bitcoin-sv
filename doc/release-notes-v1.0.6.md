# Bitcoin SV version 1.0.6 Release Notes

## List of Changes since 1.0.5
* New RPC to retrieve UTXOs.
* sendrawtransactions returns any double spend transaction IDs.
* Refactor code so HTTP client requests can be made from bitcoind.
* New RPC to list ZMQ notifications and endpoints.
* ZMQ notifications for transaction mempool removal.
* Insecure function (sprintf) no longer used.
* Make ZMQ interface thread safe.
* RPC to provide Merkle proofs for transaction inclusion in blocks.
* Invalid transactions can now be published to ZMQ or stored to disk.
* Ensure the fallback path for packages (bitcoinsv.io) in the Gitian build contains all required packages.
* Fix to Gitian build.
* Fix: Accessing memory after std::move.
* ZMQ: Push information (blockhash) in which block transaction we collided with arrived.
* Do not use deprecated boost bind features.
* Remove logging from consensus.
* Validation status dos cleanup.
* ThreadSanitiser reports issues in thread safe queue tests.
* ThreadSanitiser reported heap-use-after-free.
* Fix: gettxouts parameters CLI type mismatch.
* Fix: CheckTxInputExists should not be used.
* Remove assert and fix shutdown after unsuccessful startup.
* Unconditionally log P2P stall messages if denugp2pthreadstalls is specified.
* Fix: clearinvalidtransactions RPC returned the number of bytes freed as zero.
* Fix: #include <limits> added to script_num.cpp to fix build issues with later compilers.
* Rename ZMQ topic zmqpubremovedfrommempool to zmqpubdiscardedfrommempool.
* November 2020 STN reset.

## Scaling Test Network (STN) Reset
The Scaling Test Network has been reset at block height 4. This block has hash 
`00000000d33661d5a6906f84e3c64ea6101d144ec83760bcb4ba81edcb15e68d`.

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
