# Bitcoin SV Node software – v1.0.14 Release

Overview
========

The 1.0.14 node release is a recommended upgrade from version 1.0.13.

Headline Item in 1.0.14
-----------------------

1.  Node Freezing issue resolution (increased responsiveness)

Detailed features of 1.0.14
---------------------------

*   Parallel transaction validation (PTV) during block validation.

Other items:

*   Fixes to unit / functional tests.
*   A fix to reduce the memory used by the node when responding to **getblocktxn** requests and to better track the memory usage across P2P message queues.
*   An improvement to how the node handles prioritising an existing transaction via the **sendrawtransaction** RPC which may have unnecessarily resulted in the next block template returned from the journaling block assembler containing fewer transactions than it could.
*   The log message if the node runs out of disk space has been extended to give more information.
*   Some build warnings and failures on newer platforms and compilers have been fixed.
*   Bump OpenSSL version used by the Gitian build to 1.1.1t.
*   Make the maximum number of P2P **addnode** peers configurable.
*   Optimise locking code related to **rollingMinimumFeeRate**.

Community contribution submitted to Github:

*   RPC REST requests to fetch a binary block now have basic HTTP range header request support. For example; **Range: bytes=1024-2048**
*   Log message clarification around free RAM

PTV during block validation
---------------------------

This change allows the node to fully take advantage of all available cores during block validation. Tests on 8, 16 and 32 core machines have shown it reduces block validation times, makes the node more responsive, and improves scalability. It also helps slightly reduce initial block download times.

Most users will not need to do anything to do anything to take full advantage of this change, however the following configuration options have been added to control the behaviour:

*   **\-blockvalidationtxbatchsize**: Sets the minimum batch size for groups of transactions to be validated in parallel. For example; if this is set to 100 on an 8 core machine, and an incoming block contains 500 transactions then those transactions will be validated in 5 parallel batches of 100. A 1000 transaction block would be validated in 8 parallel batches of 125.
*   **\-txnthreadsperblock**: Sets the number of transaction validation threads used to validate a single bock. Defaults to the number of cores available on the machine, but can be raised or lowered if required.

Other new or modified configuration options
-------------------------------------------

An addition to any previously mentioned changes, the following node configuration options have either been introduced or changed:

*   **\-blockdownloadlowerwindow**: A new option to further limit the block download window size during IBD in order to help the node hit any configured pruning target. If pruning is not enabled then this will default to the same as the standard block download window size. An operator may choose to reduce this value even if pruning is not enabled which will result in the node using less disk space during IBD but at the possible cost of a longer IBD time. Conversely, an operator of a pruned node may choose to increase this value to reduce the time it takes to perform IBD but at the possible cost of exceeding the pruning target at times.
*   **\-pruneminblockstokeep**: An advanced option to override the minimum number of last blocks a node will keep on disk. Normally this should not be changed, but this option allows an operator to temporarily reduce this from the default value of 288 blocks in combination with the **\-prune** option if they need to really restrict the amount of space taken up by block data.
*   **\-maxaddnodeconnections**: A new option to set the maximum number of peers that can be connected to with the **addnode** configuration option or RPC command. This limit was previously hard-coded to 8.
