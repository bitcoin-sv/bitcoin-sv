// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
#include "verify_script_flags.h"

#include "policy/policy.h"

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

