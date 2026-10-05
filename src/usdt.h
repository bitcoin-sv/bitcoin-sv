// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// User Statically Defined Tracepoint (USDT) support.
//
// Tracepoints are zero-overhead when the node is built without tracing
// (ENABLE_USDT undefined) and near-zero when built with tracing but no
// tool is attached: each TRACEPOINT compiles to a single `nop` plus an entry
// in the ELF .note.stapsdt section. The trap is only patched in when a tool
// (bpftrace, bcc, ...) attaches to the probe.
//
// Usage:
//   1. Declare a semaphore once at file scope, in the translation unit that
//      fires the probe:
//          TRACEPOINT_SEMAPHORE(validation, block_connected);
//   2. Guard any non-trivial argument preparation with TRACEPOINT_ACTIVE so it
//      runs only while a tool is attached, then fire the probe:
//          if (TRACEPOINT_ACTIVE(validation, block_connected)) {
//              const uint256 hash = pindex->GetBlockHash();
//              TRACEPOINT(validation, block_connected, hash.data(), height);
//          }

#pragma once

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#ifdef ENABLE_USDT

#define SDT_USE_VARIADIC
#define _SDT_HAS_SEMAPHORES 1

#include <sys/sdt.h>

#define TRACEPOINT(provider, ...) STAP_PROBEV(provider, __VA_ARGS__)

#define TRACEPOINT_SEMAPHORE(provider, name) \
    volatile unsigned short provider##_##name##_semaphore __attribute__((section(".probes")))

#define TRACEPOINT_ACTIVE(provider, name) (provider##_##name##_semaphore > 0)

#else

#define TRACEPOINT_SEMAPHORE(provider, name)
#define TRACEPOINT_ACTIVE(provider, name) false
#define TRACEPOINT(provider, ...)

#endif // ENABLE_USDT

