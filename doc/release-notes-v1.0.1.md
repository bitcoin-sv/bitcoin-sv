# Bitcoin SV version 1.0.1 Release Notes

## List of Changes since 1.0.0
* Update Windows build instructions for VS 2019
* .vscode added to .gitignore
* optimize getblock/getblockrawtransactions for large transactions
* Fix unit tests that are failing because of the change in JSON formatting
* Misc document changes for Genesis
* Switch to disable replacement (non-final) transactions
* Upgrade database format for larger blocks
* Policy limit on size of transaction inputs
* Change default for maxtxnvalidatorasynctasksrunduration from 60 seconds to 10 seconds
* Fix: BSVRD-593 - accepting headers for Block 1
* Orphan txn size of 10MB should also be accepted
* Fix: lshift_big_int and rshift_big_int failing on windows
* Provide -invalidateblock CLI switch
* Fix disconnect pool limit overflow
* Fix: autotools build system uses default compiler version gen

## System Requirements
The latest **minimum** system requirements for running a Bitcoin SV node can be found on the website
[here](https://bitcoinsv.io/2019/08/02/bitcoin-sv-node-system-requirements/)

These minimum requirements have been recently updated, so please take note.

## Scaling Test Network (STN) Reset
NA

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
* [Version 0.2.2.beta](release-notes-v0.2.2-beta.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.2.beta/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.2.beta) - 2019-10-30
* [Version 1.0.0](release-notes-v1.0.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.0) - 2020-01-15
