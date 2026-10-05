# Bitcoin SV Node software – v1.2.2 Release

Overview
========

The 1.2.2 node release is an optional upgrade from version 1.2.1 that
contains several improvements and bug fixes.

Important fixes and improvements in 1.2.2
-----------------------------------------

* Relaxed and corrected the IsStandard transaction checks post-Chronicle,
  including the transaction version range and the push-only check on the
  scriptSig.
* Added a new disk-backed P2P message parser. Messages above the configured
  memory limit are now streamed to a temporary file on disk instead of being
  held entirely in memory.

Other items
-----------

* Dead code removal.
* General minor bug fixes, code-quality improvements and hardening.

New or modified configuration options
-------------------------------------

* **-acceptnonstdtxn** is now accepted on all chains, including mainnet
  (previously restricted to testnet and regtest).
* **-maxreceivebuffer** additionally controls the in-memory size limit used
  by the new disk-backed P2P message parser; P2P messages larger than this
  limit are streamed through a temporary file on disk.

