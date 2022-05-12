Overview
--------

Version 1.0.11 hotfix release is a recommended upgrade from version 1.0.11. This new version contains some bug fixes.

Bug fixes
---------

1) Fix behaviour of the *sendrawtransaction* and *sendrawtransactions* RPC functions, so that transactions already in the transaction validation queue or orphan pool are re-processed.

2) Optimisation to speedup transaction serialisation.
