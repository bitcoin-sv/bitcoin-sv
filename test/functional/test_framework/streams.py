#!/usr/bin/env python3
# Copyright (c) 2020 Bitcoin Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

from enum import Enum


# Stream types enumeration
class StreamType(Enum):
    UNKNOWN = 0
    GENERAL = 1
    DATA1 = 2
    DATA2 = 3
    DATA3 = 4
    DATA4 = 5

# Stream policies


class DefaultStreamPolicy():
    def __init__(self):
        self.policy_name = b"Default"
        self.additional_streams = []

    def stream_type_for_message_type(self, msg):
        # Everything over the GENERAL stream
        return StreamType.GENERAL


class BlockPriorityStreamPolicy():
    def __init__(self):
        self.policy_name = b"BlockPriority"
        self.additional_streams = [StreamType.DATA1]

    def stream_type_for_message_type(self, msg):
        # Block related and pings over DATA1, everything else over GENERAL
        if msg.command in [b"block", b"cmpctblock", b"blocktxn", b"getblocktxn", b"headers", b"getheaders", b"ping", b"pong"]:
            return StreamType.DATA1
        return StreamType.GENERAL
