# Bitcoin SV Node software – Upgrade to v1.0.11 Release

Overview
--------

Version 1.0.11 release is a recommended upgrade from version 1.0.10. This new version introduces changes to fee configuration options and various bug fixes.

Changed option name -blockmintxfee to -minminingtxfee
-----------------------------------------------------

Note that setting -minminingtxfee is a mandatory setting. The option -blockmintxfee has been removed. Setting it will emit a warning in the bitcoin logs. To repeat the default setting from the last release a miner needs to configure -_minminingtxfee=0.00000500_ (.i.e 500 Satoshis).

Removed options -dustrelayfee, -dustlimitfactor
-----------------------------------------------

These options have been removed. The dust threshold is now hard coded to one Satoshi per transaction output.

Note that blocks containing transactions with dust outputs (i.e. outputs with a zero Satoshi amount)  will still be accepted.

Attempting to set these options will have no effect other than a warning message in the bitcoin logs.

Removed option -minrelaytxfee
-----------------------------

This option has been removed. The mempool will reject incoming low fee transactions based on its dynamic rejection fee as in the original implementation of bitcoin.

Attempting to set this option will have no effect other than a warning message in the bitcoin logs.

Bug fixes
---------

1) Node stops responding to RPCs, including getminingcandidate

2) Dropped connections due to "Timeout downloading block"

3) Pruning target is now matched as closely as possible during initial block download

4) Disable a Bloom filtered connection by default
