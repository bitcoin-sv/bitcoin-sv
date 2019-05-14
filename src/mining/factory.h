// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOINSV_FACTORY_H
#define BITCOINSV_FACTORY_H

#include "mining/candidates.h"
#include "mining/legacy.h"

class Config;


class CMiningFactory {
private:
    static CMiningCandidateManager manager;

public:
    static BlockAssemblerRef GetAssembler(const Config &config) {
        return std::make_shared<LegacyBlockAssembler>(config);
    };
    static CMiningCandidateManager &GetCandidateManager() {
        return manager;
    };
};

#endif //BITCOINSV_FACTORY_H
