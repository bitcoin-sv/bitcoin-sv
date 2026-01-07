// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
  
#pragma once

#include "enum_cast.h"

#include <cstdint>

/**
 * Fake height value used in Coins to signify they are only in the memory
 * pool(since 0.8)
 */
static const int32_t MEMPOOL_HEIGHT = 0x7FFFFFFF;

struct ConfigScriptPolicy;

// Protocol names
enum class ProtocolName : int
{
    Unknown = 0,
    Genesis,
    Chronicle
};
const enumTableT<ProtocolName>& enumTable(ProtocolName);

// Protocol activation status
enum class ProtocolEra : int
{
    Unknown = 0,
    PreGenesis,
    PostGenesis,
    PostChronicle
};      

ProtocolEra GetProtocolEra(const int32_t genesis_activation_height, 
                           const int32_t chronicle_activation_height,
                           const int32_t nHeight);
            
// Get protocol activation status for the given block height.
ProtocolEra GetProtocolEra(const ConfigScriptPolicy& config, int32_t nHeight);
        
/* Get the "inverse" of the given protocol era for the given protocol grace period name.
 *
 * For Genesis grace period this is defined as:
 * PreGenesis => PostGenesis
 * PostGenesis => PreGenesis
 *      
 * For Chronicle grace period this is defined as:
 * PostGenesis => PostChronicle
 * PostChronicle => PostGenesis
 */
ProtocolEra GetInverseProtocolEra(ProtocolEra era, ProtocolName name);

// Helper to return if the given protocol name has active status
bool IsProtocolActive(ProtocolEra era, ProtocolName name);

// Check whether we are within the activation grace period for a release
bool InProtocolGracePeriod(const ConfigScriptPolicy& config, ProtocolName name, int32_t spendHeight);

