// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_COMPAT_H
#define BITCOIN_COMPAT_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#ifdef WIN32
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef FD_SETSIZE
#undef FD_SETSIZE // prevent redefinition compiler warning
#endif
#define FD_SETSIZE 1024 // max number of fds in fd_set

#include <winsock2.h> // Must be included before mswsock.h and windows.h

#include <mswsock.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <cstdint>
#else
#include <arpa/inet.h>
#include <climits>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#ifdef USE_POLL
#include <poll.h>
#endif
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef WIN32
#define MSG_DONTWAIT 0
#else
typedef int SOCKET;
#include "errno.h"
#define WSAGetLastError() errno
#define WSAEINVAL EINVAL
#define WSAEALREADY EALREADY
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEMSGSIZE EMSGSIZE
#define WSAEINTR EINTR
#define WSAEINPROGRESS EINPROGRESS
#define WSAEADDRINUSE EADDRINUSE
#define WSAENOTSOCK EBADF
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#ifdef WIN32
#ifndef S_IRUSR
#define S_IRUSR 0400
#define S_IWUSR 0200
#endif
#else
#define MAX_PATH 1024
#endif

#ifdef _MSC_VER
#if !defined(ssize_t)
#ifdef _WIN64
using ssize_t = int64_t ;
#else
using ssize_t = int32_t;
#endif
#endif
#endif

// As Solaris does not have the MSG_NOSIGNAL flag for send(2) syscall, it is
// defined as 0
#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

#if HAVE_DECL_STRNLEN == 0
size_t strnlen(const char *start, size_t max_len);
#endif // HAVE_DECL_STRNLEN

// Whether the P2P layer is able to wait on this socket.
//
// With select() only sockets numbered below FD_SETSIZE can be placed in an
// fd_set (and fortified glibc builds abort if a larger one is), so any socket
// that receives a higher number must be rejected as soon as it is created.
// With poll() (USE_POLL, POSIX) there is no such limit, and on Windows fd_set
// is an array of handles rather than a bitmap so there is none there either.
//
// This is the only place the FD_SETSIZE rule lives. The callers in net.cpp
// and netbase.cpp must stay in place for the select() fallback; on USE_POLL
// builds they compile to dead branches. The body depends on the config
// header, so this must only ever be compiled with HAVE_CONFIG_H defined.
static bool inline IsSelectableSocket([[maybe_unused]] SOCKET s) {
#if defined(WIN32) || defined(USE_POLL)
    return true;
#else
    return (s < FD_SETSIZE);
#endif
}

#endif // BITCOIN_COMPAT_H
