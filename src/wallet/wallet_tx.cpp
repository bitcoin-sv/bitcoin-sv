// Copyright (c) 2026 BSV Blockchain
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "wallet_tx.h"

#include "finaltx.h"

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int CWalletTx::GetRequestCount() const
{
    LOCK(pwallet->cs_wallet);

    // Returns -1 if it wasn't being tracked.
    int nRequests = -1;

    if(IsCoinBase())
    {
        // Generated block.
        if(!hashUnset())
        {
            std::map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(
                hashBlock);
            if(mi != pwallet->mapRequestCount.end())
            {
                nRequests = (*mi).second;
            }
        }
    }
    else
    {
        // Did anyone request this transaction?
        std::map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(
            GetId());
        if(mi != pwallet->mapRequestCount.end())
        {
            nRequests = (*mi).second;

            // How about the block it's in?
            if(nRequests == 0 && !hashUnset())
            {
                std::map<uint256, int>::const_iterator _mi = pwallet->mapRequestCount
                                                                 .find(hashBlock);
                if(_mi != pwallet->mapRequestCount.end())
                {
                    nRequests = (*_mi).second;
                }
                else
                {
                    // If it's in someone else's block it must have got out.
                    nRequests = 1;
                }
            }
        }
    }

    return nRequests;
}

void CWalletTx::GetAmounts(std::list<COutputEntry>& listReceived,
                           std::list<COutputEntry>& listSent,
                           Amount& nFee,
                           std::string& strSentAccount,
                           const isminefilter& filter) const
{
    nFee = Amount(0);
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Compute fee:
    Amount nDebit = GetDebit(filter);
    // debit>0 means we signed/sent this transaction.
    if(nDebit > Amount(0))
    {
        Amount nValueOut = tx->GetValueOut();
        nFee = (nDebit - nValueOut);
    }

    // Sent/received.
    for(unsigned int i = 0; i < tx->vout.size(); ++i)
    {
        const CTxOut& txout = tx->vout[i];
        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if(nDebit > Amount(0))
        {
            // Don't report 'change' txouts
            if(pwallet->IsChange(txout))
            {
                continue;
            }
        }
        else if(!(fIsMine & filter))
        {
            continue;
        }

        // In either case, we need to get the destination address.
        CTxDestination address;

        if(!CWallet::ExtractDestination(txout.scriptPubKey, address) &&
           !txout.scriptPubKey.IsKnownOpReturn())
        {
            LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, "
                      "txid %s\n",
                      this->GetId().ToString());
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent"
        // entry.
        if(nDebit > Amount(0))
        {
            listSent.push_back(output);
        }

        // If we are receiving the output, add it as a "received" entry.
        if(fIsMine & filter)
        {
            listReceived.push_back(output);
        }
    }
}

bool CWalletTx::RelayWalletTransaction(CConnman* connman)
{
    assert(pwallet->GetBroadcastTransactions());
    if(IsCoinBase() || isAbandoned() || GetDepthInMainChain() != 0)
    {
        return false;
    }

    CValidationState state;
    // GetDepthInMainChain already catches known conflicts.
    if(InMempool() || SubmitTxToMempool(maxTxFee, state))
    {
        if(connman)
        {
            TxMempoolInfo txinfo{};
            if(InMempool())
            {
                txinfo = mempool.Info(GetId());
            }
            else if(InNonFinalMempool())
            {
                txinfo = mempool.getNonFinalPool().getInfo(GetId());
            }

            if(!txinfo.IsNull())
            {
                LogPrintf("Relaying wtx %s\n", GetId().ToString());
                connman->EnqueueTransaction({CInv{MSG_TX, GetId()}, txinfo});
                return true;
            }
        }
    }

    return false;
}

std::set<uint256> CWalletTx::GetConflicts() const
{
    std::set<uint256> result;
    if(pwallet != nullptr)
    {
        uint256 myHash = GetId();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }

    return result;
}

Amount CWalletTx::GetDebit(const isminefilter& filter) const
{
    if(tx->vin.empty())
        return Amount(0);

    Amount debit(0);
    if(filter & ISMINE_SPENDABLE)
    {
        if(fDebitCached)
        {
            debit += nDebitCached;
        }
        else
        {
            nDebitCached = pwallet->GetDebit(*this, ISMINE_SPENDABLE);
            fDebitCached = true;
            debit += nDebitCached;
        }
    }

    if(filter & ISMINE_WATCH_ONLY)
    {
        if(fWatchDebitCached)
        {
            debit += nWatchDebitCached;
        }
        else
        {
            nWatchDebitCached = pwallet->GetDebit(*this, ISMINE_WATCH_ONLY);
            fWatchDebitCached = true;
            debit += Amount(nWatchDebitCached);
        }
    }

    return debit;
}

Amount CWalletTx::GetCredit(const isminefilter& filter) const
{
    // Must wait until coinbase is safely deep enough in the chain before
    // valuing it.
    if(IsCoinBase() && GetBlocksToMaturity() > 0)
    {
        return Amount(0);
    }

    // NOTE: Maturity of outputs created by confiscation transactions does not need to be
    // checked
    //       because these outputs are never sent directly to the final owner.

    Amount credit(0);
    if(filter & ISMINE_SPENDABLE)
    {
        // GetBalance can assume transactions in mapWallet won't change.
        if(fCreditCached)
        {
            credit += nCreditCached;
        }
        else
        {
            nCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
            fCreditCached = true;
            credit += nCreditCached;
        }
    }

    if(filter & ISMINE_WATCH_ONLY)
    {
        if(fWatchCreditCached)
        {
            credit += nWatchCreditCached;
        }
        else
        {
            nWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
            fWatchCreditCached = true;
            credit += nWatchCreditCached;
        }
    }

    return credit;
}

