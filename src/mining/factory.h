// Copyright (c) 2019 The Bitcoin SV developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOINSV_FACTORY_H
#define BITCOINSV_FACTORY_H

#include "mining/legacy.h"

class Config;


class CMiningFactory {
public:
    static BlockAssemblerRef GetAssembler(const Config &config) {
        return std::make_shared<LegacyBlockAssembler>(config);
    };
};

#endif //BITCOINSV_FACTORY_H
