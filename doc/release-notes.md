# Bitcoin SV Node Software – v1.2.0 Release

This release is a hard fork which includes changes to the BSV protocol.   

The scheduled TestNet activation height is 1,621,670 (Target is 12:00 midday 31-Oct-2024)

The scheduled MainNet activation height is 882,687 (Target is 12:00 midday 4-Feb-2025)

## What’s Changed  

### Chronicle Protocol Updates

#### Script OpCodes

The following opcodes are restored:  

*   **OP\_VER** – pushes the transaction version (4 byte array) onto the top of the stack.
*   **OP\_VERIF, OP\_VERNOTIF** – provides conditional logic based on the transaction version.
*   **OP\_SUBSTR** – creates an arbitrary substring of the string on top of the stack.
*   **OP\_LEFT, OP\_RIGHT –** creates the left/right most substring of specific length from the string on the top of the stack respectively.
*   **OP\_2MUL** – doubles the number on top of the stack
*   **OP\_2DIV** – halves the number on top of the stack

Please refer to the Protocol Restoration specification for further details.  

#### Sighash Flag Changes

A new optional sighash flag **SIGHASH_RELAX** is introduced (value **0x20**), and the
existing **SIGHASH_FORKID** flag becomes optional.

If the FORKID flag is omitted, then transactions signed that way will use the
original Bitcoin transaction digest algorithm and relaxed malleability rules.
If the FORKID flag is still set _AND_ the RELAX flag is also set, then a
transaction signed that way will continue to use the new transaction digest
algorithm but can take advantage of the relaxed malleability rules (as detailed
below).

Transactions signed with just the FORKID flag, as historically required, will see
no changes to their consensus rules.

#### Transaction Limitations Relaxed

The limitations in the following areas are removed.   

*   **Script numbers** – Limits on the maximum size of script numbers are removed.
Please note that practical limits are imposed by the external libraries used to
implement script numbers (max size is currently 64MB).
*   **Minimal Encoding Requirement** – Previously releases required transactions
to encode numbers as efficiently as possible. For example, the number two can be
encoded as 0002 (2 bytes) or 02 (1 byte). Prior to this release only the second
version is accepted. Minimal encoding places an unnecessary burden on users that
was not present in the original Satoshi implementation and it is removed from this
release for transactions signed with the relaxed sighash flags.
*   **Low S signatures -** If S is a signature, then so is -S. The “low S” signature
requirement was introduced to decrease transaction malleability but is an unnecessary
requirement and is removed. Either signature S or -S can now be used in transactions
signed with the relaxed sighash flags.
*   **Clean Stack Policy** – Previous releases required that after the execution
of the unlocking and locking script, the stack is “clean”. i.e. there is only a
single item (interpreted as true/false) on the stack that indicates whether the
transaction has permission to spend the relevant input. The requirement is
unnecessary and adds complexity to scripts. This release removes the clean stack
requirement for transactions signed with the relaxed sighash flags.
*   **No opcodes in Unlocking Scripts Policy** - Previous releases do not allow
unlocking scripts to include non-data opcodes. That restriction is removed in
this release for transactions signed with the relaxed sighash flags.

### Performance Improvements

#### Memory Usage

TCMalloc is now the default memory allocator used when building bitcoind. In
testing, this change results in peak memory usage of the node dropping by nearly
a half.

#### Initial Block Download (IBD)

By tuning some existing leveldb configuration parameters, introducing some new
configuration options and optimising the code, the time taken to perform a
complete IBD has been reduced. See the section below for details on the new
config options.

### Config Variables

#### Mempool Synchonisation

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

#### Other New Options

*   **preferredblockfilesize** - Preferred size (in bytes) of a single block datafile.
*   **maxcoinsdbfilesize** - Set maximum file size used by the coins database.
*   **maxconnections** - Maintain at most the given number of connections to peers.
*   **maxchroniclegracefulperiod** - The number of blocks either side of the Chronicle
activation height that we will give peers some grace if they are not yet fully
activated before banning them.

#### Removed Options

*   **maxsendbuffermult**

#### Changes to Existing Defaults

*   **dbcache** - The default database cache size is now 2GB.
*   **maxscriptnumlengthpolicy** - The default is now 0 (unlimited).

### Other items

*   STN Reset - includes an updated chain height block hash.
*   Various minor bug fixes, intermittent test failure fixes and code quality improvements.
*   Update to version 0.5.1 of libsecp256k1.
