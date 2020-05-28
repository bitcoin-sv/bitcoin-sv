# Bitcoin SV version 1.0.4 Release Notes

The Swift 1.0.4 release focuses primarily on performance-related issues.

Note that if the minimum mempool size is now less than 30% of the default
mempool size, the node will not start.

## List of Changes since 1.0.3
* Gitian build fix: instruction[_iterator].h location in Makefile.am.
* Ban nodes that violate the `maxscriptnumlengthpolicy` policy setting.
* Fix race condition in bsv-trigger-safe-mode-by-invalid-chain.py.
* Fix: bug in thread pool tests.
* Fix: Failing unit tests in debug on develop branch.
* Fix: P2P stops sending data under some circumstances.
* Remove excess physical dependencies (#includes) on script.h.
* Fix: Missing debug flags in CMake.
* Minimum mempool size should be at least 30% of default mempool size.
* Optimise frequent malloc calls in GetOp2.
* Reduce the number of orphan transactions during PTV processing.
* Windows build; Separate running tests from C++ build.
* Fix: formatting of floating point number in log messages.
* Implement caching invalid signatures.
* Fix: P2P getaddr returns very polluted results.
* Increase default script cache size to improve performance with large blocks.
* Fix: Failing functional tests.

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
