// Copyright (c) 2026 The Bitcoin SV developers
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "compat.h"
#include "enum_cast.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <unordered_map>
#include <vector>

/**
 * Backend-neutral record of the sockets the P2P socket handler wants to wait
 * on during one iteration, and which of them turned out to be ready.
 *
 * The fill pass (CNode -> Association -> StreamPolicy -> Stream) registers
 * each socket together with the events it is interested in via Add(). The
 * backend in CConnman::WaitForSockets (select() or poll()) then reads
 * the registrations through Entries() and records readiness by position with
 * SetReadyAt(), or by socket with SetReady(). The service pass reads the
 * result back with Ready() / IsReady().
 *
 * Lookups are keyed by socket value rather than by node or stream position
 * because a stream can move between associations between the fill and the
 * service passes. A socket that was never registered reads as NONE; nothing
 * in here asserts.
 *
 * Not thread-safe: intended to be owned by, and confined to, the socket
 * handler thread.
 */
class SocketWaitSet
{
  public:
    // Scoped so that neither a raw poll(2) bit nor any other integer can be
    // mistaken for one of ours; combine with the operators below.
    enum class Flags : uint8_t
    {
        NONE = 0,
        RECV = 1,   // readable, or accept() pending on a listen socket
        SEND = 2,   // writable
        ERR  = 4    // error, hang-up or invalid socket
    };
    // Short spellings so call sites read SocketWaitSet::RECV.
    static constexpr Flags NONE {Flags::NONE};
    static constexpr Flags RECV {Flags::RECV};
    static constexpr Flags SEND {Flags::SEND};
    static constexpr Flags ERR  {Flags::ERR};

    // Flags is a bitmask, so combinations such as RECV | ERR are legitimate
    // values: an enum with a fixed underlying type may hold any value of that
    // type. The analyser's EnumCastOutOfRange check only accepts the named
    // enumerators and does not model flag enums, hence the suppressions.
    friend constexpr Flags operator|(Flags a, Flags b)
    {
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        return static_cast<Flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }
    friend constexpr Flags operator&(Flags a, Flags b)
    {
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        return static_cast<Flags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }
    friend constexpr Flags& operator|=(Flags& a, Flags b)
    {
        return a = a | b;
    }

    struct Entry
    {
        SOCKET socket {INVALID_SOCKET};
        Flags wanted {NONE};
        Flags ready {NONE};
    };

    // Forget all entries but keep the allocated capacity for the next iteration.
    void Clear()
    {
        mEntries.clear();
        mIndex.clear();
    }

    // Register a socket to wait on. Registering the same socket twice merges
    // the flags. An INVALID_SOCKET is ignored.
    void Add(SOCKET socket, Flags flags)
    {
        if(socket == INVALID_SOCKET)
        {
            return;
        }
        if(const auto it { mIndex.find(socket) }; it != mIndex.end())
        {
            mEntries[it->second].wanted |= flags;
            return;
        }
        // Append first, index second: if push_back throws the index is left
        // untouched rather than pointing one past the end.
        const size_t index { mEntries.size() };
        mEntries.push_back(Entry{socket, flags, NONE});
        mIndex.emplace(socket, index);
    }

    // Record readiness for a registered socket. Unknown sockets are ignored.
    void SetReady(SOCKET socket, Flags flags)
    {
        if(const auto it { mIndex.find(socket) }; it != mIndex.end())
        {
            mEntries[it->second].ready |= flags;
        }
    }

    Flags Wanted(SOCKET socket) const
    {
        const auto it { mIndex.find(socket) };
        return (it == mIndex.end()) ? NONE : mEntries[it->second].wanted;
    }

    Flags Ready(SOCKET socket) const
    {
        const auto it { mIndex.find(socket) };
        return (it == mIndex.end()) ? NONE : mEntries[it->second].ready;
    }

    bool IsReady(SOCKET socket, Flags flags) const
    {
        return (Ready(socket) & flags) != NONE;
    }

    // Read-only view of the registrations, in registration order. The
    // position of an entry here is the index SetReadyAt() takes.
    std::span<const Entry> Entries() const { return mEntries; }

    // Record readiness for the entry at the given position. Out-of-range
    // positions are ignored.
    void SetReadyAt(size_t index, Flags flags)
    {
        if(index < mEntries.size())
        {
            mEntries[index].ready |= flags;
        }
    }

  private:
    std::vector<Entry> mEntries {};
    std::unordered_map<SOCKET, size_t> mIndex {};
};

// Names for the individual flags, for enum_cast.
inline const enumTableT<SocketWaitSet::Flags>& enumTable(SocketWaitSet::Flags)
{
    static const enumTableT<SocketWaitSet::Flags> table
    {
        { SocketWaitSet::NONE, "NONE" },
        { SocketWaitSet::RECV, "RECV" },
        { SocketWaitSet::SEND, "SEND" },
        { SocketWaitSet::ERR,  "ERR" }
    };
    return table;
}

// Print a combination of flags as e.g. "RECV|ERR" (or "NONE"). Used by the
// unit tests' BOOST_CHECK_EQUAL and handy in log output.
inline std::ostream& operator<<(std::ostream& os, SocketWaitSet::Flags flags)
{
    if(flags == SocketWaitSet::NONE)
    {
        return os << enum_cast<std::string>(SocketWaitSet::NONE);
    }
    const char* separator { "" };
    for(const SocketWaitSet::Flags bit : { SocketWaitSet::RECV, SocketWaitSet::SEND, SocketWaitSet::ERR })
    {
        if((flags & bit) != SocketWaitSet::NONE)
        {
            os << separator << enum_cast<std::string>(bit);
            separator = "|";
        }
    }
    return os;
}
