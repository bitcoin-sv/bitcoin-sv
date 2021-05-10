# Bitcoin SV version 1.0.8 beta Release Notes

## Headline changes since 1.0.7
* Support for creating double-spend enabled transactions; if a double-spend of an input is seen,i
a HTTP notification will be sent to a specified endpoint. The specification for such double-spend
enabled transactions can be found [here](https://github.com/bitcoin-sv-specs/protocol/double-spend-notifications.md).
* New getorphaninfo RPC.
* New verifyScript RPC.
* New getmerkleproof2 RPC consistent with latest TSC.
* maxstackmemoryusageconsensus parameter added to output of getsettings RPC.
* sendrawtransaction and sendrawtransactions RPC can be used with dontCheckFees even when a transaction is already known.
* sendrawtransaction and sendrawtransactions RPC modified to optionally return list of unconfirmed parents.
* New command line option -dustlimitfactor available to define dust.
* Inputs with dust amounts can now be returned to miners as a fee even if below the miner fee rate, when sent via single zero amount OP_FALSE OP_RETURN 'dust' output.
* Adjusted default maximum validation duration for async tasks to better handle chains and long graphs.
* Change algorithm for using ancestor height rather than ancestor count.
* Improve release rate from Oprhan pool.
* Better performance as a result of improvements to cs_main processing.
* Improved validation of chains.

## Technical changes
* Remove all support for Tor.
* Configure docker images so that core dump is preserved.
* Include header files in CMAKE build.
* Make CBlockIndex thread safe to remove cs_main dependency.
* Encapsulate mapBlockIndex and remove cs_main dependency.
* Create leaky bucket for tracking peer suspension score.
* Remove non-ctor init function in networkprocessing.
* Soft block orphaning support (not used).
* Refactor NET logging category.
* Move lNodesAnnouncingHeaderAndIDs into BlockDownloadTracker.
* Replace rand with the facilities in <random>.
* Allow parallel downloads of the same block from several peers.
* Error message can be of 'txid' instead of 'hash' at the beginning for incorrect txid value while executing getmerkleproof RPC as like verifymerkleproof RPC.
* GlobalConfig class is not thread safe.
* dev branch clang warning -Wdefaulted-function-deleted.
* Initialize class members where clang requires.
* Fix: P2P Messages In Multiple TCP Segments.
* Improve detection of longer chains.
* Improved orphan transaction processing.
* Reduce Logging Cost.
* Fix runtime errors in unit test (Windows os).
* Global thread-local interferes with address sanitizer.
* Remove libatomic dependency.
* Undefined behaviour sanitiser reporting vector out of bounds access in CBlockFileInfoStore::FlushBlockFile.
* May 2021 STN reset.
* Fix: St16invalid_argument displays many times on startup.
* Falsely logged error opening rev<xxxx>.dat in logs.
* Fix: coredump at shutdown within logging.
* Fix: failure in CTxMemPool::GetMemPoolChildrenNL.
* Fix: a possible false negative result during querying the PTV processing queue.
* Remove maxcollectedoutpoints configuration parameter.
* Updated default value for maxorphantxsize: from 100 MB to 1 GB
* Updated default value for limitancestorcount: from 1000 to 10000
* Improved stability when accepting transactions during the reorg.

## Technical change details

### New command line option -dustlimitfactor available to define dust.

Until now, the minimum ratio between a transaction output amount and its corresponding fee is 3/1.
If this condition was not met, the transaction was considered “dust” and was rejected during validation.

This factor can now be configured via the new “*-dustlimitfactor*” option in percent, which still defaults
to 300% but can be set to any value between 300% and 0%.

If the *-dustlimitfactor* is set to zero, then no transaction output is regarded as dust.

The formula for calculating the dust threshold is as follows (integer arithmetic):

    s = serialized size of transaction output
    d = dustlimitfactor, percent value between 300 and 0, default: 300
    r = dustrelayfee, default: default-minrelaytxfee, 250 as of v1.0.8
    m = 148, minimum bytes of spendable input
    
    d * (r * (s + m)/1000)) / 100

