// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "orphan_txns.h"
#include "txn_double_spend_detector.h"
#include "txn_recent_rejects.h"
#include "mining/journal_builder.h"

/**
 * A class used to share txn handlers during validation.
 */
struct CTxnHandlers {
    /** Mempool Journal ChangeSet */
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const mining::CJournalChangeSetPtr& mJournalChangeSet;
    /** Double Spend Detector */
    TxnDoubleSpendDetectorSPtr mpTxnDoubleSpendDetector {nullptr};
    /** Handle orphan transactions */
    OrphanTxnsSPtr mpOrphanTxns {nullptr};
    /** Filter for transactions that were recently rejected */
    TxnRecentRejectsSPtr mpTxnRecentRejects {nullptr};
};
