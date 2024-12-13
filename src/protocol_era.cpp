// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "protocol_era.h"

#include "config.h"
#include "txmempool.h"

#include <stdexcept>

// Enable enum_cast for ProtocolName
const enumTableT<ProtocolName>& enumTable(ProtocolName)
{   
    static enumTableT<ProtocolName> table
    {   
        { ProtocolName::Unknown,   "Unknown" },
        { ProtocolName::Genesis,   "Genesis" },
        { ProtocolName::Chronicle, "Chronicle" }
    };
    return table;
}

// Get protocol activation states for the given block height
ProtocolEra GetProtocolEra(const Config& config, int32_t nHeight)
{
    if(nHeight == MEMPOOL_HEIGHT)
    {
        throw std::runtime_error("A coin with height == MEMPOOL_HEIGHT was passed "
            "to GetProtocolEra overload that does not handle this case. "
            "Use the overload that takes Coin as parameter.");
    }

    if(nHeight < config.GetGenesisActivationHeight())
    {   
        return ProtocolEra::PreGenesis;
    }
    if(nHeight < config.GetChronicleActivationHeight())
    {   
        return ProtocolEra::PostGenesis;
    }
    return ProtocolEra::PostChronicle;
}

// Get protocol activation status for the given coin at a particular mining height.
ProtocolEra GetProtocolEra(const Config& config, const CoinWithScript& coin, int32_t miningHeight)
{
    const auto height { coin.GetHeight() };

    if(height == MEMPOOL_HEIGHT)
    {
        return GetProtocolEra(config, miningHeight);
    }

    return GetProtocolEra(config, height);
}

// Get the "inverse" of the given protocol era for the given protocol grace period name.
ProtocolEra GetInverseProtocolEra(ProtocolEra era, ProtocolName name)
{   
    if(name == ProtocolName::Genesis)
    {   
        if(era == ProtocolEra::PreGenesis)
        {   
            return ProtocolEra::PostGenesis;
        }
        else if(era == ProtocolEra::PostGenesis)
        {   
            return ProtocolEra::PreGenesis;
        }
        else
        {   
            throw std::runtime_error("Can't create inverse of era for: " + enum_cast<std::string>(name));
        }
    }
    else if(name == ProtocolName::Chronicle)
    {
        if(era == ProtocolEra::PostGenesis)
        {
            return ProtocolEra::PostChronicle;
        }
        else if(era == ProtocolEra::PostChronicle)
        {
            return ProtocolEra::PostGenesis;
        }
        else
        {
            throw std::runtime_error("Can't create inverse of era for: " + enum_cast<std::string>(name));
        }
    }
    else
    {
        throw std::runtime_error("Unknown grace period name: " + enum_cast<std::string>(name));
    }
}

// Helper to return if the given protocol name has active status
bool IsProtocolActive(ProtocolEra era, ProtocolName name)
{
    if(name == ProtocolName::Genesis)
    {
        return era >= ProtocolEra::PostGenesis;
    }
    else if(name == ProtocolName::Chronicle)
    {
        return era >= ProtocolEra::PostChronicle;
    }
    else
    {
        throw std::runtime_error("Unknown protocol name: " + enum_cast<std::string>(name));
    }
}

// Check whether we are within the activation grace period for a protocol
bool InProtocolGracePeriod(const Config& config, ProtocolName name, int32_t spendHeight)
{
    int32_t activationHeight {0};
    int32_t gracePeriod {0};
    if(name == ProtocolName::Genesis)
    {
        activationHeight = config.GetGenesisActivationHeight();
        gracePeriod = static_cast<int32_t>(config.GetGenesisGracefulPeriod());
    }
    else if(name == ProtocolName::Chronicle)
    {
        activationHeight = config.GetChronicleActivationHeight();
        gracePeriod = static_cast<int32_t>(config.GetChronicleGracefulPeriod());
    }
    else
    {
        throw std::runtime_error("Unknown protocol name: " + enum_cast<std::string>(name));
    }

    return ((activationHeight - gracePeriod) < spendHeight &&
            (activationHeight + gracePeriod) > spendHeight);
}

