// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "miner_info_doc.h"

#include <variant>

#include "script/instruction_iterator.h"
#include "script/script.h"
#include "span.h"

#include "miner_info.h"

using namespace std;

miner_info_doc::miner_info_doc(supported_version version,
                               int32_t height,
                               const key_set& mi_keys,
                               const key_set& revocation_keys,
                               std::optional<revocation_msg> rev_msg)
    : version_(version),
      height_{height},
      miner_id_keys_{mi_keys},
      revocation_keys_{revocation_keys},
      rev_msg_{std::move(rev_msg)}
{
}

bool operator==(const miner_info_doc& a, const miner_info_doc& b)
{
    return a.version_ == b.version_ &&
           a.height_ == b.height_ && 
           a.miner_id_keys_ == b.miner_id_keys_ &&
           a.revocation_keys_ == b.revocation_keys_ &&
           a.rev_msg_ == b.rev_msg_;
}

std::ostream& operator<<(std::ostream& os, const miner_info_doc& mi)
{
    os << "version: " << mi.version_ 
       << "\nheight: " << mi.height_
       << "\nminer_id: " << mi.miner_id_keys_ 
       << "\nrevocation_key: " << mi.revocation_keys_;

    if(mi.rev_msg_)
        os << "\nrevocation_msg: " << mi.rev_msg_.value();

    return os;
}

namespace
{
    void to_json(ostream& os, const miner_info_doc& doc)
    {
        // clang-format off
        os << '{' 
            << R"("version" : )" << '"' << doc.version() << '"' 
            << R"(, "height" : )" << doc.GetHeight()

            << R"(, "minerId" : )" << '"' << doc.miner_id().key() << '"' 
            << R"(, "prevMinerId" : )" << '"' << doc.miner_id().prev_key() << '"' 
            << R"(, "prevMinerIdSig" : )" << '"' << doc.miner_id().prev_key_sig() << '"' 
                        
            << R"(, "revocationKey" : )" << '"' << doc.revocation_keys().key() << '"' 
            << R"(, "prevRevocationKey" : )" << '"' << doc.revocation_keys().prev_key() << '"' 
            << R"(, "prevRevocationKeySig" : )" << '"' << doc.revocation_keys().prev_key_sig() << '"' 
            << '}';
            
            // todo datarefs?
        // clang-format on
    }
}

std::string to_json(const miner_info_doc& doc)
{
    ostringstream oss;
    to_json(oss, doc);
    return oss.str();
}

key_set::key_set(const std::string& key,
                 const std::string& prev_key,
                 const std::string& prev_key_sig)
    : key_{key},
      prev_key_{prev_key},
      prev_key_sig_{prev_key_sig}
{
}

bool operator==(const key_set& a, const key_set& b)
{
    return a.key_ == b.key_ && 
           a.prev_key_ == b.prev_key_ &&
           a.prev_key_sig_ == b.prev_key_sig_;
}

std::ostream& operator<<(std::ostream& os, const key_set& t)
{
    os << "key: " << t.key_ 
       << "\nprev_key: " << t.prev_key_
       << "\nprev_key_sig: " << t.prev_key_sig_;
    return os;
}

revocation_msg::revocation_msg(const string& compromised_miner_id,
                               const string& sig_1,
                               const string& sig_2)
    : compromised_miner_id_{compromised_miner_id},
      sig_1_{sig_1},
      sig_2_{sig_2}
{
    //if(!is_hash_256(compromised_miner_id_))
    //    cout << compromised_miner_id << "\n";

    //assert(is_hash_256(compromised_miner_id));
}

bool operator==(const revocation_msg& a, const revocation_msg& b)
{
    return a.compromised_miner_id_ == b.compromised_miner_id_ &&
           a.sig_1_ == b.sig_1_ &&
           a.sig_2_ == b.sig_2_;
}

std::ostream& operator<<(ostream& os, const revocation_msg& msg)
{
    os << "compromised_miner_id_: " << msg.compromised_miner_id_
       << "\nsig_1: " << msg.sig_1_
       << "\nsig_2: " << msg.sig_2_;
    return os;
}

namespace
{
    using expected_revocation_msg = std::variant<revocation_msg, miner_info_error>;

    expected_revocation_msg ParseRevocationMsg(const UniValue& id_doc,
                                               const UniValue& sig_doc)
    {
        assert(id_doc.isObject());
        assert(sig_doc.isObject());
        
        const auto comp_minerId = id_doc["compromised_minerId"];
        if(!comp_minerId.isStr())
            return miner_info_error::doc_parse_error_rev_msg_field;

        const auto& key = comp_minerId.getValStr();
        if(!is_compressed_key(key))
            return miner_info_error::doc_parse_error_rev_msg_key;
        
        const auto sig1_field = sig_doc["sig1"];
        if(!sig1_field.isStr())
            return miner_info_error::doc_parse_error_rev_msg_sig1;

        const auto& sig1 = sig1_field.getValStr();
        if(!is_der_signature(sig1))
            return miner_info_error::doc_parse_error_rev_msg_sig1_key;

        const auto sig2_field = sig_doc["sig2"];
        if(!sig2_field.isStr())
            return miner_info_error::doc_parse_error_rev_msg_sig2;

        const auto& sig2 = sig2_field.getValStr();
        if(!is_der_signature(sig2))
            return miner_info_error::doc_parse_error_rev_msg_sig2_key;

        return revocation_msg{comp_minerId.getValStr(),
                              sig1_field.getValStr(),
                              sig2_field.getValStr()};
    }
}

