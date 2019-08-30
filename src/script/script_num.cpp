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

bool CScriptNum::MinimallyEncode(std::vector<uint8_t> &data) {
    if (data.size() == 0) {
        return false;
    }

    // If the last byte is not 0x00 or 0x80, we are minimally encoded.
    uint8_t last = data.back();
    if (last & 0x7f) {
        return false;
    }

    // If the script is one byte long, then we have a zero, which encodes as an
    // empty array.
    if (data.size() == 1) {
        data = {};
        return true;
    }

    // If the next byte has it sign bit set, then we are minimaly encoded.
    if (data[data.size() - 2] & 0x80) {
        return false;
    }

    // We are not minimally encoded, we need to figure out how much to trim.
    for (size_t i = data.size() - 1; i > 0; i--) {
        // We found a non zero byte, time to encode.
        if (data[i - 1] != 0) {
            if (data[i - 1] & 0x80) {
                // We found a byte with it sign bit set so we need one more
                // byte.
                data[i++] = last;
            } else {
                // the sign bit is clear, we can use it.
                data[i - 1] |= last;
            }

            data.resize(i);
            return true;
        }
    }

    // If we the whole thing is zeros, then we have a zero.
    data = {};
    return true;
}

