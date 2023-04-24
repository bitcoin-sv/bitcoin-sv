# UNIX BUILD NOTES

## Building using a pre-built docker image

Docker images containing all the required tools, build dependencies and
Bitcoin SV node source code are available [here](https://hub.docker.com/r/bitcoinsv/bitcoin-sv-src/).

Pre-built images are available for Debian, Ubuntu and Centos.

These docker images can be used to build the node software without needing
to worry about installing all the required dependencies as detailed below.

## Manual building notes

Some notes on how to build Bitcoin SV in Unix.

Always use absolute paths to configure and compile bitcoin and the dependencies,
for example, when specifying the path of the dependency:

	../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX

Here BDB_PREFIX must be an absolute path - it is defined using $(pwd) which ensures
the usage of the absolute path.

## To Build

```bash
./autogen.sh
./configure
make
make install # optional
```

## Dependencies

These dependencies are required:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 libssl      | Crypto           | Random Number Generation, Elliptic Curve Cryptography
 libboost    | Utility          | Library for threading, data structures, etc
 libevent    | Networking       | OS independent asynchronous networking

Optional dependencies:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 miniupnpc   | UPnP Support     | Firewall-jumping support
 libdb       | Berkeley DB      | Wallet storage (only needed when wallet enabled)
 univalue    | Utility          | JSON parsing and encoding (bundled version will be used unless --with-system-univalue passed to configure)
 libzmq3     | ZMQ notification | Optional, allows generating ZMQ notifications (requires ZMQ version >= 4.3.2)
 tcmalloc    | Memory allocator | Alternative memory allocator (may be helpful for nodes on the STN)

### Toolchain dependency build instructions

#### GCC version

A sufficiently recent version of GCC that supports C++20 is required for
building, which in practise means at least version 10.2 or above. On the
current stable release of Debian, Ubuntu 20+ and Centos 9 a suitable version
should either be installed by default or available in the standard
package repositories. If however you are running an older release you
may have to look outside the standard repositories for a suitable version
of GCC.

For example; to install a suitable version of GCC on Ubuntu 18:

```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install g++-10
```

After installing a newer version of GCC you may still need to update your
system so that the correct version is found when you run g++. For example;
using the *update-alternatives* command on Debian/Ubuntu or *scl enable* on
Centos.


### Dependency Build Instructions

#### Debian & Ubuntu

##### Build requirements

```bash
sudo apt-get install build-essential libtool autotools-dev automake python3 pkg-config libssl-dev libevent-dev bsdmainutils
```

##### BerkeleyDB

BerkeleyDB 5.3 or later is required for the wallet. This can be installed with:

```bash
sudo apt-get install libdb-dev libdb++-dev
```

See the section "Disable-wallet mode" to build Bitcoin SV without wallet.

##### Boost libraries

On Debian stable and Ubuntu 22 suitable versions of the boost libraries are
available in the main package repositories and the following instructions
apply:

1. There are generic names for the individual boost development packages, so
the following can be used to only install necessary parts of boost:

```bash
sudo apt-get install libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev
```

2. If that doesn't work, you can install all boost development packages with:

```bash
sudo apt-get install libboost-all-dev
```

On older versions of Debian and Ubuntu you will need to download a more recent
version of boost yourself than is available by default. See the section
[below](#manually-installing-boost-libraries) for instructions.

##### Other dependencies

Optional (see --with-miniupnpc and --enable-upnp-default):

```bash
sudo apt-get install libminiupnpc-dev
```

Optional (see --enable-tcmalloc):

```bash
sudo apt-get install libgoogle-perftools-dev
```

ZMQ dependencies (provides ZMQ API 4.x):

```bash
sudo apt-get install libzmq3-dev
```

Functional tests:
```bash
sudo apt-get install python3-ecdsa python3-zmq python3-bip32utils
```

#### RHEL / Centos

Enable powertools repository:

RHEL 9 / Centos 9:        
```bash
sudo dnf config-manager --set-enabled crb
sudo dnf install epel-release epel-next-release
```

RHEL 8 / Centos 8:
```bash
sudo dnf config-manager --set-enabled powertools
sudo dnf install epel-release epel-next-release
```

##### Build requirements:

RHEL 9 / Centos 9:
```bash
sudo dnf install gcc-c++ libtool make autoconf automake python3 openssl-devel libevent-devel libdb-devel libdb-cxx-devel
```

RHEL 8 / Centos 8:
```bash
sudo dnf install gcc-toolset-11 libtool make autoconf automake python3 openssl-devel libevent-devel libdb-devel libdb-cxx-devel
scl enable gcc-toolset-11 bash
```

##### Boost libraries

On RHEL 9 / Centos 9 suitable versions of the boost libraries are avaialable
in the standard package repositories and can be installed with:

```bash
sudo dnf install boost-devel
```

On RHEL 8 / Centos 8

On RHEL 8 / Centos 8 you will need to download a more recent version of boost
yourself than is available by default. See the section
[below](#manually-installing-boost-libraries) for instructions.

##### Other dependencies

Optional (see --with-miniupnpc and --enable-upnp-default):
```bash
sudo dnf install miniupnpc-devel
```

ZMQ dependencies (provides ZMQ API 4.x):
```bash
sudo dnf install cppzmq-devel
```

Functional tests:
```bash
sudo dnf install python3-ecdsa
```

## Notes

### Stripping the binaries

The release is built with GCC and then "strip bitcoind" to strip the debug
symbols, which reduces the executable size by about 90%.

### Manually installing boost libraries

Version 1.74 or above of the boost libraries are required to build Bitcoin SV.
The latest version can always be obtained from here:
[boost latest download](https://www.boost.org/users/download/). 

Once you have downloaded a suitable version of boost and extracted the
contents of the archive do the following steps to install system-wide:

```bash
cd <directory containing boost files>
./bootstrap.sh --prefix=/usr/local
sudo ./b2 install
sudo echo '/usr/local/lib' > /etc/ld.so.conf.d/local.conf
sudo ldconfig
```

**NOTE:** If you now have multiple versions of boost installed on your build
system (for example; if you have an old version installed from a package and
another version installed manually under */usr/local* as instructed above) you
may encounter errors when trying to build bitcoind if the linker first
encounters the older, incorrect version of the boost libraries. To solve this,
you must either uninstall the old boost packages, or explicitly tell the bitcoin
configure script where to look for the correct new version of boost:

```bash
./configure --with-boost=/usr/local
```

### miniupnpc

[miniupnpc](http://miniupnp.free.fr/) may be used for UPnP port mapping. It can be downloaded from [here](
http://miniupnp.tuxfamily.org/files/). UPnP support is compiled in and
turned off by default. See the configure options for upnp behavior desired:

	--without-miniupnpc      No UPnP support miniupnp not required
	--disable-upnp-default   (the default) UPnP support turned off by default at runtime
	--enable-upnp-default    UPnP support turned on by default at runtime

### Memory allocators

If you see memory usage blow up over time from your bitcoind node (particularly on the STN) you
may find it useful to try swapping out the default C++ memory allocator for Google tcmalloc.
See the configure options for specifying the allocator to use:

    --enable-tcmalloc       Link with the Google tcmalloc library

### Security

To help make your bitcoin installation more secure by making certain attacks impossible to
exploit even if a vulnerability is found, binaries are hardened by default.
This can be disabled with:

Hardening Flags:

	./configure --enable-hardening
	./configure --disable-hardening

Hardening enables the following features:

* Position Independent Executable
    Build position independent code to take advantage of Address Space Layout Randomization
    offered by some kernels. Attackers who can cause execution of code at an arbitrary memory
    location are thwarted if they don't know where anything useful is located.
    The stack and heap are randomly located by default but this allows the code section to be
    randomly located as well.

    On an AMD64 processor where a library was not compiled with -fPIC, this will cause an error
    such as: "relocation R_X86_64_32 against `......' can not be used when making a shared object;"

    To test that you have built PIE executable, install scanelf, part of paxutils, and use:

    	scanelf -e ./bitcoin

    The output should contain:

     TYPE
    ET_DYN

* Non-executable Stack
    If the stack is executable then trivial stack based buffer overflow exploits are possible if
    vulnerable buffers are found. By default, bitcoin should be built with a non-executable stack
    but if one of the libraries it uses asks for an executable stack or someone makes a mistake
    and uses a compiler extension which requires an executable stack, it will silently build an
    executable without the non-executable stack protection.

    To verify that the stack is non-executable after compiling use:
    `scanelf -e ./bitcoin`

    the output should contain:
	STK/REL/PTL
	RW- R-- RW-

    The STK RW- means that the stack is readable and writeable but not executable.

### Disable-wallet mode

When the intention is to run only a P2P node without a wallet, bitcoin may be compiled in
disable-wallet mode with:

    ./configure --disable-wallet

Mining is also possible in disable-wallet mode, but only using the `getblocktemplate` RPC
call not `getwork`.

### Additional Configure Flags

A list of additional configure flags can be displayed with:

    ./configure --help

