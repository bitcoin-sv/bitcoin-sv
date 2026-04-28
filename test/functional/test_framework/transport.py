#!/usr/bin/env python3
# Copyright (c) 2026 BSV Blockchain Association
# Distributed under the Open BSV software license, see the accompanying file LICENSE.

"""
Selector-based network transport layer.

  - Tests run in the main thread
  - Network I/O runs in a dedicated NetworkThread with a selector-based event loop
  - Thread synchronization via locks (mininode_lock, network_thread_loop_lock)

"""

from .rate_limiter import RateLimiter
from .static_attributes import StaticAttrsMeta
from threading import RLock, Thread
from abc import ABC, abstractmethod

import logging
from selectors import DefaultSelector, EVENT_READ, EVENT_WRITE
import socket
import time

# How much data will be read from the network at once
READ_BUFFER_SIZE = 1024 * 256

logger = logging.getLogger("TestFramework.mininode")

# One lock for synchronizing all data access between the networking thread (see
# NetworkThread below) and the thread running the test logic.  For simplicity,
# MessageHandler acquires this lock whenever delivering a message to a ConnectionCallback,
# and whenever adding anything to the send buffer (in send_message()).  This
# lock should be acquired in the thread running the test logic to synchronize
# access to any data shared with the MessageHandler or ConnectionCallback.
mininode_lock = RLock()

# Registry of active Connection connections
# This replaces the asyncore socket_map
mininode_socket_map = dict()

# Lock used to synchronize access to data required by loop running in NetworkThread.
# It must be locked, for example, when adding new MessageHandler object, otherwise loop in
# NetworkThread may try to access partially constructed object.
network_thread_loop_lock = RLock()

# Network thread acquires network_thread_loop_lock at start of each iteration and releases
# it at the end. Since the next iteration is run immediately after that, lock is acquired
# almost all of the time making it difficult for other threads to also acquire this lock.
# To work around this problem, NetworkThread first acquires network_thread_loop_intent_lock
# and immediately releases it before acquiring network_thread_loop_lock.
# Other threads (e.g. the ones calling MessageHandler constructor) acquire both locks before
# proceeding. The end result is that other threads wait at most one iteration of loop in
# NetworkThread.
network_thread_loop_intent_lock = RLock()


class ConnectionCallback(ABC):

    @abstractmethod
    def on_open(self, conn):
        pass

    @abstractmethod
    def on_close(self, conn):
        pass


class MessageHandler(ABC):
    @abstractmethod
    def got_data(self, data):
        pass

    @abstractmethod
    def got_message(self, message):
        pass


