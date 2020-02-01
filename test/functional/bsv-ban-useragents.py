#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

import socket
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until

_lan_ip = None
def get_lan_ip():
    global _lan_ip
    if _lan_ip: return _lan_ip
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # doesn't even have to be reachable
        s.connect(('10.255.255.255', 1))
        _lan_ip = s.getsockname()[0]
    except:
        _lan_ip = '127.0.0.1'
    finally:
        s.close()
    return _lan_ip


class BanClientUA(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.setup_nodes()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):

        with self.run_node_with_connections("Testing that we will get banned if we are ClientA",
                                            0, ['-banclientua=ClientA', '-banclientua=ClientB'],
                                            number_of_connections=1, ip=get_lan_ip(), strSubVer=b"ClientA", wait_for_verack=False) as (conn,):
            wait_until(lambda: len(conn.rpc.listbanned()) == 1, check_interval=1, label="Waiting to be banned")
            assert get_lan_ip() in conn.rpc.listbanned()[0]["address"]
            conn.rpc.clearbanned()

        with self.run_node_with_connections("Testing that we will get banned if we are ClientB",
                                            0, ['-banclientua=ClientA', '-banclientua=ClientB'],
                                            number_of_connections=1, ip=get_lan_ip(), strSubVer=b"ThisIsClientB_", wait_for_verack=False) as (conn,):
            wait_until(lambda: len(conn.rpc.listbanned()) == 1, check_interval=1, label="Waiting to be banned")
            assert get_lan_ip() in conn.rpc.listbanned()[0]["address"]
            conn.rpc.clearbanned()

        with self.run_node_with_connections("Testing that we will NOT get banned if we are ClientC",
                                            0, ['-banclientua=ClientA', '-banclientua=ClientB'],
                                            number_of_connections=1, ip=get_lan_ip(), strSubVer=b"ClientC") as (conn,):
            assert conn.connected

        with self.run_node_with_connections("Testing that we will NOT get banned if banclientua is not specified",
                                            0, [],
                                            number_of_connections=1, ip=get_lan_ip(), strSubVer=b"ClientA") as (conn,):
            assert conn.connected


if __name__ == '__main__':
    BanClientUA().main()
