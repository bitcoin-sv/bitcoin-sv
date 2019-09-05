# Sunset of P2SH

The handling of P2SH transactions is an anomaly in the Script language and this special function will be retired in the 
Genesis Upgrade.

As a first stage, there will be a policy change implemented in the Bitcoin SV Node implementation.
This policy change will be to treat transactions that contain P2SH outputs as un-desirable transactions. 
Transactions with P2SH outputs will not be relayed by the Bitcoin SV Node implementation, and they will not be accepted 
into the mempool.

The effect of this policy change will be that transactions containing P2SH outputs will not be propagated or mined by nodes. 

This policy will be configurable by node operators. Node operators will be able to configure their node to accept
P2SH transactions into the mempool, and they will then subsequently be mined into a block if the node is a mining node. 

This is not a consensus change, it is a policy change. Transactions containing P2SH outputs will remain valid if they
are mined into a block. This policy change discourages the use of P2SH but does not entirely prevent it.
 