// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <enum_cast.h>
#include <mining/candidates.h>
#include <mining/legacy.h>

class Config;

namespace mining
{

class CMiningFactory
{
  public:

    // The types of supported block assembler
    enum class BlockAssemblerType
    {
        UNKNOWN,
        LEGACY,
        JOURNALING
    };

    // Get an appropriate block assembler
    static BlockAssemblerRef GetAssembler(const Config& config);

    // Get a reference to the mining candidate manager
    static CMiningCandidateManager& GetCandidateManager();

};

// Enable enum_cast for BlockAssemblerType
const enumTableT<CMiningFactory::BlockAssemblerType>& enumTable(CMiningFactory::BlockAssemblerType);

// Default block assembler type to use
constexpr CMiningFactory::BlockAssemblerType DEFAULT_BLOCK_ASSEMBLER_TYPE { CMiningFactory::BlockAssemblerType::LEGACY };

}
