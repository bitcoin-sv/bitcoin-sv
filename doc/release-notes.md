# Bitcoin SV version 0.2.0 Release Notes

## List of Changes
* Update to C++17
* Refactor P2P message processing
* Return immediately after processing a P2P reject message
* Fix potential race condition in ActivateBestChain()
* Update Gitian build process
* Remove support for 32-bit & ARM builds
* Remove Magnetic Upgrade activation code
* Improve MSVC compatability
* Refactor block file storage
* Remove limitation on max size of block storage file
* Add Jenkinsfile for automated CI tests
* Refactor log messages to NET category, remove log spamming
* Remove ABC forced obsolescence code
* Update ZeroMQ library version

## Scaling Test Network Reset
The Scaling Test Network has been reset at block height 2951. This block has hash 
`0000000076b49c5857b2daf2b363478a799f95a18852155113bbace94321b0d0`.

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
