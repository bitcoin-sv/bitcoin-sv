// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
#include "verify_script_flags.h"

#include "consensus/params.h"
#include "policy/policy.h"
#include "protocol_era.h"

#include <cstdint>

uint32_t GetScriptVerifyFlags(const ProtocolEra era,
                              const bool require_standard,
                              const bool is_prom_mempool_flags,
                              const uint64_t prom_mempool_flags)
{
    // Get verification flags for overall script - individual UTXOs may need
    // to add/remove flags (done by CheckInputScripts).
    uint32_t scriptVerifyFlags { StandardScriptVerifyFlags(era) };
    if(!require_standard)
    {
        if(is_prom_mempool_flags)
        {
            scriptVerifyFlags = prom_mempool_flags;
        }
        scriptVerifyFlags |= SCRIPT_ENABLE_SIGHASH_FORKID;
    }

    return scriptVerifyFlags;

}

uint32_t GetBlockScriptFlags(const Consensus::Params& consensus_params,
                             const int32_t height,
                             const ProtocolEra protocol_era)
{
    uint32_t flags = SCRIPT_VERIFY_NONE;

    // P2SH became active on Apr 1 2012, block height 173,805 (mainnet)
    if(height >= consensus_params.p2shHeight)
        flags |= SCRIPT_VERIFY_P2SH;

    // Start enforcing the DERSIG (BIP66) rule
    if((height + 1) >= consensus_params.BIP66Height)
        flags |= SCRIPT_VERIFY_DERSIG;

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    if((height + 1) >= consensus_params.BIP65Height)
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

    // Start enforcing BIP112 (CSV).
    if((height + 1) >= consensus_params.CSVHeight)
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;

    // If the UAHF is enabled, we start accepting replay protected txns
    if(height >= consensus_params.uahfHeight)
    {
        flags |= SCRIPT_VERIFY_STRICTENC;
        flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
    }

    // If the DAA HF is enabled, we start rejecting transaction that use a high
    // s in their signature. We also make sure that signature that are supposed
    // to fail (for instance in multisig or other forms of smart contracts) are
    // null.
    if(height >= consensus_params.daaHeight)
    {
        flags |= SCRIPT_VERIFY_LOW_S;
        flags |= SCRIPT_VERIFY_NULLFAIL;
    }

    if(IsProtocolActive(protocol_era, ProtocolName::Genesis))
    {
        flags |= SCRIPT_GENESIS;
        flags |= SCRIPT_VERIFY_SIGPUSHONLY;
    }

    if(IsProtocolActive(protocol_era, ProtocolName::Chronicle))
        flags |= SCRIPT_CHRONICLE;

    return flags;
}

