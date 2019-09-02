// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script_num.h"

#include "int_serialization.h"
#include <iterator>

using namespace std;

CScriptNum::CScriptNum(const vector<uint8_t> &vch, bool fRequireMinimal,
                       const size_t nMaxNumSize) {
    if (vch.size() > nMaxNumSize) {
        throw scriptnum_overflow_error("script number overflow");
    }
    if (fRequireMinimal && !bsv::IsMinimallyEncoded(vch, nMaxNumSize)) {
        throw scriptnum_minencode_error("non-minimally encoded script number");
    }
    m_value = vch.empty() ? 0 : bsv::deserialize<int64_t>(begin(vch), end(vch));
}

vector<uint8_t> CScriptNum::getvch() const {
    vector<uint8_t> v;
    v.reserve(sizeof(m_value));
    bsv::serialize(m_value, back_inserter(v));
    return v;
}


