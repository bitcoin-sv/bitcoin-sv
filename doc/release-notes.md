# Bitcoin SV Node Software – v1.1.0 Release

## Overview

Security enhancements and peer management features for node operators are the main features included in this release.

The 1.1.0 node release is a recommended upgrade from version 1.0.16.

## Security Enhancements

*   Network peer connection management.
    *   Configurable numbers of:
        *   pending p2p message response queue management.
        *   connections from the same IP address.
        *   connections to both inbound and outbound peers.
*   General code security enhancements.

## Security Dependency Updates

*   Updated secp256k1 library version.
*   Updated OpenSSL library version in build system.

## Alert System
Version 1.1.0 reintroduces the Alert System. The Alert System, originally implemented in the v0.3.10 Bitcoin release, enables the BSV Association to send signed messages to the network. Messages can be of an informational or directive nature. This release also contains native support for Digital Asset Recovery alerts.

Node operators are required to run the Alert System in conjunction with the BSV Node. Node operators who do not interact with the Alert System risk being banned and/or having their blocks rejected by node operators who do.

For non-mining businesses that insist on continuing to run the node software, we strongly encourage installation and connection of the Alert System to remain in sync with the valid longest chain. For non-mining businesses that do not want to stand up the Alert System, we recommend modifying the following configuration in your `bitcoin.conf` file:
```bash
enableassumewhitelistedblockdepth=0
assumewhitelistedblockdepth=0
```

Detailed instructions on how to run the Alert System are available at: [node.bitcoinsv.io/sv-node/alert-system](https://node.bitcoinsv.io/sv-node/alert-system)

## Other items

*   STN Reset - includes an updated chain height block hash.
*   Include GitHub workflows configuration file structure.

## Specific Configuration Details

### Inbound and Outbound Connection Management

Max Connections gives node operators flexibility in how to manage peers they interact with on the network.

| **Configuration parameter** | **Default value** | **Description** |
| -------------- | ---------------  | --------------- |
| `maxconnections`                 | 125    |    Maintain at most *n* connections to peers |
| `maxconnectionsfromaddr`         | 0     | Maximum number of inbound connections from a single address. Not applicable to whitelisted peers.<br/><br/>A value of 0 = unrestricted |
| `maxpendingresponses_getheaders` | 0     | Maximum allowed number of pending responses in the sending queue for received GETHEADERS P2P requests before the connection is closed. Not applicable to whitelisted peers. The main purpose of this setting is to limit memory usage. The specified value should be small (e.g. ~50) since in practice connected peers do not need to send many GETHEADERS requests in parallel. <br/><br/>A value of 0 = unrestricted. |
| `maxpendingresponses_gethdrsen`  | 0     | Maximum allowed number of pending responses in the sending queue for received GETHDRSEN P2P requests before the connection is closed. Not applicable to whitelisted peers. The main purpose of this setting is to limit memory usage. The specified value should be small (e.g. ~10) since in practice connected peers do not need to send many GETHDRSEN requests in parallel. <br/><br/>A value of 0 = unrestricted. |
|       |       |       |