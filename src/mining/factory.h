// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <enum_cast.h>
#include <mining/candidates.h>
#include <mining/assembler.h>

#include <memory>

class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)

namespace mining
{

class CMiningFactory
{
  public:

    CMiningFactory(const Config& config);

    // The types of supported block assembler
    enum class BlockAssemblerType
    {
        UNKNOWN,
        LEGACY,
        JOURNALING
    };

    // Get an appropriate block assembler
    BlockAssemblerRef GetAssembler() const;

    // Get a reference to the mining candidate manager
    static CMiningCandidateManager& GetCandidateManager();

  private:

    // Keep reference to the global config
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const Config& mConfig;

    // A single journaling block assember; only created if configured appropriately.
    BlockAssemblerRef   mJournalingAssembler {nullptr};

};

// Enable enum_cast for BlockAssemblerType
const enumTableT<CMiningFactory::BlockAssemblerType>& enumTable(CMiningFactory::BlockAssemblerType);

// Default block assembler type to use
constexpr CMiningFactory::BlockAssemblerType DEFAULT_BLOCK_ASSEMBLER_TYPE { CMiningFactory::BlockAssemblerType::JOURNALING };

// A global unique mining factory
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::unique_ptr<CMiningFactory> g_miningFactory {nullptr};

}