class Connection:
    """
    Socket handler for network connections.
    Uses non-blocking sockets and a selector-based event loop.
    """

    def __init__(self,
                 dstaddr,
                 dstport,
                 callback):

        if not isinstance(callback, ConnectionCallback):
            raise TypeError("callback must be an instance of ConnectionCallback")

        self.send_data_rate_limiter = None
        self.receive_data_rate_limiter = None

        with network_thread_loop_intent_lock, network_thread_loop_lock:
            self._dstaddr = dstaddr
            self._dstport = dstport
            self.cb = callback

            # Create non-blocking socket
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.setblocking(False)
            self._fileno = self._socket.fileno()

            self.sendbuf = bytearray()
            self.recvbuf = bytearray()
            self._last_sent = 0
            self._state = "connecting"
            self._disconnect = False
            self._node_conn = None

            # Register in socket map
            mininode_socket_map[id(self)] = self

    def connect(self):
        """Initiate connection to the remote host."""
        try:
            self._socket.connect((self._dstaddr, self._dstport))
        except BlockingIOError:
            # Connection in progress - this is expected for non-blocking sockets
            pass
        except Exception as e:
            logger.error("Error connecting to %s:%d: %s" % (self._dstaddr, self._dstport, e))
            self.close()

    def send_message(self, msg):
        """Queue a message for sending."""
        with mininode_lock:
            self.sendbuf += msg
            self._last_sent = time.time()

    @property
    def dstaddr(self):
        return self._dstaddr

    @dstaddr.setter
    def dstaddr(self, value):
        self._dstaddr = value

    @property
    def dstport(self):
        return self._dstport

    @dstport.setter
    def dstport(self, value):
        self._dstport = value

    @property
    def state(self):
        return self._state

    @state.setter
    def state(self, value):
        self._state = value

    @property
    def connected(self):
        return self._state == "connected"

    @property
    def closed(self):
        return self._state == "closed"

    @property
    def disconnect(self):
        return self._disconnect

    @disconnect.setter
    def disconnect(self, value=True):
        self._disconnect = value

    @property
    def last_sent(self):
        return self._last_sent

    @last_sent.setter
    def last_sent(self, value):
        self._last_sent = value

    def rate_limit_sending(self, limit):
        """The maximal rate of sending data in bytes/second, send rate will
        never exceed the limit while it will be somewhat slower.
         If None there will be no limit."""
        with mininode_lock:
            self.send_data_rate_limiter = RateLimiter(limit) if limit else None

    def rate_limit_receiving(self, limit):
        """The maximal rate of receiving in bytes/second, receive rate will
        never exceed the limit while it will be somewhat slower.
         If None there will be no limit."""
        with mininode_lock:
            self.receive_data_rate_limiter = RateLimiter(limit) if limit else None

    def close(self):
        with mininode_lock:
            self._state = "closed"
            self.recvbuf = bytearray()
            self.sendbuf = bytearray()
            try:
                self._socket.close()
            except Exception:
                pass
            # Remove from socket map
            if id(self) in mininode_socket_map:
                del mininode_socket_map[id(self)]

        self.cb.on_close(self)

    @property
    def node_conn(self):
        if self._node_conn is not None and isinstance(self._node_conn, MessageHandler):
            return self._node_conn
        else:
            raise AttributeError("Connection instance does not have a valid MessageHandler object associated with it.")

    @node_conn.setter
    def node_conn(self, value):
        if isinstance(value, MessageHandler):
            self._node_conn = value
        else:
            raise TypeError("node_conn must be an instance of MessageHandler")

    def read(self):
        if self._node_conn is None:
            return

        with mininode_lock:
            if self.closed:
                return
            try:
                if self.receive_data_rate_limiter is None:
                    t = self._socket.recv(READ_BUFFER_SIZE)
                else:
                    time_now = time.time()
                    read_chunk = self.receive_data_rate_limiter.calculate_next_chunk(time_now)
                    max_read = min(READ_BUFFER_SIZE, read_chunk)
                    t = self._socket.recv(max_read)
                    self.receive_data_rate_limiter.update_amount_processed(time_now, len(t), 0.1)
            except BlockingIOError:
                # No data available right now
                return
            except (ConnectionResetError, BrokenPipeError, OSError) as e:
                logger.debug("Read error on %s:%d: %s" % (self._dstaddr, self._dstport, e))
                self.close()
                return

            if len(t) == 0:
                # Connection closed by remote
                self.close()
                return

            # Check state again under the lock - connection may have been closed
            # while we were waiting for data
            if self.closed:
                return

            self.recvbuf += t

        while True:
            # Check state again before processing each message since connection
            # might have been closed while we were processing previous messages
            if self.closed:
                return
            try:
                msg, length = self._node_conn.got_data(self.recvbuf)
            except ValueError:
                # During connection shutdown, partial data may remain in buffer
                # which causes "got garbage" errors - this is expected and can be ignored
                if self.closed:
                    return
                raise

            if msg is None:
                break

            self.recvbuf = self.recvbuf[length:]
            self._node_conn.got_message(msg)

    def write(self):
        """Called when the socket is ready for writing."""
        just_connected = False
        with mininode_lock:
            if self.closed:
                return

            # Check if we just connected (first write event indicates connection complete)
            if self._state == "connecting":
                # Check if connection succeeded
                err = self._socket.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if err != 0:
                    logger.error("Connection failed to %s:%d: error %d" %
                                 (self._dstaddr, self._dstport, err))
                    self.close()
                    return
                self._state = "connected"
                just_connected = True

        # on_open callback must be called outside mininode_lock to allow
        # callback to acquire locks without deadlock
        if just_connected:
            self.cb.on_open(self)

        with mininode_lock:
            if self.closed:
                return

            if not self.writable():
                return

            try:
                if self.send_data_rate_limiter is None:
                    sent = self._socket.send(self.sendbuf)
                else:
                    time_now = time.time()
                    next_chunk = self.send_data_rate_limiter.calculate_next_chunk(time_now)
                    next_chunk = min(next_chunk, len(self.sendbuf))
                    sent = self._socket.send(self.sendbuf[:next_chunk])
                    self.send_data_rate_limiter.update_amount_processed(time_now, sent, 0.1)
            except BlockingIOError:
                # Socket buffer full, try again later
                return
            except (ConnectionResetError, BrokenPipeError, OSError) as e:
                logger.error("Write error on %s:%d: %s" % (self._dstaddr, self._dstport, e))
                self.close()
                return

            del self.sendbuf[:sent]

    def writable(self):
        """Return True if there is data to send or connection is pending."""
        with mininode_lock:
            pre_connection = self._state == "connecting"
            length = len(self.sendbuf)
        return (length > 0 or pre_connection)

    def fileno(self):
        """Return the socket's file descriptor."""
        return self._fileno

    def readable(self):
        """Return True if the connection is open and ready to receive."""
        with mininode_lock:
            return self._state == "connected"


