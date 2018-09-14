Mac OS X Build Instructions and Notes
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Preparation
-----------

1.  Install Xcode from the app store

2.  Install the OS X command line tools:

`xcode-select --install`

When the popup appears, click `Install`.

3.  Install [Homebrew](http://brew.sh).

Dependencies
----------------------

Install dependencies:

    brew install automake berkeley-db libtool boost --c++11 miniupnpc openssl pkg-config --c++11 libevent

In case you want to build the disk image with `make deploy` (.dmg / optional), you need RSVG

    brew install librsvg

Build Bitcoin SV
-----------------

1. Clone the Bitcoin SV source code and cd into `bitcoin-sv`

        git clone github.com/bitcoin-sv/bitcoin-sv
        cd bitcoin-sv

2.  Build bitcoin-sv:

    Configure and build the headless bitcoin binaries

        ./autogen.sh
        ./configure
        make

3.  It is recommended to build and run the unit tests:

        make check

4.  You can also create a .dmg that contains the .app bundle (optional):

        make deploy

Running
-------

Bitcoin SV is now available at `./src/bitcoind`

Before running, it's recommended you create an RPC configuration file.

    echo -e "rpcuser=bitcoinrpc\nrpcpassword=$(xxd -l 16 -p /dev/urandom)" > "/Users/${USER}/Library/Application Support/Bitcoin/bitcoin.conf"

    chmod 600 "/Users/${USER}/Library/Application Support/Bitcoin/bitcoin.conf"

The first time you run bitcoind, it will start downloading the blockchain. This process could take several hours.

You can monitor the download process by looking at the debug.log file:

    tail -f $HOME/Library/Application\ Support/Bitcoin/debug.log

Other commands:
-------

    ./src/bitcoind -daemon # Starts the bitcoin daemon.
    ./src/bitcoin-cli --help # Outputs a list of command-line options.
    ./src/bitcoin-cli help # Outputs a list of RPC commands when the daemon is running.
