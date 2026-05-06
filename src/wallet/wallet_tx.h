// Copyright (c) 2026 BSV Blockchain
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "merkle_tx.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "uint256.h"
#include "wallet/wallet.h"

class CBlockIndex;

class CConnman;

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct COutputEntry
{
    CTxDestination destination;
    Amount amount;
    int vout;
};

/**
 * A transaction with a bunch of additional info that only the owner cares
 * about. It includes any unrecorded transactions needed to link it back to the
 * block chain.
 */
class CWalletTx : public CMerkleTx
{
private:
    const CWallet* pwallet;

public:
    mapValue_t mapValue;
    std::vector<std::pair<std::string, std::string>> vOrderForm;
    unsigned int fTimeReceivedIsTxTime;
    //!< time received by this node
    unsigned int nTimeReceived;
    /**
     * Stable timestamp that never changes, and reflects the order a transaction
     * was added to the wallet. Timestamp is based on the block time for a
     * transaction added as part of a block, or else the time when the
     * transaction was received if it wasn't part of a block, with the timestamp
     * adjusted in both cases so timestamp order matches the order transactions
     * were added to the wallet. More details can be found in
     * CWallet::ComputeTimeSmart().
     */
    unsigned int nTimeSmart;
    /**
     * From me flag is set to 1 for transactions that were created by the wallet
     * on this bitcoin node, and set to 0 for transactions that were created
     * externally and came in through the network or sendrawtransaction RPC.
     */
    char fFromMe;
    std::string strFromAccount;
    //!< position in ordered transaction list
    int64_t nOrderPos;

    // memory only
    mutable bool fDebitCached;
    mutable bool fCreditCached;
    mutable bool fImmatureCreditCached;
    mutable bool fAvailableCreditCached;
    mutable bool fWatchDebitCached;
    mutable bool fWatchCreditCached;
    mutable bool fImmatureWatchCreditCached;
    mutable bool fAvailableWatchCreditCached;
    mutable bool fChangeCached;
    mutable Amount nDebitCached;
    mutable Amount nCreditCached;
    mutable Amount nImmatureCreditCached;
    mutable Amount nAvailableCreditCached;
    mutable Amount nWatchDebitCached;
    mutable Amount nWatchCreditCached;
    mutable Amount nImmatureWatchCreditCached;
    mutable Amount nAvailableWatchCreditCached;
    mutable Amount nChangeCached;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CWalletTx() { Init(nullptr); }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    CWalletTx(const CWallet* pwalletIn, CTransactionRef arg) : CMerkleTx(std::move(arg))
    {
        Init(pwalletIn);
    }

    void Init(const CWallet* pwalletIn)
    {
        pwallet = pwalletIn;
        mapValue.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false;
        strFromAccount.clear();
        fDebitCached = false;
        fCreditCached = false;
        fImmatureCreditCached = false;
        fAvailableCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fChangeCached = false;
        nDebitCached = Amount(0);
        nCreditCached = Amount(0);
        nImmatureCreditCached = Amount(0);
        nAvailableCreditCached = Amount(0);
        nWatchDebitCached = Amount(0);
        nWatchCreditCached = Amount(0);
        nAvailableWatchCreditCached = Amount(0);
        nImmatureWatchCreditCached = Amount(0);
        nChangeCached = Amount(0);
        nOrderPos = -1;
    }

    ADD_SERIALIZE_METHODS

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        if(ser_action.ForRead())
            Init(nullptr);

        char fSpent = false;

        if(!ser_action.ForRead())
        {
            mapValue["fromaccount"] = strFromAccount;

            WriteOrderPos(nOrderPos, mapValue);

            if(nTimeSmart)
                mapValue["timesmart"] = strprintf("%u", nTimeSmart);
        }

        READWRITE(*(CMerkleTx*)this);
        //!< Used to be vtxPrev
        std::vector<CMerkleTx> vUnused;
        READWRITE(vUnused);
        READWRITE(mapValue);
        READWRITE(vOrderForm);
        READWRITE(fTimeReceivedIsTxTime);
        READWRITE(nTimeReceived);
        READWRITE(fFromMe);
        READWRITE(fSpent);

        if(ser_action.ForRead())
        {
            strFromAccount = mapValue["fromaccount"];

            ReadOrderPos(nOrderPos, mapValue);

            nTimeSmart = mapValue.count("timesmart")
                             ? (unsigned int)atoi64(mapValue["timesmart"])
                             : 0;
        }

        mapValue.erase("fromaccount");
        mapValue.erase("version");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    //! make sure balances are recalculated
    void MarkDirty()
    {
        fCreditCached = false;
        fAvailableCreditCached = false;
        fImmatureCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fDebitCached = false;
        fChangeCached = false;
    }

    void BindWallet(CWallet* pwalletIn)
    {
        pwallet = pwalletIn;
        MarkDirty();
    }

    //! filter decides which addresses will count towards the debit
    Amount GetDebit(const isminefilter& filter) const;
    Amount GetCredit(const isminefilter& filter) const;
    Amount GetImmatureCredit(bool fUseCache = true) const;
    Amount GetAvailableCredit(bool fUseCache = true) const;
    Amount GetImmatureWatchOnlyCredit(const bool& fUseCache = true) const;
    Amount GetAvailableWatchOnlyCredit(const bool& fUseCache = true) const;
    Amount GetChange() const;

    void GetAmounts(std::list<COutputEntry>& listReceived,
                    std::list<COutputEntry>& listSent,
                    Amount& nFee,
                    std::string& strSentAccount,
                    const isminefilter& filter) const;

    bool IsFromMe(const isminefilter& filter) const
    {
        return (GetDebit(filter) > Amount(0));
    }

    // True if only scriptSigs are different
    bool IsEquivalentTo(const CWalletTx& tx) const;

    bool InMempool() const;
    bool InNonFinalMempool() const;
    bool IsTrusted() const;

    int64_t GetTxTime() const;
    int GetRequestCount() const;

    // RelayWalletTransaction may only be called if fBroadcastTransactions!
    bool RelayWalletTransaction(CConnman* connman);

    std::set<uint256> GetConflicts() const;
};
