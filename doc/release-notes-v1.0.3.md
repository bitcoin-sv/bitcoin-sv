# Bitcoin SV version 1.0.3 Release Notes

## List of Changes since 1.0.2
* Fix errors reported by Address sanitizer
* User input to RPC submitblock can crash node
* Fix: improve functional tests nodes synchronization stability
* STN network reset (April 2020)
* Fix: PTV race condition in bsv-highsigopsdensitymempool functional test 
* Build instructions updated for CentOS 7 and Ubuntu 16.04
* Fix: Racy test in bsv-block-stalling-test.py
* Add reference to BSV Wiki site to documentation
* Fix duration profiling for "Connect transactions" in ConnectBlock()
* RPC getblock method with verbosity 3 throws exception for blocks without CDiskBlockMetaData
* RPC sendrawtransaction doesn't accept last argument 'dontcheckfee'
* Fix lost transactions under high load. 
* Binary src/bench/bench_bitcoin aborts due to failing benchmark
* Prevent P2SH outputs in coinbase transactions
* Fix compiler error for MSVC trigger introduced  in DetectStaling
* Configurable P2P timeouts
* Remove excess Padding in CTxMemPoolEntry
* Fix forknotify.py test and consider CI improvements
* Fix: Orphan txn processing keeps CNode reference
* Add big block tests to CI build
* Fix gcc-9 warnings
* Fix: Journal errors in large reorg (over Genesis boundary)
* Perform quick block validation venturing in large reorg
* Do not activate safe mode for large forks with invalid blocks
* Fix: security@bitcoinsv.io PGP key has expired
* Remove "Warning: Unknown block versions being mined! It's possible unknown rules are in effect"
* Remove P2SH test data from Makefile
* Log calls to RPC function stop
* getminingcandidate - include tx count + block size (excluding coinbase transaction)
* Fix Typo on maxstackmemoryusageconsensus help
* TOB;#20 Update OpenSSL depends package from 1.1.1 to 1.1.1d
* Update help message for ancestor and descendant size limit
* Add field "num_tx" to RPC "getblock <hash> 3"
* bitcoin-tx supports encapsulating in P2SH
* Centos 8 build, functional tests skipped: rpcbind and zmq
* Remove unused variables in config_tests.cpp
* Fix: IBD stalls at block 5095 (1GB transaction) on GT network
* [GITHUB-131] provide for compilation on 32-bit systems without changing behavior on 64-bit
* [GITHUB-124] Test failure on MacOSX
* Remove warning: RPC getinfo warns that version 1.0.0 is pre-release
* CBaseChainParams Lacks Virtual Destructor
* [GITHUB-115] Update minimum required version of Boost to 1.59.0
* [GITHUB-114] Fix typo: isGenesisEnsbled -> isGenesisEnabled
* [GITHUB-122] Fix typos in RDP
* [GITHUB-121] Replace ABC with SV logo in Doxygen
* Remove -excessutxocharge startup parameter
* TOB; #17 Several integer variables could overlow during block validation
* [Github] Fix duplicate misbehaving message for sendheaders
* Update the Responsible Disclosure Policy
* OptBool net_processing.cpp cleanup
* Fix: MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS is missing in test/functional
* Fix asserts and potential null pointer reference in call to PeerHasHeader
* Remove the estimatefee RPC command
* Add 'getblockstats' RPC call
* Backport fix for CVE-2017-18350 - Severity: low
* Remove CBlockPolicyEstimator 
* bitcoind crashes when too many RPC calls are made
* Interrupt (^c) does not work during block verification in initialization

## Scaling Test Network (STN) Reset
The Scaling Test Network has been reset at block height 2. This block has hash 
`0000000040f8f40b5111d037b8b7ff69130de676327bcbd76ca0e0498a06c44a`.

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
* [Version 0.2.2.beta](release-notes-v0.2.2-beta.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.2.beta/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.2.beta) - 2019-10-30
* [Version 1.0.0](release-notes-v1.0.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.0) - 2020-01-15
* [Version 1.0.1](release-notes-v1.0.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.1) - 2020-01-28
* [Version 1.0.2](release-notes-v1.0.2.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.2/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.2) - 2020-02-17
