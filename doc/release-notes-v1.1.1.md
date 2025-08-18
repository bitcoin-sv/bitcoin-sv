# Bitcoin SV Node Software – v1.1.1 Release

This is an optional minor patch release to the Bitcoin SV Node Software
version 1.1.0. It includes a change to allow node operators to run the
software with both the 'txindex' and 'prune' options enabled.

### Transaction Index with Pruning

The restriction that prevented the use of both 'txindex' and 'prune' options
together has been removed. If you attempt to request the details for a
transaction that is in a block that has been pruned, then the node will simply
return an error indicating that the transaction cannot be found.
