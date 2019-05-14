// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "amount.h"

#include "tinyformat.h"

const std::string CURRENCY_UNIT = "BSV";

std::string Amount::ToString() const {
    return strprintf("%d.%08d %s", amount / COIN.GetSatoshis(),
                     amount % COIN.GetSatoshis(), CURRENCY_UNIT);
}

CFeeRate::CFeeRate(const Amount nFeePaid, size_t nBytes_) {
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    if (nSize > 0) {
        nSatoshisPerK = 1000 * nFeePaid / nSize;
    } else {
        nSatoshisPerK = Amount(0);
    }
}

Amount CFeeRate::GetFee(size_t nBytes_) const {
    assert(nBytes_ <= uint64_t(std::numeric_limits<int64_t>::max()));
    int64_t nSize = int64_t(nBytes_);

    Amount nFee = nSize * nSatoshisPerK / 1000;

    if (nFee == Amount(0) && nSize != 0) {
        if (nSatoshisPerK > Amount(0)) {
            nFee = Amount(1);
        }
        if (nSatoshisPerK < Amount(0)) {
            nFee = Amount(-1);
        }
    }

    return nFee;
}

std::string CFeeRate::ToString() const {
    return strprintf(
        "%d.%08d %s/kB", nSatoshisPerK.GetSatoshis() / COIN.GetSatoshis(),
        nSatoshisPerK.GetSatoshis() % COIN.GetSatoshis(), CURRENCY_UNIT);
}
