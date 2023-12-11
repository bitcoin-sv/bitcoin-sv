#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from .mininode import (NodeConn,
                       NodeConnCB,
                       msg_createstream,
                       msg_block,
                       msg_cmpctblock,
                       msg_blocktxn,
                       msg_getblocktxn,
                       msg_headers,
                       msg_getheaders,
                       msg_ping,
                       msg_pong)
from .util import p2p_port
from .streams import StreamType


# Association callbacks
class AssociationCB():

    # Replace or override callbacks to get notified in a test
    def on_addr(self, stream, message): pass
    def on_alert(self, stream, message): pass
    def on_block(self, stream, message): pass
    def on_blocktxn(self, stream, message): pass
    def on_cmpctblock(self, stream, message): pass
    def on_feefilter(self, stream, message): pass
    def on_getaddr(self, stream, message): pass
    def on_getblocks(self, stream, message): pass
    def on_getblocktxn(self, stream, message): pass
    def on_getdata(self, stream, message): pass
    def on_getheaders(self, stream, message): pass
    def on_headers(self, stream, message): pass
    def on_mempool(self, stream): pass
    def on_ping(self, stream, message): pass
    def on_pong(self, stream, message): pass
    def on_reject(self, stream, message): pass
    def on_sendcmpct(self, stream, message): pass
    def on_sendheaders(self, stream, message): pass
    def on_tx(self, stream, message): pass
    def on_inv(self, stream, message): pass
    def on_verack(self, stream, message): pass
    def on_streamack(self, stream, message): pass
    def on_version(self, stream, message): pass
    def on_protoconf(self, stream, message): pass


# Simple wrapper for a single stream within an association
class Stream():
    def __init__(self, stream_type, conn):
        self.stream_type = stream_type
        self.conn = conn

    def send_message(self, msg):
        self.conn.send_message(msg)

    def close(self):
        self.conn.close()
        self.conn = None


# Connection callback for stream within an association
class StreamCB(NodeConnCB):
    def __init__(self, association):
        super().__init__()
        self.association = association
        self.recvAssocID = None
        self.recvStreamPolicies = None

    # Forward message callbacks to association callback class
    def on_addr(self, conn, message):
        super().on_addr(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_addr(stream, message)

    def on_alert(self, conn, message):
        super().on_alert(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_alert(stream, message)

    def on_block(self, conn, message):
        super().on_block(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_block(stream, message)

    def on_blocktxn(self, conn, message):
        super().on_blocktxn(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_blocktxn(stream, message)

    def on_cmpctblock(self, conn, message):
        super().on_cmpctblock(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_cmpctblock(stream, message)

    def on_feefilter(self, conn, message):
        super().on_feefilter(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_feefilter(stream, message)

    def on_getaddr(self, conn, message):
        super().on_getaddr(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_getaddr(stream, message)

    def on_getblocks(self, conn, message):
        super().on_getblocks(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_getblocks(stream, message)

    def on_getblocktxn(self, conn, message):
        super().on_getblocktxn(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_getblocktxn(stream, message)

    def on_getdata(self, conn, message):
        super().on_getdata(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_getdata(stream, message)

    def on_getheaders(self, conn, message):
        super().on_getheaders(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_getheaders(stream, message)

    def on_headers(self, conn, message):
        super().on_headers(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_headers(stream, message)

    def on_mempool(self, conn):
        super().on_mempool(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_mempool(stream, message)

    def on_ping(self, conn, message):
        super().on_ping(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_ping(stream, message)

    def on_pong(self, conn, message):
        super().on_pong(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_pong(stream, message)

    def on_reject(self, conn, message):
        super().on_reject(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_reject(stream, message)

    def on_sendcmpct(self, conn, message):
        super().on_sendcmpct(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_sendcmpct(stream, message)

    def on_sendheaders(self, conn, message):
        super().on_sendheaders(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_sendheaders(stream, message)

    def on_tx(self, conn, message):
        super().on_tx(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_tx(stream, message)

    def on_inv(self, conn, message):
        super().on_inv(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_inv(stream, message)

    def on_verack(self, conn, message):
        super().on_verack(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_verack(stream, message)

    def on_streamack(self, conn, message):
        super().on_streamack(conn, message)
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_streamack(stream, message)

    def on_version(self, conn, message):
        super().on_version(conn, message)
        self.recvAssocID = message.assocID
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_version(stream, message)

    def on_protoconf(self, conn, message):
        super().on_protoconf(conn, message)
        self.recvStreamPolicies = message.protoconf.stream_policies
        stream = self.association.conn_to_stream_map[conn]
        self.association.callbacks.on_protoconf(stream, message)


# An association
class Association():
    def __init__(self, stream_policy, cb_class):
        self.stream_policy = stream_policy
        self.callbacks = cb_class()
        self.stream_callbacks = {}
        self.streams = {}
        self.conn_to_stream_map = {}
        self.association_id = None
        self.recvd_stream_policy_names = None

    # Create streams to node
    def create_streams(self, node, ip='127.0.0.1', connArgs={}):
        # Create GENERAL stream connection
        general_cb = StreamCB(self)
        self.stream_callbacks[StreamType.GENERAL] = general_cb
        conn = NodeConn(ip, p2p_port(0), node, general_cb, **connArgs)
        general_stream = Stream(StreamType.GENERAL, conn)
        self.streams[StreamType.GENERAL] = general_stream
        self.conn_to_stream_map[conn] = general_stream

        # Create additional stream connections
        for additional_stream_type in self.stream_policy.additional_streams:
            stream_cb = StreamCB(self)
            self.stream_callbacks[additional_stream_type] = stream_cb
            conn = NodeConn(ip, p2p_port(0), node, stream_cb, assocID=general_stream.conn.assocID, **connArgs)
            stream = Stream(additional_stream_type, conn)
            self.streams[additional_stream_type] = stream
            self.conn_to_stream_map[conn] = stream

    # Close streams to node
    def close_streams(self):
        for stream in self.streams:
            self.streams[stream].close()
        del self.streams

    # Exchange required messages to setup association fully
    def setup(self):
        # Wait for initial protoconf message from GENERAL stream
        general_cb = self.stream_callbacks[StreamType.GENERAL]
        general_cb.wait_for_protoconf()

        # Record association ID and negotiated stream policy names
        self.association_id = general_cb.recvAssocID
        self.recvd_stream_policy_names = general_cb.recvStreamPolicies

        # Send required additional create_stream messages
        for additional_stream_type in self.stream_policy.additional_streams:
            stream = self.streams[additional_stream_type]
            stream_cb = self.stream_callbacks[additional_stream_type]
            stream.send_message(msg_createstream(stream_type=additional_stream_type.value,
                                                 stream_policy=self.stream_policy.policy_name,
                                                 assocID=self.association_id))
            stream_cb.wait_for_streamack()

    # Send a message
    def send_message(self, msg):
        # Get stream to send messages of this type on
        stream_type = self.stream_policy.stream_type_for_message_type(msg)
        stream = self.streams[stream_type]

        # Send message
        stream.send_message(msg)
