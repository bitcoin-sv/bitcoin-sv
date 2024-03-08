# Bitcoin SV Node software – v1.0.15 Release

Overview
========

The 1.0.15 node release is a recommended upgrade from version 1.0.14.

Headline Items in 1.0.15
========================

1. Memory usage optimisations.
2. Build updates:
    1. C++ v20.
    2. Ubuntu 20.04 LTS.
    3. CentOS 9.
3. STN Network reset.

Detailed features of 1.0.15
===========================

Memory Usage Optimisations
--------------------------

In this release we have updated the software to reduce peak memory consumption during the receipt of large p2p messages - specifically **block**, **blocktxn** and **cmpctblock** messages.

Build Updates
-------------

In this release we have updated the tooling used to build the node software and the target operating systems supported by the pre-built node binaries. This has the following main consequences:

### Pre-built binaries

The pre-built binaries will run on the following supported platforms:

* **Ubuntu 20.04 LTS** (or later).
* **Debian 11** bullseye (or later).
* **Centos 9** (or later).

Running the node on different or older versions of Linux is still possible but will require manually building from source.

This release has also been updated to use the latest upstream version **0.3.1** of the **secp256k1** library.

### Building from source

Building the node software from source requires at least version **10.2** of the **GCC compiler** and at least version **1.74** of the **Boost libraries**.

See the instructions in [build-unix.md](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/build-unix.md) for a detailed description on how to build from source, or alternatively see the provided docker images [here](https://hub.docker.com/r/bitcoinsv/bitcoin-sv-src/) of suitable build environments already setup (scripts to generate the docker images can be found [here](https://github.com/bitcoin-sv/docker-sv-src)).

Other items:
------------

* The Scaling Test Network (**STN**) has been reset at block height eight. This block has hash **0000000074230d332b2ed9d87af3ad817b6f2616c154372311c9b2e4f386c24c**.

Other new or modified configuration options
-------------------------------------------

* \-_**maxconnections**_ has been removed.
* \-_**maxoutboundconnections**_ has been added (default=8).

