#!/usr/bin/env python3
# Copyright (c) 2019 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.
"""
Script that prints out version string for every seed node

Run this script manually
"""
import socket
import sys

# adding this line to reach test_framework
sys.path.append('../functional/')

from test_framework.mininode import (
    NodeConn,
    NodeConnCB,
    msg_verack,
    MY_VERSION,
    NetworkThread,
    NETWORK_PORTS
)

# dns seed data is taken from chainparams.cpp
# format of seeds: [(name1, host1), ... , (nameN, hostN)]
dnsseeds_mainnet = [("bitcoinsv.io", "seed.bitcoinsv.io"),
                    ("cascharia.com", "seed.cascharia.com"),
                    ("satoshisvision.network", "seed.satoshisvision.network")]
dnsseeds_stn = [("bitcoinsv.io", "stn-seed.bitcoinsv.io")]
dnsseeds_testnet = [("bitcoinsv.io", "testnet-seed.bitcoinsv.io"),
                    ("cascharia.com", "testnet-seed.cascharia.com"),
                    ("bitcoincloud.net", "testnet-seed.bitcoincloud.net")]

dnsseed_map = {
    "mainnet" : dnsseeds_mainnet,
    "stn" : dnsseeds_stn,
    "testnet3" : dnsseeds_testnet
}


class SeedNodeConn(NodeConn):

    def handle_error(self):
        pass


class SeedNode(NodeConnCB):
    def __init__(self):
        super().__init__()
        self.log = False

    def enable_verion_log(self):
        self.log = True

    def on_version(self, conn, message):
        if message.nVersion >= 209:
            conn.send_message(msg_verack())
            self.send_protoconf(conn)
        conn.ver_send = min(MY_VERSION, message.nVersion)
        if message.nVersion < 209:
            conn.ver_recv = conn.ver_send
        conn.nServices = message.nServices
        if self.log:
            print(message.strSubVer.decode("utf-8"))


def check_seeds(network, dnsseeds, print_out=False):
    seeds_count = 0
    response_count = 0
    for dnsseed in dnsseeds:
        if print_out:
            print(f"Name: {dnsseed[0]} ### Host: {dnsseed[1]}")
        ip_addresses = set()
        for addinfo in socket.getaddrinfo(dnsseed[1], 80):
            ip_addresses.add(addinfo[4][0])

        ip_addresses = sorted(ip_addresses)
        for ip_address in ip_addresses:
            if print_out:
                print('%s %-15s' % (dnsseed[1], ip_address), end=" version_string: ")
            node0 = SeedNode()
            if print_out:
                node0.enable_verion_log()
            connection = SeedNodeConn(ip_address, NETWORK_PORTS[network], rpc=None, callback=node0, net=network)
            node0.add_connection(connection)

            network_thread = NetworkThread()
            network_thread.start()
            try:
                seeds_count += 1
                node0.wait_for_verack(timeout=3)
                node0.connection.close()
                network_thread.join()
                response_count += 1
            except:
                if print_out:
                    print("no response")
                if node0.connection is not None:
                    node0.connection.close()
                network_thread.join()
        if print_out:
            print()
    if print_out:
        print("Results:\n" + f"Seeds: {seeds_count} Responded: {response_count}")

    # we return true if all seed nodes responded
    if seeds_count == response_count:
        return True
    else:
        return False


if __name__ == "__main__":
    network = "mainnet"
    if len(sys.argv) == 2:
        if sys.argv[1] == "-testnet":
            network = "testnet3"
        elif sys.argv[1] == "-stn":
            network = "stn"
        elif sys.argv[1] == "-help":
            print("Usage:")
            print("Main network: python checkseednodes.py")
            print("Testnet network: python checkseednodes.py -testnet")
            print("Stn network: python checkseednodes.py -stn")
            exit(0)

    success = check_seeds(network=network, dnsseeds=dnsseed_map[network], print_out=True)
    exit(0 if success else 1)
