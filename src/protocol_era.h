// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
  
#pragma once

#include "enum_cast.h"

#include <cstdint>

class CoinWithScript;
class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)

// Protocol names
enum class ProtocolName
{
    Unknown = 0,
    Genesis,
    Chronicle
};
const enumTableT<ProtocolName>& enumTable(ProtocolName);

// Protocol activation status
enum class ProtocolEra
{
    Unknown = 0,
    PreGenesis,
    PostGenesis,
    PostChronicle
};      
            
// Get protocol activation status for the given block height.
ProtocolEra GetProtocolEra(const Config& config, int32_t nHeight);

// Get protocol activation status for the given coin at a particular "mining height".
// When a coin is present in mempool it will have height MEMPOOL_HEIGHT. 
// In this case you should call this overload and specify the expected height at which
// it will be mined (chainActive.Height()+1) to correctly determine which release
// is enabled for this coin.
ProtocolEra GetProtocolEra(const Config& config, const CoinWithScript& coin, int32_t miningHeight);
        
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
bool InProtocolGracePeriod(const Config& config, ProtocolName name, int32_t spendHeight);

