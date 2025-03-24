// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
#include <cstdint>

enum class ProtocolEra;

uint32_t GetScriptVerifyFlags(ProtocolEra,
                              bool require_standard,
                              bool is_prom_mempool_flags=false,
                              uint64_t prom_mempool_flags=0);
