// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_COINCONTROL_H
#define BITCOIN_WALLET_COINCONTROL_H

#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/standard.h"

class CCoinControl
{
public:
    CTxDestination destChange{};
    //! If false, allows unselected inputs, but requires all selected inputs be
    //! used
    bool fAllowOtherInputs{false};
    //! Includes watch only addresses which match the ISMINE_WATCH_SOLVABLE
    //! criteria
    bool fAllowWatchOnly{false};
    //! Minimum absolute fee (not per kilobyte)
    Amount nMinimumTotalFee{};
    //! Override estimated feerate
    bool fOverrideFeeRate{false};
    //! Feerate to use if overrideFeeRate is true
    CFeeRate nFeeRate{};

    bool HasSelected() const { return !setSelected.empty(); }

    bool IsSelected(const COutPoint& output) const { return setSelected.contains(output); }

    void Select(const COutPoint& output) { setSelected.insert(output); }

    void ListSelected(std::vector<COutPoint>& vOutpoints) const {
        vOutpoints.assign(setSelected.begin(), setSelected.end());
    }

private:
    std::set<COutPoint> setSelected;
};

#endif // BITCOIN_WALLET_COINCONTROL_H
