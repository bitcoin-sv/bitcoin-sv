// Copyright (c) 2026 The Bitcoin SV developers
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstddef>

// File-descriptor budgeting for the coins LevelDB. Kept in its own header
// (with zero leveldb dependencies) so libraries that must not depend on
// leveldb can read these limits without pulling in <leveldb/...> via
// dbwrapper.h.
namespace dbwrapper_limits
{

class MaxFiles
{
  public:
    explicit MaxFiles(size_t maxFiles_) : maxFiles{maxFiles_} {}
    static MaxFiles Default();

    size_t Get() const { return maxFiles; }

    // Default budget = min(AbsoluteMax, system FD hard limit - ReservedFds).
    static constexpr size_t ReservedFds { 512 };
    static constexpr size_t AbsoluteMax { 8192 };
    // LevelDB's own internal cap on max_open_files
    // (see src/leveldb/db/db_impl.cc ClipToRange call).
    static constexpr size_t LevelDbMax { 50'000 };
    // Default to use in the absence of any system FD limit information
    static constexpr size_t DefaultFallback { 64 };

  private:
    size_t maxFiles { DefaultFallback };
};

} // namespace dbwrapper_limits

