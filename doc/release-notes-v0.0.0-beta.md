Bitcoin SV beta version is now available. 

This release includes the following features and fixes:
 - November 2018 upgrade activation. When the upgrade activates, the 
 following upgrades take effect:
    - The opcodes OP_MUL, OP_INVERT, OP_LSHIFT, & OP_RSHIFT are re-enabled
    - The limit on the number of opcodes per script is increased to 500
    - The default maximum size of accepted blocks = 128MB
 - Disabled the automatic replay protection feature

## Known Issues
This release has the following known issues:

* SV-30 - if excessiveblocksize has not been manually configured, the following fields report the
post-upgrade value (128MB) before the upgrade has taken place
  * maxblocksize field in the results from RPC getinfo
  * excessiveBlockSize field in the results from RPC getexcessiveblock
  * EB field in the P2P useragent string
