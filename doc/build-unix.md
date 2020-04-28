UNIX BUILD NOTES
====================
Some notes on how to build Bitcoin SV in Unix.

Note
---------------------
Always use absolute paths to configure and compile bitcoin and the dependencies,
for example, when specifying the path of the dependency:

	../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX

Here BDB_PREFIX must be an absolute path - it is defined using $(pwd) which ensures
the usage of the absolute path.

To Build
---------------------

```bash
./autogen.sh
./configure
make
make install # optional
```

Dependencies
---------------------

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
 libzmq3     | ZMQ notification | Optional, allows generating ZMQ notifications (requires ZMQ version >= 4.x)
 tcmalloc    | Memory allocator | Alternative memory allocator (may be helpful for nodes on the STN)

For the versions used in the release, see [release-process.md](release-process.md) under *Fetch and build inputs*.

Memory Requirements
--------------------

C++ compilers are memory-hungry. It is recommended to have at least 1.5 GB of
memory available when compiling Bitcoin SV. On systems with less, gcc can be
tuned to conserve memory with additional CXXFLAGS:


    ./configure CXXFLAGS="--param ggc-min-expand=1 --param ggc-min-heapsize=32768"

Dependency Build Instructions: Ubuntu & Debian
----------------------------------------------
Build requirements:

    sudo apt-get install build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils

GCC version:

A sufficiently recent version of GCC that supports C++17 is required for
building, which in practise means at least version 7.X or above. On recent
testing versions of Debian & Ubuntu 18.04+ a suitable version should either
be installed by default or available in the standard package repositories.
But if you are running an older release you may have to install a newer
version of GCC from another repository.

For instructions on how to install a more recent version of GCC into an
older Ubuntu LTS see here: https://gist.github.com/application2000/73fd6f4bf1be6600a2cf9f56315a2d91 

Options when installing required Boost library files:

1. On at least Ubuntu 14.04+ and Debian 7+ there are generic names for the
individual boost development packages, so the following can be used to only
install necessary parts of boost:

        sudo apt-get install libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev

2. If that doesn't work, you can install all boost development packages with:

        sudo apt-get install libboost-all-dev

BerkeleyDB 5.3 or later is required for the wallet. This can be installed with:

        sudo apt-get install libdb-dev
        sudo apt-get install libdb++-dev

See the section "Disable-wallet mode" to build Bitcoin SV without wallet.

Optional (see --with-miniupnpc and --enable-upnp-default):

    sudo apt-get install libminiupnpc-dev

Optional (see --enable-tcmalloc):

    sudo apt-get install libgoogle-perftools-dev

ZMQ dependencies (provides ZMQ API 4.x):

    sudo apt-get install libzmq3-dev

Dependency Build Instructions: Fedora/Centos
--------------------------------------------
Build requirements:

    sudo dnf install gcc-c++ libtool make autoconf automake openssl-devel libevent-devel boost-devel libdb-devel libdb-cxx-devel

Optional:

    sudo dnf install miniupnpc-devel

GCC version:

A sufficiently recent version of GCC that supports C++17 is required for
building, which in practise means at least version 7.X or above.

Notes
-----
The release is built with GCC and then "strip bitcoind" to strip the debug
symbols, which reduces the executable size by about 90%.


miniupnpc
---------

