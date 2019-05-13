Bitcoin SV
=====================

Setup
---------------------
Bitcoin SV is an implementation of a node for the Bitcoin SV network and is one of the pieces of software that provide 
the backbone of the network. It downloads and stores the entire history of Bitcoin SV transactions (which is currently 
several GBs); depending on the speed of your computer and network connection, the synchronization process can take 
anywhere from a few hours to a day or more.

To download Bitcoin SV, visit [bitcoinsv.io](https://bitcoinsv.io/).

Running
---------------------
Bitcoin SV is only supported on the Unix platform at this time.

To run Bitcoin SV, unpack the files into a directory and run:

- `bin/bitcoind`

### Need Help?

* Log an issue on [GitHub] (https://github.com/bitcoin-sv/bitcoin-sv/issues)
* Ask for help on the [Bitcoin SV Subreddit](https://www.reddit.com/r/bitcoinSV/) or
[Bictoin Cash SV Subreddit](https://www.reddit.com/r/bitcoincashSV/).

Building
---------------------
The following are developer notes on how to build Bitcoin. They are not complete guides, but include notes on the 
necessary libraries, compile flags, etc.

- [Unix Build Notes](build-unix.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Bitcoin SV repo's [root README](/README.md) contains relevant information on the development process and automated 
testing.

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Benchmarking](benchmarking.md)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distribution is done under the [Open BSV License](/LICENSE). This product includes software developed by the OpenSSL 
Project for use in the [OpenSSL Toolkit](https://www.openssl.org/), cryptographic software written by Eric Young 
([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
