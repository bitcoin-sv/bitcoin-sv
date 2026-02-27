// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "random.h"
#include "txhasher.h"

//NOLINTBEGIN(cert-err58-cpp)
const uint64_t StaticHasherSalt::k0{GetRand(std::numeric_limits<uint64_t>::max())};
const uint64_t StaticHasherSalt::k1{GetRand(std::numeric_limits<uint64_t>::max())};
//NOLINTEND(cert-err58-cpp)
