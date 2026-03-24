# Bitcoin SV Node Software – v1.2.0 Release

This release is a hard fork which includes changes to the BSV protocol.   

The scheduled TestNet activation height is 1,713,168 (Target is 12:00 midday 14-Jan-2026)

The scheduled MainNet activation height is 943,816 (Target is 12:00 midday 07-Apr-2026)


## Release Summary

The Chronicle release is a follow-up of the Genesis upgrade in 2020 which
restored many aspects of the original Bitcoin protocol and removed many
previously imposed limitations. The Chronicle release continues this
process and incorporates the following main features:

* **Restoration of Bitcoin's Original Protocol**: The Chronicle release
re-introduces several opcodes that were previously disabled or removed
from the script language.
* **Transaction Digest Algorithms**: The BSV Blockchain will now support the
Original Transaction Digest Algorithm (OTDA), in addition to the current BIP143
digest algorithm. Usage of the OTDA will require setting the new CHRONICLE
\[`0x20`] sighash flag.
* **Selective Malleability Restrictions:** The Chronicle Release removes
restrictions that were put in place to prevent transaction malleability.
To address concerns about the re-introduction of sources of transaction
malleability, the application of malleability restrictions will depend on
the transaction version field. Transactions signed with with a version number
higher than 1 [`0x01000000`] will allow relaxed rules, removing strict
enforcement of malleability-related constraints. The relevant restrictions
are:
  * Minimal Encoding Requirement
  * Low S Requirement for Signatures
  * NULLFAIL and NULLDUMMY check for `OP_CHECKSIG` and `OP_CHECKMULTISIG`
  * MINIMALIF Requirement for `OP_IF` and `OP_NOTIF`
  * Clean Stack Requirement
  * Data Only in Unlocking Script Requirement

### Transaction Digest Algorithms

As mentioned above, in the Chronicle Release, the Original Transaction Digest
Algorithm (OTDA) is being reinstated for use.

This change will depend on the usage of the new `CHRONICLE` \[`0x20`] Sighash
bit. By default, users who do nothing will retain the current behaviour (with
`CHRONICLE` disabled).

### Increase the Limit on the Size of Script Numbers

The consensus limit for the maximum size of script numbers will be increased
from 750KB to 32MB. Node operators can continue to set their policy limit for
the size of script numbers using the `maxscriptnumlengthpolicy` configuration
parameter.

### Selective Malleability Restrictions

The Chronicle Release will remove malleability-related restrictions during
script evaluation. For any transactions signed with a version field greater
than 1 [`0x01000000`], the restrictions below will no longer apply to the
transaction. This behaviour requires users and developers to "opt-in", as any
transactions that continue to use a version field of 1 [`0x01000000`] will
keep these restrictions. The malleability-related restrictions being removed
are:

*   **Minimal Encoding Requirement**: The requirement that data elements on
the stack are required to be expressed using the minimum number of bytes.
*   **Low S Signatures**: The requirement that the signature must be the low
"S" value. See [BIP-146](https://github.com/bitcoin/bips/blob/master/bip-0146.mediawiki).
*   **NULLFAIL Checks**: The requirement that all signatures passed to an
`OP_CHECKSIG` or `OP_CHECKMULTISIG` must be empty byte arrays if the result
of the signature check is FALSE.
*   **NULLDUMMY Checks**: The requirement that the dummy value consumed by an
`OP_CHECKMULTISIG` is equal to zero.
*   **MINIMALIF Requirement**: The input argument to the `OP_IF` and `OP_NOTIF`
opcodes is no longer required to be exactly 1 (the one-byte vector with value 1)
to be evaluated as TRUE. Similarly, the input argument to the `OP_IF` and
`OP_NOTIF` opcodes is no longer required to be exactly 0 (the empty vector) to
be evaluated as FALSE.
*   **Clean Stack Policy**: The requirement that the stack has only a single
element on it on completion of the execution of a script.
*   **Data Only in Unlocking Scripts Requirement**: The requirement that unlocking
scripts only include data and associated pushdata op codes. Note that the
script-code that forms part of the hash pre-image for signature verification by
an `OP_CHECKSIG` or `OP_CHECKMULTISIG` within an unlocking script is defined
to be everything from the opcode after the last `OP_CODESEPARATOR` (or the start
of the script if there has not been an `OP_CODESEPARATOR`) to the end of the
unlocking script, plus the entire concatenated locking script.

### Script OpCodes

The following opcodes are restored:  

*   **OP\_VER** – pushes the transaction version (4 byte array) onto the top of the stack.
*   **OP\_VERIF, OP\_VERNOTIF** – provides conditional logic based on the transaction version.
*   **OP\_SUBSTR** – creates an arbitrary substring of the string on top of the stack.
*   **OP\_LEFT, OP\_RIGHT –** creates the left/right most substring of specific length from the string on the top of the stack respectively.
*   **OP\_2MUL** – doubles the number on top of the stack.
*   **OP\_2DIV** – halves the number on top of the stack.

The following new opcodes are introduced:

*   **OP\_LSHIFTNUM** - numerically left shifts a value by the specified number of bits.
*   **OP\_RSHIFTNUM** - numerically right shifts a value by the specified number of bits.

### Chronicle Protocol Specification

Please refer to the [Chronicle Protocol Restoration](https://github.com/bitcoin-sv-specs/protocol/blob/master/updates/chronicle-spec.md)
specification for further details on all of the above.  


## Performance Improvements

### Memory Usage

TCMalloc is now the default memory allocator used when building bitcoind. In
testing, this change results in peak memory usage of the node dropping by nearly
a half.

### Initial Block Download (IBD)

By tuning some existing leveldb configuration parameters, introducing some new
configuration options and optimising the code, the time taken to perform a
complete IBD has been reduced. See the section below for details on the new
config options.


## Config Variables

### Mempool Synchronisation

A new feature has been introduced to allow nodes to be configured to periodically
update their mempools with missing transactions from other specified peers. Note
that peers that wish to synchronise mempools must be configured symmetrically, i.e.
both peers must be configured to sync' with each other.

The options to configure this are:

*   **mempoolsyncpeer** - Specify IPs or subnets of peers with which we want to
periodically synchronise mempools. Can be used more than once to specify
multiple peers / networks.
*   **mempoolsyncage** - Only transactions older than this age will be included
in synchronisation.
*   **mempoolsyncperiod** - How often synchronisation takes place.

### Other New Options

*   **preferredblockfilesize** - Preferred size (in bytes) of a single block datafile.
*   **maxcoinsdbfilesize** - Set maximum file size used by the coins database.
*   **maxconnections** - Maintain at most the given number of connections to peers.
*   **maxchroniclegracefulperiod** - The number of blocks either side of the Chronicle
activation height that we will give peers some grace if they are not yet fully
activated before banning them.

### Removed Options

*   **maxsendbuffermult**

### Changes to Existing Defaults

*   **dbcache** - The default database cache size is now 2GB.


## Other Items

*   STN Reset - includes an updated chain height block hash.
*   Various minor bug fixes, intermittent test failure fixes and code quality improvements.
*   Update to version 0.5.1 of libsecp256k1.