Amount CWalletTx::GetImmatureCredit(bool fUseCache) const
{
    if(IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if(fUseCache && fImmatureCreditCached)
            return nImmatureCreditCached;
        nImmatureCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
        fImmatureCreditCached = true;
        return nImmatureCreditCached;
    }

    return Amount(0);
}

Amount CWalletTx::GetAvailableCredit(bool fUseCache) const
{
    if(pwallet == 0)
    {
        return Amount(0);
    }

    // Must wait until coinbase is safely deep enough in the chain before
    // valuing it.
    if(IsCoinBase() && GetBlocksToMaturity() > 0)
    {
        return Amount(0);
    }

    if(fUseCache && fAvailableCreditCached)
    {
        return nAvailableCreditCached;
    }

    Amount nCredit(0);
    uint256 hashTx = GetId();
    for(unsigned int i = 0; i < tx->vout.size(); i++)
    {
        if(!pwallet->IsSpent(hashTx, i))
        {
            const CTxOut& txout = tx->vout[i];
            nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
            if(!MoneyRange(nCredit))
            {
                throw std::runtime_error(
                    "CWalletTx::GetAvailableCredit() : value out of range");
            }
        }
    }

    nAvailableCreditCached = nCredit;
    fAvailableCreditCached = true;
    return nCredit;
}

Amount CWalletTx::GetImmatureWatchOnlyCredit(const bool& fUseCache) const
{
    if(IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if(fUseCache && fImmatureWatchCreditCached)
        {
            return nImmatureWatchCreditCached;
        }

        nImmatureWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
        fImmatureWatchCreditCached = true;
        return nImmatureWatchCreditCached;
    }

    return Amount(0);
}

Amount CWalletTx::GetAvailableWatchOnlyCredit(const bool& fUseCache) const
{
    if(pwallet == 0)
    {
        return Amount(0);
    }

    // Must wait until coinbase is safely deep enough in the chain before
    // valuing it.
    if(IsCoinBase() && GetBlocksToMaturity() > 0)
    {
        return Amount(0);
    }

    if(fUseCache && fAvailableWatchCreditCached)
    {
        return nAvailableWatchCreditCached;
    }

    Amount nCredit(0);
    for(unsigned int i = 0; i < tx->vout.size(); i++)
    {
        if(!pwallet->IsSpent(GetId(), i))
        {
            const CTxOut& txout = tx->vout[i];
            nCredit += pwallet->GetCredit(txout, ISMINE_WATCH_ONLY);
            if(!MoneyRange(nCredit))
            {
                throw std::runtime_error(
                    "CWalletTx::GetAvailableCredit() : value out of range");
            }
        }
    }

    nAvailableWatchCreditCached = nCredit;
    fAvailableWatchCreditCached = true;
    return nCredit;
}

Amount CWalletTx::GetChange() const
{
    if(fChangeCached)
    {
        return nChangeCached;
    }

    nChangeCached = pwallet->GetChange(*this);
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTx::InMempool() const { return mempool.Exists(GetId()); }

bool CWalletTx::InNonFinalMempool() const
{
    return mempool.getNonFinalPool().exists(GetId());
}

bool CWalletTx::IsTrusted() const
{
    // Quick answer in most cases
    if(!CheckFinalTx(*this,
                     chainActive.Height(),
                     // NOLINTNEXTLINE(*-narrowing-conversions)
                     chainActive.Tip()->GetMedianTimePast()))
    {
        return false;
    }

    int nDepth = GetDepthInMainChain();
    if(nDepth >= 1)
    {
        return true;
    }

    if(nDepth < 0)
    {
        return false;
    }

    // using wtx's cached debit
    if(!bSpendZeroConfChange || !IsFromMe(ISMINE_ALL))
    {
        return false;
    }

    // Don't trust unconfirmed transactions from us unless they are in the
    // mempool.
    if(!InMempool())
    {
        return false;
    }

    // Trusted if all inputs are from us and are in the mempool:
    for(const CTxIn& txin : tx->vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = pwallet->GetWalletTx(txin.prevout.GetTxId());
        if(parent == nullptr)
        {
            return false;
        }

        const CTxOut& parentOut = parent->tx->vout[txin.prevout.GetN()];
        if(pwallet->IsMine(parentOut) != ISMINE_SPENDABLE)
        {
            return false;
        }
    }

    return true;
}

bool CWalletTx::IsEquivalentTo(const CWalletTx& _tx) const
{
    CMutableTransaction tx1 = *this->tx;
    CMutableTransaction tx2 = *_tx.tx;
    for(unsigned int i = 0; i < tx1.vin.size(); i++)
    {
        tx1.vin[i].scriptSig = CScript();
    }

    for(unsigned int i = 0; i < tx2.vin.size(); i++)
    {
        tx2.vin[i].scriptSig = CScript();
    }

    return CTransaction(tx1) == CTransaction(tx2);
}
