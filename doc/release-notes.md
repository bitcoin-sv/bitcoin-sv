# Bitcoin SV version 0.2.1 Release Notes

## List of Changes
    getblock optionally returns rpc transaction details
    fix to univalue read to mitigate memory issues. 
    New parameter (-factorMaxSendQueuesBytes) which limits total size of blocks currently in queue for sending to peers.
    Help message for "-preload" options moved to another section.
    libgen.h included unconditionally in vmtouch.cpp.
    Fixes for Mac and Windows builds.
    Minor refactoring and potential bug fixes.
    Remove Bitcoin ABC cashaddr address format.
    Increase UTXO cache default to hold the entire UTXO set in cache.
    Calls to TestBlockValidity() call are now configurable using -blockcandidatevaliditytest option.
    Support for OP_FALSE OP_RETURN as a standard transactions
    Pyhton code: Fix for array.tostring deprecated warning
    Update the OpenBSV license for Testnet/Regnet/STN

## Scaling Test Network Reset
NA

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
