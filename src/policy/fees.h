// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_POLICY_FEE_H 
#define BITCOIN_POLICY_FEE_H 

#include "amount.h"
#include "random.h"
#include "uint256.h"

#include <map>
#include <string>
#include <vector>

class CFeeRate;

// Maximum value for tracking feerates
static const Amount MAX_FEERATE(int64_t(1e7));

// We have to lump transactions into buckets based on feerate, but we want to be
// able to give accurate estimates over a large range of potential feerates.
// Therefore it makes sense to exponentially space the buckets
/** Spacing of FeeRate buckets */
static const double FEE_SPACING = 1.1;


class FeeFilterRounder {
public:
    /** Create new FeeFilterRounder */
    FeeFilterRounder(const CFeeRate &minIncrementalFee);

    /** Quantize a minimum fee for privacy purpose before broadcast **/
    Amount round(const Amount currentMinFee);

private:
    std::set<Amount> feeset;
    FastRandomContext insecure_rand;
};
#endif /*BITCOIN_POLICY_FEE_H  */

