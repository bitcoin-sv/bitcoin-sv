// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "coinbase_doc.h"

#include "hash.h"
#include "logging.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include <iterator>
#include <ostream>
#include <sstream>

using namespace std;

CoinbaseDocument::CoinbaseDocument(std::string_view rawJSON, const miner_info_doc& minerInfoDoc)
: mRawJSON { rawJSON },
  mVersion { "0.3" },
  mHeight { minerInfoDoc.GetHeight() },
  mPrevMinerId { minerInfoDoc.miner_id().prev_key() },
  mPrevMinerIdSig { minerInfoDoc.miner_id().prev_key_sig() },
  mMinerId { minerInfoDoc.miner_id().key() },
  mPrevRevocationKey { minerInfoDoc.revocation_keys().prev_key() },
  mRevocationKey { minerInfoDoc.revocation_keys().key() }
{
    const auto& revocationMsg { minerInfoDoc.revocation_message() };
    if(revocationMsg)
    {
        mRevocationMessage = { revocationMsg->compromised_miner_id() };
    }

    // Parse out minerContact
    UniValue doc { UniValue::VOBJ };
    doc.read(mRawJSON);
    const auto& contact { doc["minerContact"] };
    if(contact.isObject())
    {
        mMinerContact = contact;
    }

    // dataRefs
    const auto data_refs = minerInfoDoc.data_refs();
    if(!data_refs.empty())
    {
        vector<DataRef> v;
        transform(data_refs.cbegin(),
                  data_refs.cend(),
                  back_inserter(v),
                  [](const auto& ref) {
                      DataRef d;
                      d.txid = TxId{ref.txid()};
                      d.vout = ref.vout();
                      d.brfcIds = ref.brfc_ids();
                      d.compress = ref.compress();
                      return d;
                  });

        mDataRefs = v;
    }
}

bool operator==(const CoinbaseDocument::DataRef& a,
                const CoinbaseDocument::DataRef& b)
{
    // clang-format off
    return a.brfcIds.size() == b.brfcIds.size() &&
           std::equal(a.brfcIds.begin(), a.brfcIds.end(), b.brfcIds.begin()) &&
           a.txid == b.txid &&
           a.vout == b.vout;
    // clang-format on
}

bool operator==(const CoinbaseDocument::RevocationMessage& a,
                const CoinbaseDocument::RevocationMessage& b)
{
    // clang-format off
    return a.mCompromisedId == b.mCompromisedId;
    // clang-format on
}

bool operator==(const CoinbaseDocument& a, const CoinbaseDocument& b)
{
    // clang-format off
    return a.mVersion == b.mVersion && 
           a.mHeight == b.mHeight &&
           a.mPrevMinerId == b.mPrevMinerId &&
           a.mPrevMinerIdSig == b.mPrevMinerIdSig && 
           a.mMinerId == b.mMinerId &&
           a.mVctx == b.mVctx &&
           a.mPrevRevocationKey == b.mPrevRevocationKey &&
           a.mRevocationKey == b.mRevocationKey &&
           ((!a.mRevocationMessage && !b.mRevocationMessage) ||
            ((a.mRevocationMessage && b.mRevocationMessage) &&
             (a.mRevocationMessage.value() == b.mRevocationMessage.value()))) &&
           ((!a.mDataRefs && !b.mDataRefs) ||
            ((a.mDataRefs && b.mDataRefs) &&
             (a.mDataRefs.value() == b.mDataRefs.value()))) &&
           ((!a.mMinerContact && !b.mMinerContact) ||
            ((a.mMinerContact && b.mMinerContact) &&
             (a.mMinerContact->write() == b.mMinerContact->write())));
    // clang-format on
}

std::ostream& operator<<(std::ostream& os,
                         const CoinbaseDocument::DataRef& dataref)
{
    // clang-format off
    os << "txid: " << dataref.txid.ToString()
       << "\nvout: " << dataref.vout;
    // clang-format on

    for(const auto& brfcId : dataref.brfcIds)
        os << "\nbrfcs: " << brfcId;

    return os;
}

std::ostream& operator<<(std::ostream& os,
                         const CoinbaseDocument::RevocationMessage& msg)
{
    // clang-format off
    os << "compromised_minerId: " << msg.mCompromisedId;
    // clang-format on

    return os;
}

std::ostream& operator<<(std::ostream& os, const CoinbaseDocument& doc)
{
    // clang-format off
    os << "version: " << doc.mVersion
       << "\nheight: " << doc.mHeight
       << "\nprev_miner_id: " << doc.mPrevMinerId
       << "\nprev_miner_sig: " << doc.mPrevMinerIdSig
       << "\nminer_id: " << doc.mMinerId
       << "\nprev_revocation_key: " << doc.mPrevRevocationKey
       << "\nrevocation_key: " << doc.mRevocationKey
       << "\noutpoint: " << doc.mVctx;
    // clang-format on

    if(doc.mRevocationMessage)
        os << "\nrevocation_message: " << doc.mRevocationMessage.value();

    if(doc.mMinerContact)
        os << "\nminer_contact: " << doc.mMinerContact->write();

    if(!doc.mDataRefs)
        return os;

    for(const auto& dataref : doc.mDataRefs.value())
    {
        os << '\n' << dataref;
    }

    return os;
}

void to_json(std::ostream& os, const CoinbaseDocument::DataRef& dataRef)
{
    os << R"({ "brfcIds": [ )";
    for(size_t i{0}; i < dataRef.brfcIds.size(); ++i)
    {
        os << R"(")" << dataRef.brfcIds[i] << '"';
        if(i != dataRef.brfcIds.size() - 1)
            os << ',';
    }
    os << ']';

    os << R"(, "txid": ")" << dataRef.txid.GetHex() << '"';
    os << R"(, "vout": )" << dataRef.vout;
    os << '}';
}

void to_json(std::ostream& os,
             const vector<CoinbaseDocument::DataRef>& data_refs)
{
    os << R"(, "dataRefs": { "refs" : [ )";
    for(size_t i{}; i < data_refs.size(); ++i)
    {
        to_json(os, data_refs[i]);
        if(i != data_refs.size() - 1)
            os << ',';
    }
    os << "] }";
}

void to_json(std::ostream& os, const CoinbaseDocument& doc)
{
    // clang-format off
    os << '{' 
        << R"("version" : )" << '"' << doc.GetVersion() << '"' 
        << R"(, "height" : )" << doc.GetHeight()
        << R"(, "prevMinerId" : )" << '"' << doc.GetPrevMinerId() << '"' 
        << R"(, "prevMinerIdSig" : )" << '"' << doc.GetPrevMinerIdSig() << '"' 
        << R"(, "dynamicMinerId" : "")"
        << R"(, "minerId" : )" << '"' << doc.GetMinerId() << '"' 
        << R"(, "vctx" : )"
        << R"({ "txId": ")" << doc.GetVctx().GetTxId().GetHex() << '"'
        << R"(, "vout":)" << doc.GetVctx().GetN() << '}';

    const auto& dataRefs{doc.GetDataRefs()};
    if(dataRefs)
    {
        to_json(os, dataRefs.value());
    }
    // clang-format on

    os << '}';
}

std::string to_json(const CoinbaseDocument& doc)
{
    ostringstream oss;
    to_json(oss, doc);
    return oss.str();
}