std::variant<miner_info_doc, miner_info_error> ParseMinerInfoDoc(
    const std::string_view sv)
{
    enum class field_type
    {
        string,
        number
    };
    using data_type = pair<string, field_type>;
    const static std::array<data_type, 8>
        required_fields{make_pair("version", field_type::string),
                        make_pair("height", field_type::number),
                        make_pair("minerId", field_type::string),
                        make_pair("prevMinerId", field_type::string),
                        make_pair("prevMinerIdSig", field_type::string),
                        make_pair("revocationKey", field_type::string),
                        make_pair("prevRevocationKey", field_type::string),
                        make_pair("prevRevocationKeySig", field_type::string)};

    UniValue doc;
    if(!doc.read(sv.data(), sv.size()))
        return miner_info_error::doc_parse_error_ill_formed_json;

    // check all required fields are present
    if(any_of(required_fields.cbegin(),
              required_fields.cend(),
              [&doc](const auto& req_field) { return doc[req_field.first].isNull(); }))
        return miner_info_error::doc_parse_error_missing_fields;

    for(const auto& [req_name, req_type] : required_fields)
    {
        const auto& field = doc[req_name];
        switch(req_type)
        {
        case field_type::string:
            if(!field.isStr())
                return miner_info_error::doc_parse_error_invalid_string_type;
            break;
        case field_type::number:
            if(!field.isNum())
                return miner_info_error::doc_parse_error_invalid_number_type;
            break;
        default:
            assert(false);
        }
    }

    const auto version = doc["version"].getValStr();
    if(version != "0.3")
        return miner_info_error::doc_parse_error_unsupported_version;
    
    const auto height = doc["height"].get_int();
    if(height <= 0)
        return miner_info_error::doc_parse_error_invalid_height;

    const auto minerId = doc["minerId"].getValStr();
    if(!is_compressed_key(minerId))
        return miner_info_error::doc_parse_error_invalid_miner_id;

    const auto prevMinerId = doc["prevMinerId"].getValStr();
    if(!is_compressed_key(prevMinerId))
        return miner_info_error::doc_parse_error_invalid_prev_miner_id;
   
    const auto prevMinerIdSig = doc["prevMinerIdSig"].getValStr();
    if(!is_der_signature(prevMinerIdSig))
        return miner_info_error::doc_parse_error_invalid_prev_miner_id_sig;
    
    const auto revKey = doc["revocationKey"].getValStr();
    if(!is_compressed_key(revKey))
        return miner_info_error::doc_parse_error_invalid_revocation_key;
    
    const auto prevRevKey = doc["prevRevocationKey"].getValStr();
    if(!is_compressed_key(prevRevKey))
        return miner_info_error::doc_parse_error_invalid_prev_revocation_key;
    
    const auto prevRevKeySig = doc["prevRevocationKeySig"].getValStr();
    if(!is_der_signature(prevRevKeySig))
        return miner_info_error::doc_parse_error_invalid_prev_revocation_key_sig;

    const auto revMsg = doc["revocationMessage"];
    const auto revMsgSig = doc["revocationMessageSig"];
    std::optional<revocation_msg> rev_msg;
    if(revMsg.isObject() ^ revMsgSig.isObject())
        return miner_info_error::doc_parse_error_rev_msg_fields;
    else if(revMsg.isObject() && revMsgSig.isObject())
    {
        const auto var_revocation_msg = ParseRevocationMsg(revMsg, revMsgSig);
        if(std::holds_alternative<miner_info_error>(var_revocation_msg))
            return std::get<miner_info_error>(var_revocation_msg);
        else if(std::holds_alternative<revocation_msg>(var_revocation_msg))
            rev_msg = std::get<revocation_msg>(var_revocation_msg);
        else
            assert(false);
    }

    const key_set miner_id_ks{minerId, prevMinerId, prevMinerIdSig};
    const key_set revocation_ks{revKey, prevRevKey, prevRevKeySig};

    return miner_info_doc{miner_info_doc::v0_3,
                          height,
                          miner_id_ks,
                          revocation_ks,
                          rev_msg};
}

std::variant<mi_doc_sig, miner_info_error> ParseMinerInfoScript(
    const bsv::span<const uint8_t> script)
{
    assert(IsMinerInfo(script)); // programming error if false in calling code

    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4) 
    // 7 pushdata 1 (1)
    // 8 version (1)
    // 9 pushdata len(miner-info-doc) (1-9) 
    // x miner-info-doc (len(miner-info-doc) )
    // y pushdata (len(sig)) (1)
    // z sig(miner-info-doc) (len(sig))

    // miner_info_ref starts at 7th byte of the output message
    bsv::instruction_iterator it{script.last(script.size() - 7)};
    if(!it.valid())
        return miner_info_error::invalid_instruction;

    const auto operand{it->operand()};
    if(operand.size() != 1)
        return miner_info_error::script_version_unsupported;

    const auto version{operand[0]};
    if(version != 0)
        return miner_info_error::script_version_unsupported;
    
    ++it;
    if(!it.valid())
        return miner_info_error::invalid_instruction;

    const auto doc = bsv::to_sv(it->operand());
    const auto var_miner_info_doc = ParseMinerInfoDoc(doc);
    if(holds_alternative<miner_info_error>(var_miner_info_doc))
        return get<miner_info_error>(var_miner_info_doc);
    
    ++it;
    if(!it.valid())
        return miner_info_error::invalid_instruction; 
    
    if(!is_der_signature(it->operand()))
        return miner_info_error::invalid_sig_len;

    bsv::span<const uint8_t> sig{it->operand()};

    return make_tuple(doc, get<miner_info_doc>(var_miner_info_doc), sig);   
}

std::ostream& operator<<(std::ostream& os, miner_info_doc::supported_version v)
{
    switch(v)
    {
    case miner_info_doc::supported_version::v0_3:
        os << "0.3";
        break;
    default:
        assert(false);
    }
    return os;
}