[miniupnpc](http://miniupnp.free.fr/) may be used for UPnP port mapping.  It can be downloaded from [here](
http://miniupnp.tuxfamily.org/files/).  UPnP support is compiled in and
turned off by default.  See the configure options for upnp behavior desired:

	--without-miniupnpc      No UPnP support miniupnp not required
	--disable-upnp-default   (the default) UPnP support turned off by default at runtime
	--enable-upnp-default    UPnP support turned on by default at runtime

Memory allocators
-----------------

If you see memory usage blow up over time from your bitcoind node (particularly on the STN) you
may find it useful to try swapping out the default C++ memory allocator for Google tcmalloc.
See the configure options for specifying the allocator to use:

    --enable-tcmalloc       Link with the Google tcmalloc library

Boost
-----
For documentation on building Boost look at their official documentation: http://www.boost.org/build/doc/html/bbv2/installation.html

Security
--------
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

Disable-wallet mode
--------------------
When the intention is to run only a P2P node without a wallet, bitcoin may be compiled in
disable-wallet mode with:

    ./configure --disable-wallet

Mining is also possible in disable-wallet mode, but only using the `getblocktemplate` RPC
call not `getwork`.

Additional Configure Flags
--------------------------
A list of additional configure flags can be displayed with:

    ./configure --help


Setup and Build Example: Arch Linux
-----------------------------------
This example lists the steps necessary to setup and build a command line only, non-wallet distribution of the latest changes on Arch Linux:

    pacman -S git base-devel boost libevent python
    git clone https://github.com/bitcoin-sv/bitcoin-sv
    cd bitcoin-sv/
    ./autogen.sh
    ./configure --disable-wallet --without-miniupnpc
    make check


ARM Cross-compilation
-------------------
These steps can be performed on, for example, an Ubuntu VM. The depends system
will also work on other Linux distributions, however the commands for
installing the toolchain will be different.

Make sure you install the build requirements mentioned above.
Then, install the toolchain and curl:

    sudo apt-get install g++-arm-linux-gnueabihf curl

To build executables for ARM:

    cd depends
    make HOST=arm-linux-gnueabihf
    cd ..
    ./configure --prefix=$PWD/depends/arm-linux-gnueabihf --enable-glibc-back-compat --enable-reduce-exports LDFLAGS=-static-libstdc++
    make


For further documentation on the depends system see [README.md](../depends/README.md) in the depends directory.


Building on Ubuntu 16.04
--------------------

Install build tools

    sudo apt-get install build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils -y
    sudo apt-get install libdb++-dev -y
    sudo apt-get install git -y


Upgrade GCC to 7.4
Note: this will upgrade your system version of gcc. See instructions at the bottom how to revert back to original version.

    sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
    sudo apt-get update
    sudo apt-get install g++-7 -y
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 60 \
                             --slave /usr/bin/g++ g++ /usr/bin/g++-7
    sudo update-alternatives --config gcc


Compile and install libboost

    sudo apt-get update
    sudo apt-get install build-essential g++ python-dev autotools-dev libicu-dev libbz2-dev -y
    mkdir boost
    cd boost
    wget https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz
    tar -xzvf boost_1_70_0.tar.gz
    cd boost_1_70_0
    ./bootstrap.sh
    ./b2
    sudo ./b2 install
    cd ../../


Clone bitcoin-sv repo

    git clone https://github.com/bitcoin-sv/bitcoin-sv


Build bitcoin-sv

    cd bitcoin-sv
    ./autogen.sh
    mkdir build
    cd build
    ../configure
    make

Revert gcc to original version (optional)

    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-5* 60 \
                             --slave /usr/bin/g++ g++ /usr/bin/g++-5*
    sudo update-alternatives --config gcc

Building on Centos 7
--------------------

Note: to enable gcc-7 run the command `scl enable devtoolset-7 bash`. 
RUN IT MANUALY because it doesn't work in a script!!!

Note 2: When running bitcoind, it expects boost libreries in /usr/lib64 folder. Make sure that they are located there. See libbost instalation step for details.


Install prerequisites

    sudo yum install gcc-c++ libtool make autoconf automake openssl-devel libevent-devel  libdb-devel libdb-cxx-devel -y
    sudo yum install python3 -y
    sudo yum install git -y
    sudo yum install centos-release-scl -y
    sudo yum install devtoolset-7-gcc* -y
    sudo scl enable devtoolset-7 bash &


Compile and install libboost

    mkdir boost
    cd boost
    wget https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.gz
    tar -xzvf boost_1_70_0.tar.gz
    cd boost_1_70_0
    ./bootstrap.sh
    ./b2
    sudo ./b2 install --prefix=/opt/boost_1_70
    sudo echo "/opt/boost_1_70/lib" > /etc/ld.so.conf.d/boost_1_70.conf
    sudo ldconfig

    cd ../../


Clone bitcoin-sv repo

    git clone https://github.com/bitcoin-sv/bitcoin-sv


Build bitcoin-sv

    cd bitcoin-sv
    ./autogen.sh
    mkdir build
    cd build
    scl enable devtoolset-7 bash 
    ../configure --with-boost=/opt/boost_1_70
    make


Building on FreeBSD
--------------------

(Updated as of FreeBSD 11.0)

Clang is installed by default as `cc` compiler. Installing dependencies:

    pkg install autoconf automake libtool pkgconf
    pkg install boost-libs openssl libevent gmake

(`libressl` instead of `openssl` will also work)

For the wallet (optional):

    pkg install db5

This will give a warning "configure: WARNING: Found Berkeley DB other
than 4.8; wallets opened by this build will not be portable!", but as FreeBSD never
had a binary release, this may not matter. If backwards compatibility
with 4.8-built Bitcoin Core is needed follow the steps under "Berkeley DB" above.

Also, if you intend to run the regression tests (qa tests):

    pkg install python3

Then build using:

    ./autogen.sh
  
With wallet support:

    ./configure --without-miniupnpc --with-incompatible-bdb BDB_CFLAGS="-I/usr/local/include/db5" BDB_LIBS="-L/usr/local/lib -ldb_cxx-5"

Without wallet support:

    ./configure --without-miniupnpc --disable-wallet

Then to compile:

    gmake

*Note on debugging*: The version of `gdb` installed by default is [ancient and considered harmful](https://wiki.freebsd.org/GdbRetirement).
It is not suitable for debugging a multi-threaded C++ program, not even for getting backtraces. Please install the package `gdb` and
use the versioned gdb command e.g. `gdb7111`.
