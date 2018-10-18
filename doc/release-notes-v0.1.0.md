# Bitcoin SV version 0.1.0 Release Notes

This release includes support for the November 2018 Upgrade. When the upgrade activates, the 
following changes take effect:
 - The Satoshi opcodes OP_MUL, OP_INVERT, OP_LSHIFT, & OP_RSHIFT are re-enabled
 - The limit on the number of opcodes per script is increased to 500 (up from 201)
 - The default maximum size of accepted blocks = 128MB
 
The following features are also included in this release:
 - Disabled the automatic replay protection feature

## Known Issues
This release has the following known issues:

* SV-30 - if excessiveblocksize has not been manually configured, the following fields report the
post-upgrade value (128MB) before the November 2018 upgrade has taken place
  * maxblocksize field in the results from RPC getinfo
  * excessiveBlockSize field in the results from RPC getexcessiveblock
  * EB field in the P2P useragent string

## List of Changes
* November 2018 Upgrade activation logic
* Re-enabled Satoshi opcodes OP_MUL, OP_INVERT, OP_LSHIFT, & OP_RSHIFT (takes effect after Nov 2018 upgrade)
* Increased limit on the number of opcodes per script to 500 (takes effect after Nov 2018 upgrade)
* Increased default size of accepted blocks to 128MB  (takes effect after Nov 2018 upgrade)
* Reduced the maximum size of P2P messages to be closer to the `excessiveblocksize`
* Removed the Automatic Replay Protection feature
* Removed activation logic for the May 2018 upgrade
* Removed the GUI
* Made the `excessiveblocksize` a standard parameter (was debug)
* Added `excessiveblocksize` and `maxblocksize` to RPC getinfo
* Fix for CVE-2018-17144

## List of Tests
* Unit Tests
* Python functional tests
* System tests:
  * Validate that the default excessive block size does not change prior to the hardfork
  * Validate that the excessive block size setting is defaulted to 128MB after hard fork
  * Validate that the excessive block size setting remains 128MB after hard fork
  * Validate the excessive block size setting is unchanged after hard fork
  * Validate the max generated block size is defaulted to 32MB prior to hard fork
  * Validate the max generated block size is defaulted to 32MB after the hard fork
  * Validate that scripts with >201 op codes are rejected before magnetic activation
  * Validate that scripts with >201 op codes are handled after magnetic activation
  * Validate that scripts with >500 op codes are rejected after magnetic activation
  * Validate rejection of all 4 Opcodes (OP_MUL, OP_LSHIFT, OP_RSHIFT, OP_INVERT) prior to magnetic activation
  * Validate that an infinite block attack is declined and the node disconnected

All tests passed.