NetworkThread_should_stop = False


def StopNetworkThread():
    global NetworkThread_should_stop
    NetworkThread_should_stop = True


class NetworkThread(Thread, metaclass=StaticAttrsMeta):
    """
    Thread that runs the network event loop using selectors.

    Note: Uses StaticAttrsMeta to catch attribute typos (e.g., 'deamon' instead of 'daemon').
    """

    poll_timeout = 0.1

    def run(self):
        # Create a selector for I/O multiplexing
        sel = DefaultSelector()

        while mininode_socket_map and not NetworkThread_should_stop:
            with network_thread_loop_intent_lock:
                # Acquire and immediately release lock.
                # This allows other threads to more easily acquire network_thread_loop_lock
                pass

            with network_thread_loop_lock:
                # Handle pending disconnects
                disconnected = []
                for key, obj in list(mininode_socket_map.items()):
                    if obj.disconnect:
                        disconnected.append(obj)
                for obj in disconnected:
                    obj.close()

                # Update selector registrations
                # First, unregister any closed sockets
                registered_fds = set(key.fd for key in sel.get_map().values())
                active_fds = set()

                for key, obj in list(mininode_socket_map.items()):
                    if obj._state == "closed":
                        continue

                    try:
                        fd = obj.fileno()
                        if fd < 0:
                            continue
                        active_fds.add(fd)

                        # Determine which events to monitor
                        events = 0
                        if obj.readable():
                            events |= EVENT_READ
                        if obj.writable():
                            events |= EVENT_WRITE

                        # Skip registration if no events to monitor
                        if events == 0:
                            continue

                        if fd in registered_fds:
                            # Modify existing registration
                            sel.modify(fd, events, obj)
                        else:
                            # New registration
                            sel.register(fd, events, obj)
                    except (ValueError, KeyError, OSError):
                        # Socket might have been closed
                        pass

                # Unregister closed sockets
                for fd in registered_fds - active_fds:
                    try:
                        sel.unregister(fd)
                    except (ValueError, KeyError):
                        pass

            # Wait for events OUTSIDE the lock to avoid holding it during blocking I/O.
            # This matches the asyncore pattern and prevents lock contention.
            try:
                events = sel.select(timeout=NetworkThread.poll_timeout)
            except Exception as e:
                # All exceptions are caught to prevent them from taking down the network thread.
                logger.warning("NetworkThread: select() failed! " + str(e))
                continue

            # Process events with the lock held
            with network_thread_loop_lock:
                for key, mask in events:
                    obj = key.data
                    try:
                        if mask & EVENT_READ:
                            obj.read()
                        if mask & EVENT_WRITE:
                            obj.write()
                    except Exception as e:
                        logger.warning("NetworkThread: Event handler failed: " + str(e))

        # Clean up
        sel.close()
        logger.debug("Network thread closing")
