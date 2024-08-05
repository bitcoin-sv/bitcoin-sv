// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "serialize.h"

#include <cstdint>
#include <optional>

/**
 * Encapsulate a P2P mempool message.
 */
class MempoolMsg
{
  public:

    // Defaults (in seconds)
    static constexpr int64_t DEFAULT_AGE { static_cast<int64_t>(60 * 60) };  // Older than an hour
    static constexpr int64_t DEFAULT_PERIOD { static_cast<int64_t>(15 * 60) }; // Every 15 minutes

    MempoolMsg() = default;
    MempoolMsg(int64_t age) : mAge{age} {}

    // Accessors
    [[nodiscard]] const std::optional<int64_t>& GetAge() const { return mAge; }

    // Serialisation
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if(ser_action.ForRead())
        {
            try
            {
                // Try reading age, but be prepared if it's an older message for read to fail
                READWRITE(mAge);
            }
            catch(...) {} // NOLINT(bugprone-empty-catch)
        }
        else
        {
            READWRITE(mAge);
        }
    }

  private:

    // Age of mempool entries (in seconds)
    std::optional<int64_t> mAge {};

};