Note that the division by 100 as the dustlimitfactor specifies a percentage value. Note also, that the setting
of the parenthesis is important due to integer arithmetic.

A transaction exactly at the threshold passes. If one output is below the threshold, the transaction is rejected.

Example: For a typical transaction with an output of 34 bytes in size, the formula yields a threshold of 135 Satoshis.

    d = 300
    s = 34
    r = 250
    
    Threshold = (300 * (250 * (34 + 148)/1000)) / 100 = 135

Note the same formula would give a threshold of 136.5 Satoshis if floating point arithmetic is used.

Consequently, with the default parameter settings in 1.0.8, a typical transaction output needs to be worth 135
Satoshis at least to be not considered dust. Note that due to rounding, the calculated dust threshold for the
example transaction becomes 0 if the *dustlimitfactor* and the *dustrelayfee* are very low.

### Free consolidation transaction can be used to avoid dusting attack.

Transaction validation has now been relaxed to allow a new kind of transaction, the dust return transaction.
This new transaction type allows donating dust to miners via fees as a new way to counter wallet dusting attacks.
This is more economical for the network because it allows clearing the wallets and UTXO databases from otherwise
practically unspendable outputs, whilst completely removing the incentive to conduct dust attacks at all.

A transaction is considered a dust return transaction if the following conditions are met:

- it has only one output.
- the txn outputs a zero amount, i.e. the single output of the transaction has zero value.
- The scriptPubKey is as follows:

        OP_FALSE OP_RETURN n ‘dust’.

    where n = length of the protocol ID, i.e. string size of ‘dust’.

- Either all inputs are standard or configuration following parameter is set:

    *-acceptnonstdconsolidationinput=1* (default: 0)

- Non standard transactions must be allowed, i.e. following configuration must be set:

    *-acceptnonstdtx=1* (default: 1)

Note that dust return transactions share two configuration parameters with consolidation transactions:

- *-acceptnonstdconsolidationinput* (default: false)
- *-minconsolidationfactor* (default: not zero. Setting consolidation factor to 0 disables consolidation transactions and dust return transactions as well)

## Functional tests
* Fix: import-rescan.py - possible race condition during tip update signaling.
* Fix: bsv-factorMaxSendQueueBytes functional test fails on Windows.
* Remove test/test_bitcoin_fuzzy.cpp.
* Fix: failing functional test bsv-getdata.py.
* Add a new FT to test long chains of CPFP txs.
* Fix: bsv-magicbytes.py functional test failed.
* Fix: internittent failure in bsv-block-propagation-priority.py.
* Fix: Random failures of bsv_pbv_firstvalidactive.py functional test.
* Fix: Failing util tests on Windows.
* Fix for issue detected by bsv-dsreport.py and bsv-ds-bad-callback-service.py on Windows.
* Wait_for_getdata method is misused in functional tests.

## Security
* Remove warnings from source code.
* Update out-of-date dependencies with known security issues.
* Lock consistency violations in WPUSMutex.
* CReeRate calculation of CPFP groups allows invalid amounts.
* Incorect usage of cs_main.

## Known issues
* A double-spend notification might not be triggered if a previously invalid double-spend was detected.

## Scaling Test Network (STN) Reset
The Scaling Test Network has been reset at block height 5. This block has hash 
`00000000e9222ebe623bf53f6ec774619703c113242327bdc24ac830787873d6`.

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
* [Version 1.0.7](release-notes-v1.0.7.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.7/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.7) - 2021-02-10
* [Version 1.0.7.1](release-notes-v1.0.7.1.md) - [Download](https://download.bitcoinsv.io/bitcoinsv/1.0.7.1/) - [Source](https://github.com/bitcoin-sv/bitcoin-sv/tree/v1.0.7.1) - 2021-04-22
