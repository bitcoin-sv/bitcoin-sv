#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin SV developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from http.server import BaseHTTPRequestHandler
from enum import Enum
import urllib.parse as urlparse
from urllib.parse import parse_qs
import json
import time


class RECEIVE(Enum):
    YES = 1
    NO = 0


class STATUS(Enum):
    SUCCESS = 0
    CLIENT_ERROR = 1
    SERVER_ERROR = 2


class RESPONSE_TIME(Enum):
    FAST = 0
    SLOW = 10
    SLOWEST = 70


class FLAG(Enum):
    YES = 1
    NO = 0


expectedProofs = []
receivedProofs = []

# Function to reset the expected/received proofs


def reset_proofs():
    expectedProofs = []
    receivedProofs = []


class CallbackService(BaseHTTPRequestHandler):

    def __init__(self, receive, status, response_time, add_flag, *args, **kwargs):
        self.receive = receive
        self.status = status
        self.response_time = response_time
        self.add_flag = add_flag
        super().__init__(*args, **kwargs)

    def do_GET(self):

        if (self.status == STATUS.CLIENT_ERROR):
            self.send_response(400, "Mocking client error.")
            self.send_header('x-bsv-dsnt', 1)
            self.end_headers()
            return

        if (self.status == STATUS.SERVER_ERROR):
            self.send_response(500, "Mocking server error.")
            self.send_header('x-bsv-dsnt', 1)
            self.end_headers()
            return

        request = self.path.split("/")
        if (len(request) == 5):
            dsnt = request[1]
            ver = request[2]
            request_type = request[3]

            if (dsnt == "dsnt" and ver == "1" and request_type == "query"):
                txid = request[4]

                if (self.response_time == RESPONSE_TIME.SLOW):
                    time.sleep(RESPONSE_TIME.SLOW.value)
                elif (self.response_time == RESPONSE_TIME.SLOWEST):
                    time.sleep(RESPONSE_TIME.SLOWEST.value)

                self.send_response(200)
                if (self.receive == RECEIVE.YES):
                    if (self.add_flag == FLAG.NO):
                        self.send_header('random-header', 1)
                        self.end_headers()
                        return

                    expectedProofs.append(txid)
                    self.send_header('x-bsv-dsnt', 1)
                    self.end_headers()
                    return
                else:
                    self.send_header('x-bsv-dsnt', 0)
                    self.end_headers()
                    return

        elif (len(request) == 4):
            dsnt = request[1]
            ver = request[2]
            request_type = request[3]

            # helper method for testing purposes
            # return received transactions' ids
            if (dsnt == "dsnt" and ver == "1" and request_type == "received"):
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('x-bsv-dsnt', 1)
                self.end_headers()
                self.wfile.write(json.dumps(receivedProofs).encode('utf-8'))
                return

            # helper method for testing purposes
            # return queried transactions' ids
            if (dsnt == "dsnt" and ver == "1" and request_type == "queried"):
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('x-bsv-dsnt', 1)
                self.end_headers()
                self.wfile.write(json.dumps(expectedProofs).encode('utf-8'))
                return

        self.send_response(400, "Malformed URL.")
        self.end_headers()

    def do_POST(self):

        if (self.status == STATUS.CLIENT_ERROR):
            self.send_response(400, "Mocking client error.")
            self.send_header('x-bsv-dsnt', 1)
            self.end_headers()
            return

        if (self.status == STATUS.SERVER_ERROR):
            self.send_response(500, "Mocking server error.")
            self.send_header('x-bsv-dsnt', 1)
            self.end_headers()
            return

        request = self.path.split("/")
        if (len(request) == 4):
            dsnt = request[1]
            ver = request[2]
            request_type = request[3]

            if (dsnt == "dsnt" and ver == "1" and request_type.startswith("submit")):

                if (self.response_time == RESPONSE_TIME.SLOW):
                    time.sleep(RESPONSE_TIME.SLOW.value)
                elif (self.response_time == RESPONSE_TIME.SLOWEST):
                    time.sleep(RESPONSE_TIME.SLOWEST.value)

                parsed = parse_qs(urlparse.urlparse(self.path).query)
                if all (key in parsed for key in ('txid', 'n', 'ctxid', 'cn')):
                    txid = parsed['txid'][0]
                    ctxid = parsed['ctxid'][0]
                    n = parsed['n'][0]
                    cn = parsed['cn'][0]
                    if (not n.isdigit() or not cn.isdigit()):
                        self.send_response(400, "Malformed URL: output number should be positive int or 0.")
                        self.end_headers()
                        return

                    if (txid == ctxid):
                        self.send_response(400, "Malformed URL: txid must not be the same as ctxid.")
                        self.end_headers()
                        return

                    if (txid in expectedProofs):
                        self.send_response(200)
                        self.send_header('x-bsv-dsnt', 1)
                        self.end_headers()
                        expectedProofs.remove(txid)
                        receivedProofs.append(txid)
                        return

                    self.send_response(400, "This txid was not asked for.")
                    self.end_headers()
                    return

                else:
                    self.send_response(400, "Malformed URL: URL should contain 'txid', 'n', 'ctxid', 'cn' parameters.")
                    self.end_headers()
                    return

        self.send_response(400, "Malformed URL.")
        self.end_headers()
