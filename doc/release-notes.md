# Bitcoin SV version 1.0.3 Release Notes

## List of Changes since 1.0.2
* Fix: user input to rpc submitblock can crash node
* Fix: PTV race condition in bsv-highsigopsdensitymempool functional test
* Racy test in bsv-block-stalling-test.py
* Fix duration profiling for "Connect transactions" in ConnectBlock()
* RPC sendrawtransaction doesn't accept last argument 'dontcheckfee'
* Improve transaction propogation under high load.
* Binary src/bench/bench_bitcoin aborts due to failing benchmark
* Prevent P2SH outputs in coinbase transactions
* Fix compiler error for MSVC trigger introduced  in DetectStaling
* Configurable P2P timeouts
* Excess Padding in CTxMemPoolEntry removed
* Fix forknotify.py test and consider CI improvements
* Fix: Orphan txn processing keeps CNode reference too long
* Fix gcc-9 warnings
* Perform quick block validation in large reorg
* Do not activate safe mode for large forks with invalid blocks
* Remove "Warning: Unknown block versions being mined! It's possible unknown rules are in effect"
* Remove P2SH test data from Makefile
* Log calls to RPC function stop
* getminingcandidate - include tx count + block size (excluding coinbase transaction)
* Genesis (v1.0.1) :  Typo on maxstackmemoryusageconsensus
* TOB;#20 Update OpenSSL depends package from 1.1.1 to 1.1.1d
* Fix fseek call for large offsets on Windows
* RPC API returns invalid JSON in some cases
* Add capability to ban peers based on their user agent
* Update help message for ancestor and descendant size limit
* Add field "num_tx" to RPC "getblock <hash> 3"
* bitcoin-tx supports encapsulating in P2SH
* './test_runner.py --large-block-tests' command test scripts failed
* Two functional tests fail due to recent change in validation duration
* Remove unused variables in config_tests.cpp
* Fix: IBD stalls at block 5095 (1GB transaction) GT network
* Fix unit tests that are failing because of the change in JSON formatting
* [GITHUB-131] provide compilation support on 32-bit systems without changing behavior on 64-bit
* Orphans that exceed non standard queue validation times could be processed multiple times
* RPC getinfo warns that version 1.0.0 is pre-release
* CBaseChainParams Lacks Virtual Destructor
* [GITHUB-115] Update minimum required version of Boost to 1.59.0
* [GITHUB-114] Fix typo: isGenesisEnsbled -> isGenesisEnabled
* [GITHUB-122] Fix typos in RDP
* [GITHUB-121] Replace ABC with SV logo in Doxygen
* Consider removing -excessutxocharge startup parameter
* TOB; #17 Several integer variables could overlow during block validation
* Include additional logging in the new Height based Test framework
* [Github] Fix duplicate misbehaving message for sendheaders
* Update the Responsible Disclosure Policy
* Random crashes when running modified version of bsv-txnvalidator_p2p_txns.py
* Rename acceptp2sh to acceptp2sh_ashashpuzzle
* OptBool net_processing.cpp cleanup
* Genesis 1.0.0.beta: MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS is missing in test/functional
* fix memory leak of asn1_integer
* Fix call to PeerHasHeader
* Fix MedianTimePast
* Fix boost::optional warnings
* additional op_checkmultisig unit tests
* Block send queue overflow
* Increase or remove limits - SPV bloom filters attack mitigation
* Improve performance of prevector deserialization
* bitcoin-cli help getblock not working in 0.2.2
* Remove the estimatefee RPC command
* Add 'getblockstats' RPC call
* Backport fix for CVE-2017-18350 - Severity: low
* Gitian build broken. big_int.hpp not found.
* Remove CBlockPolicyEstimator
* bitcoind crashes when too many RPC calls are made
* Parallel Block Validation
* Increase or remove limits - script size, op codes per script, etc
* Remove the limit on the number of CHECKSIGS per MB of block space
* Fix: autotools build system uses default compiler version (which might not be C++17) for some of the build directories
* Fix: Interrupt (^c) does not work during block verification in initialization

## Scaling Test Network (STN) Reset
NA

# Previous Releases
* [Version 0.1.0](release-notes-v0.1.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.0) - 2018-10-15
* [Version 0.1.1](release-notes-v0.1.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.1.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.1.1) - 2019-02-11
* [Version 0.2.0](release-notes-v0.2.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.0) - 2019-06-05
* [Version 0.2.1](release-notes-v0.2.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.1) - 2019-07-12
* [Version 0.2.2.beta](release-notes-v0.2.2-beta.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/0.2.2.beta/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v0.2.2.beta) - 2019-10-30
* [Version 1.0.0](release-notes-v1.0.0.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.0/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.0) - 2020-01-15
* [Version 1.0.1](release-notes-v1.0.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.1) - 2020-01-28
* [Version 1.0.2](release-notes-v1.0.2.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.2/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.2) - 2020-02-17
