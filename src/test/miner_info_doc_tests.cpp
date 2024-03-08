// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <boost/test/tools/old/interface.hpp>
#include <numeric>

#include <boost/test/unit_test.hpp>
#include <sstream>
#include <variant>
#include "boost/algorithm/hex.hpp"

#include "miner_id/miner_info.h"
#include "miner_id/miner_info_error.h"
#include "script/instruction_iterator.h"
#include "script/opcodes.h"
#include "uint256.h"

using namespace std;

namespace
{    
    enum class json_value_type
    {
        string,
        number,
        object
    };

    const vector<pair<string, json_value_type>>
        required_fields{make_pair("version", json_value_type::string),
                        make_pair("height", json_value_type::number),
                        make_pair("minerId", json_value_type::string),
                        make_pair("prevMinerId", json_value_type::string),
                        make_pair("prevMinerIdSig", json_value_type::string),
                        make_pair("revocationKey", json_value_type::string),
                        make_pair("prevRevocationKey",
                                   json_value_type::string),
                        make_pair("prevRevocationKeySig",
                                   json_value_type::string)};
    
    const string version{"0.3"};
    const string height{"1234"};
    constexpr int h{1234};
    const string compressed_key_2{[]{ return string{"02"} + string(64, '0');}()};
    const string compressed_key_3{[]{ return string{"03"} + string(64, '0');}()};

    const string miner_id{
        "031ad1328476a7ff79016775b5cc66d028af6d647da5c8627e1266e6a209d3d1ee"};
    const string prev_miner_id{
        "03f08b8eaa43fd93f650a3f4e270c501d061d4ba39e7e9c2367cc1f41fe7d763a9"};
    const string prev_miner_id_sig{
        "304402207e30b01e4a8eae62b9d7d5e35aa6bc4786ead2efa3ffbfee4243652ed71e60"
        "c302205b95222e9e646ac214ffaa348a6ffd509e84f4172bb4bc89e3ef90d40310e3ee"};

    const string rev_key{
        "02d1a9cf97a0fe1ff01c723c364130c20eac3695e1381d854732892693f54b00d2"};
    const string prev_rev_key{
        "03a0bde734ed65b29c81c7313d2e4d3c9bc711d2dc22182e9dad29e5c72fcd2cf0"};
    const string prev_rev_key_sig{
        "3045022100a695874e273da77238087a28a7f99377400b8c4"
        "a8d30fce5f15a4fe9fb6088f802201df8b523690ee3c3a721"
        "703ef3696a47b836e93f8dcbc3bd2bdca77a0d8a2dff"};
    
    const vector<string> required_values{version,
                                         height,
                                         miner_id,
                                         prev_miner_id,
                                         prev_miner_id_sig,
                                         rev_key,
                                         prev_rev_key,
                                         prev_rev_key_sig};

    const vector<pair<string, json_value_type>> optional_fields{
        make_pair("revocationMessage", json_value_type::object),
        make_pair("revocationMessageSig", json_value_type::object),
        make_pair("datarefs", json_value_type::object)};
    
    const string rev_msg{R"("compromised_minerId") : ")"};
    const string comp_miner_id{
        "03f08b8eaa43fd93f650a3f4e270c501d061d4ba39e7e9c2367cc1f41fe7d763a9"};
    const string sig_1{"3044022065d23509e353b516dbe1cd62e2aa1f2dcfe6d4264a2c0c4"
                       "e3b91d62976154f3f022004abc96a1c5a60a8658887ac25c9d66181"
                       "7d0b9ce778ff18a1a4f1ab2eec0ea0"};
    const string sig_2{"3045022100a26745be5035f154c26850222639e0ed3f8c08d117495"
                       "bbbaaeb646d9d79d182022077935c701643d42e2405da945c583c53"
                       "9f5d358496258ea05f0252c630f40fee"};

    const string refs{""};
    const vector<string>
        optional_values{[] {
                            ostringstream oss;
                            oss << R"("compromised_minerId" : ")"
                                << comp_miner_id << R"(")";
                            return oss.str();
                        }(),
                        []{
                            ostringstream oss;
                            oss << R"("sig1" : ")" << sig_1 << R"(", )"
                                << R"("sig2" : ")" << sig_2 << R"(")";
                            return oss.str();
                        }(),
                        []{
                            return refs;
                        }()};

    const key_set mi_keys{miner_id, prev_miner_id, prev_miner_id_sig};
    const key_set rev_keys{rev_key, prev_rev_key, prev_rev_key_sig};
    const vector<data_ref> data_refs;
    const miner_info_doc mi_doc{miner_info_doc::v0_3,
                                h,
                                mi_keys,
                                rev_keys,
                                data_refs};

    const string sig_bad_0{[] {
        string s{"304502"};
        s.insert(s.end(), 136, '0');
        return s;
    }()};

    const string sig_bad_1{[] {
        string s{"304502"};
        s.insert(s.end(), 136, '1');
        return s;
    }()};
    
    template <typename T>
    std::string to_json(T first, T last)
    {
        bool first_pass{true};
        string doc{R"({ )"};
        doc = accumulate(first,
                         last,
                         doc,
                         [&first_pass](auto&& doc, const auto& x) {
                             const auto& [name, type, value] = x;
                             if(first_pass)
                                 first_pass = false;
                             else
                                 doc += ", ";
                             doc += R"(")";
                             doc += name;
                             doc += R"(" : )";
                             if(type == json_value_type::string)
                             {
                                 doc += R"(")";
                                 doc += value;
                                 doc += R"(")";
                             }
                             else if(type == json_value_type::number)
                             {
                                 doc += value;
                             }
                             else if(type == json_value_type::object)
                             {
                                 doc += R"({ )";
                                 doc += value;
                                 doc += R"( })";
                             }
                             return doc;
                         });
        doc.append("}");
        return doc;
    }

    template <typename T, typename U>
    void concat(const T& src, U& dst)
    {
        if(src.size() < OP_PUSHDATA1)
        {
            dst.insert(dst.end(), uint8_t(src.size()));
        }
        else if(src.size() <= 0xff)
        {
            dst.insert(dst.end(), OP_PUSHDATA1);
            dst.insert(dst.end(), uint8_t(src.size()));
        }
        else if(src.size() <= 0xffff)
        {
            dst.insert(dst.end(), OP_PUSHDATA2);
            uint8_t data[2];
            WriteLE16(data, src.size());
            dst.insert(dst.end(), data, data + sizeof(data));
        }
        else
        {
            dst.insert(dst.end(), OP_PUSHDATA4);
            uint8_t data[4];
            WriteLE32(data, src.size());
            dst.insert(dst.end(), data, data + sizeof(data));
        }
        dst.insert(dst.end(), src.begin(), src.end());
    }
}

BOOST_AUTO_TEST_SUITE(miner_info_doc_tests)

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_script_failure_cases)
{
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4) 
    // 7 pushdata 1 (1)
    // 8 version (1)
    // 9 pushdata len(miner-info-doc) (1-9) 
    // ? miner-info-doc (len(miner-info-doc))
    // ? pushdata 69-72 (1)
    // ? sig(miner-info-doc) (69-72)
    // Total >= ? elements

    const auto mi_doc_str = to_json(mi_doc);

    // version, sig_len_offset, expected result
    const vector<tuple<uint8_t, uint8_t, miner_info_error>> v{
        make_tuple(1, 0, miner_info_error::script_version_unsupported),
        make_tuple(0, -2, miner_info_error::invalid_sig_len),
        make_tuple(0, +3, miner_info_error::invalid_sig_len),
    };
    for(const auto& [version, sig_len_offset, expected] : v)
    {
        vector<uint8_t> script{0x0, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce};
        script.push_back(1); // version length
        script.push_back(version);
        concat(mi_doc_str, script);     

        constexpr uint8_t sig_len{70};
        script.push_back(sig_len + sig_len_offset);
        generate_n(back_inserter(script), sig_len + sig_len_offset, [](){ return 0x42; });

        const auto var_mi_doc_sig = ParseMinerInfoScript(script); 
        BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc_sig));
        BOOST_CHECK_EQUAL(expected, get<miner_info_error>(var_mi_doc_sig));
    }
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_script_happy_case)
{
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4) 
    // 7 pushdata 1 (1)
    // 8 version (1)
    // 9 pushdata len(miner-info-doc) (1-9) 
    // ? miner-info-doc (len(miner-info-doc))
    // ? pushdata 71-73 (1)
    // ? sig(miner-info-doc) (71-73)
    // Total >= ? elements

    vector<uint8_t> script{0x0, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x0};

    constexpr auto height{1234};
    const miner_info_doc expected{miner_info_doc::v0_3,
                                  height,
                                  rev_keys,
                                  rev_keys,
                                  vector<data_ref>{}};
    const string mi_str = to_json(expected);
    vector<uint8_t> mi_script;
    transform(mi_str.begin(),
              mi_str.end(),
              back_inserter(mi_script),
              [](const auto c) { return c; });

    concat(mi_script, script);
  
    const vector<uint8_t> sig(71, 0x42); 
    script.push_back(sig.size());
    script.insert(script.end(), sig.begin(), sig.end()); 
        
    const auto var_mi_doc_sig = ParseMinerInfoScript(script);
    BOOST_CHECK(std::holds_alternative<mi_doc_sig>(var_mi_doc_sig));
    const auto [raw_mi_doc, mi_doc, mi_sig] = get<mi_doc_sig>(var_mi_doc_sig);
    BOOST_CHECK_EQUAL(mi_str, raw_mi_doc);
    BOOST_CHECK_EQUAL(expected, mi_doc);
    BOOST_CHECK_EQUAL_COLLECTIONS(sig.begin(),
                                  sig.end(),
                                  mi_sig.begin(),
                                  mi_sig.end());
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_ill_formed_json)
{
    string_view doc{"{"};
    const auto var_mi_doc = ParseMinerInfoDoc(doc);
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_ill_formed_json,
                      std::get<miner_info_error>(var_mi_doc));
}

                        // name, type, value
using json_field_type = tuple<string, json_value_type, string>;
using json_fields_type = vector<json_field_type>;

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_required_fields)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    // Test for all but one field, for each field
    for(size_t i{}; i < required_fields.size(); ++i)
    {
        const string doc = to_json(next(fields.cbegin()), fields.cend());
        const auto var_mi_doc = ParseMinerInfoDoc(doc);   
        BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
        BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_missing_fields,
                          std::get<miner_info_error>(var_mi_doc));

        rotate(fields.begin(), next(fields.begin()), fields.end());
    }
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_bad_version)
{
    vector<string> values{required_values};
 
    constexpr string_view bad_version{"0.2"};
    values[0] = bad_version; 

    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_unsupported_version,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_bad_height)
{
    vector<string> values{required_values};
 
    constexpr string_view bad_height{"-1"};
    values[1] = bad_height; 
  
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_height,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_miner_id)
{
    vector<string> values{required_values};
 
    constexpr string_view too_short{"bad1"}; 
    values[2] = too_short; 
  
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_miner_id,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_miner_id)
{
    vector<string> values{required_values};
 
    constexpr string_view invalid{"bad1"}; 
    values[3] = invalid; 
  
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_miner_id,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_miner_id_sig)
{
    vector<string> values{required_values};
 
    constexpr string_view invalid{"bad1"}; 
    values[4] = invalid; 
  
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_miner_id_sig,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(
    parse_miner_info_doc__prevMinerIDSig_verification_fail)
{
    vector<string> values{required_values};

    values[4] = sig_bad_0; 

    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_prev_miner_id_sig_verification_fail,
                      get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_revocation_key)
{
    vector<string> values{required_values};
 
    constexpr string_view invalid{"bad1"}; 
    values[5] = invalid; 
  
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_revocation_key,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_revocation_key)
{
    vector<string> values{required_values};
 
    constexpr string_view invalid{"bad1"}; 
    values[6] = invalid; 
  
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_revocation_key,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_revocation_key_sig)
{
    vector<string> values{required_values};
 
    constexpr string_view invalid{"bad1"}; 
    values[7] = invalid; 
  
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(var_mi_doc));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_revocation_key_sig,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_prevRevSig_verification_fail)
{
    vector<string> values{required_values};

    values[7] = sig_bad_0; 

    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_prev_rev_key_sig_verification_fail,
                      get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_rev_msg_is_not_an_object)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    ostringstream oss;
    oss << R"(INVALID - NOT A JSON OBJECT)";

    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::string,
                                oss.str()));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_fields,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_only)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    ostringstream oss;
    oss << R"("compromised_minerId" : ")" << string(64, '1') << R"(")";

    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::object,
                                oss.str()));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_fields,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_sig_only)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                R"("sig1" : "42", "sig2" : "42")"));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_fields,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_no_compromised_minerId_field)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::object,
                                R"("INVALID" : "42")"));
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                R"("sig1" : "42", "sig2" : "42")"));

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_field,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_key)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::object,
                                R"("compromised_minerId" : "INVALID")"));
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                R"("sig1" : "42", "sig2" : "42")"));

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_key,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig1)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    
    ostringstream oss;
    oss << R"("compromised_minerId" : ")" << compressed_key_2 << R"(")";
    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::object,
                                oss.str()));
    
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                R"("INVALID" : "42", "sig2" : "42")"));

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig1,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig1_key)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    
    ostringstream oss;
    oss << R"("compromised_minerId" : ")" << compressed_key_2 << R"(")";
    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::object,
                                oss.str()));
    
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                R"("sig1" : "INVALID", "sig2" : "42")"));

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig1_key,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig2)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    
    ostringstream oss;
    oss << R"("compromised_minerId" : ")" << compressed_key_2 << R"(")";
    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::object,
                                oss.str()));
    oss.str("");
    oss << R"("sig1" : ")" << sig_bad_0 << R"(", "INVALID" : "42")";
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                oss.str()));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig2,
                      std::get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig2_key)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    
    ostringstream oss;
    oss << R"("compromised_minerId" : ")" << compressed_key_2 << R"(")";
    fields.push_back(make_tuple("revocationMessage",
                                json_value_type::object,
                                oss.str()));
    oss.str("");
    oss << R"("sig1" : ")" << sig_bad_0 << R"(", "sig2" : "INVALID")";
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                oss.str()));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig2_key,
                      std::get<miner_info_error>(var_mi_doc));
}

const auto compressed_key_init = [](char c) {
    string s{"02"};
    s.insert(s.end(), 64, c);
    return s;
};

BOOST_AUTO_TEST_CASE(revocation_message_construction)
{
    const string compromised_miner_id{compressed_key_init('1')};
    const revocation_msg msg{compromised_miner_id, sig_bad_0, sig_bad_1};
    BOOST_CHECK_EQUAL(compromised_miner_id, msg.compromised_miner_id());
    BOOST_CHECK_EQUAL(sig_bad_0, msg.sig_1());
    BOOST_CHECK_EQUAL(sig_bad_1, msg.sig_2());
}

BOOST_AUTO_TEST_CASE(revocation_message_equality)
{
    const string cmp_miner_id_1{compressed_key_init('1')};
    const revocation_msg a{cmp_miner_id_1, sig_bad_0, sig_bad_1};
    BOOST_CHECK_EQUAL(a, a);
    
    const revocation_msg b{a};
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, a);

    const string cmp_miner_id_2(compressed_key_init('4'));
    const revocation_msg c{cmp_miner_id_2, sig_bad_0, sig_bad_1};
    BOOST_CHECK_NE(a, c);
    
    const revocation_msg d{cmp_miner_id_1, sig_bad_1, sig_bad_1};
    BOOST_CHECK_NE(a, d);
    
    const revocation_msg e{cmp_miner_id_1, sig_bad_0, sig_bad_0};
    BOOST_CHECK_NE(a, e);
}

BOOST_AUTO_TEST_CASE(parse_revocation_sig_1_verification_fail)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    
    transform(optional_fields.cbegin(),
              next(optional_fields.cbegin()),
              optional_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value){
                  return make_tuple(field.first, field.second, value);
              });

    ostringstream oss;
    oss << R"("sig1" : ")" << sig_bad_0 << R"(", "sig2" : ")" << sig_bad_1 << R"(")";
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                oss.str()));

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_sig1_verification_failed,
                      get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_revocation_sig_2_verification_fail)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    
    transform(optional_fields.cbegin(),
              next(optional_fields.cbegin()),
              optional_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value){
                  return make_tuple(field.first, field.second, value);
              });

    ostringstream oss;
    oss << R"("sig1" : ")" << sig_1 << R"(", "sig2" : ")" << sig_bad_1 << R"(")";
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                oss.str()));

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_sig2_verification_failed,
                      get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_datarefs_invalid_json)
{
    using mie = miner_info_error;

    // clang-format off
    const vector<pair<string, miner_info_error>>
        test_data{
                  make_pair(R"({ "dataRefs" : "INVALID" })",
                            mie::doc_parse_error_datarefs_invalid_datarefs_type),
                  make_pair(R"({ "dataRefs" : { "refs" : "INVALID" } })",
                            mie::doc_parse_error_datarefs_invalid_refs_type),
                  make_pair(R"({ "dataRefs" : { "refs" : [ "INVALID" ] } })",
                            mie::doc_parse_error_datarefs_invalid_dataref_type),
                  make_pair(R"({ "dataRefs" : { "refs" : [ {} ] } })",
                            mie::doc_parse_error_datarefs_dataref_missing_fields),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : 42 } ] } })",
                            mie::doc_parse_error_datarefs_dataref_missing_fields),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : 42,
                                                             "txid" : 42 } ] } })",
                            mie::doc_parse_error_datarefs_dataref_missing_fields),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : 42,
                                                             "txid" : 42,
                                                             "vout" : "INVALID" } ] } })",
                            mie::doc_parse_error_datarefs_refs_brfcid_type),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : [ 42 ],
                                                             "txid" : "",
                                                             "vout" : 0 } ] } })",
                            mie::doc_parse_error_datarefs_refs_brfcid_field_type),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : [ "" ],
                                                             "txid" : 42,
                                                             "vout" : 0 } ] } })",
                            mie::doc_parse_error_datarefs_refs_txid_type),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : [ "" ],
                                                             "txid" : "",
                                                             "vout" : "INVALID" } ] } })",
                            mie::doc_parse_error_datarefs_refs_vout_type),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : [ "" ],
                                                             "txid" : "",
                                                             "vout" : 0,
                                                             "compress" : 0 } ] } })",
                            mie::doc_parse_error_datarefs_refs_compress_type),
                  make_pair(R"({ "dataRefs" : { "refs" : [ { "brfcIds" : [ "" ],
                                                             "txid" : "INVALID",
                                                             "vout" : 0,
                                                             "compress" : "" } ] } })",
                            mie::doc_parse_error_datarefs_refs_txid_type)
                 };
    // clang-format on

    for(const auto& [ip, expected] : test_data)
    {
        const auto var_data_refs = ParseDataRefs(ip);
        BOOST_CHECK_EQUAL(expected, get<miner_info_error>(var_data_refs));
    }
}

BOOST_AUTO_TEST_CASE(parse_datarefs_invalid_datarefs_type)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    fields.insert(fields.end(), make_tuple("dataRefs", json_value_type::string, "42"));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseDataRefs(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_datarefs_invalid_datarefs_type,
                      get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_datarefs_happy_case)
{
    ostringstream oss;
    oss << R"({ "dataRefs" : { "refs" : [ { "brfcIds" : [ "brfcid_1", "brfcid_2" ],
                                                    "txid" : ")";
    vector<uint8_t> v(32);
    iota(v.begin(), v.end(), 0);
    const uint256 expected_txid{v};

    oss << expected_txid << R"(", )";
    oss << R"("vout" : 1, "compress" : "gzip" } ] } })";

    const auto var_data_refs = ParseDataRefs(oss.str());

    const vector<string> expected_brfcids{"brfcid_1", "brfcid_2"};
    const vector<data_ref> expected{
        data_ref{expected_brfcids, expected_txid, 1, "gzip"}};
    const auto actual = get<::data_refs>(var_data_refs);
    BOOST_CHECK_EQUAL_COLLECTIONS(expected.cbegin(),
                                  expected.cend(),
                                  actual.cbegin(),
                                  actual.cend());
}

BOOST_AUTO_TEST_CASE(parse_datarefs_invalid_datarefs_refs_type)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    fields.insert(fields.end(), 
                  make_tuple("dataRefs", json_value_type::object, R"("refs" : 42)"));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseDataRefs(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_datarefs_invalid_refs_type,
                      get<miner_info_error>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_without_rev_msg_happy_case)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());

    const auto var_mi_doc = ParseMinerInfoDoc(doc);
    BOOST_CHECK(std::holds_alternative<miner_info_doc>(var_mi_doc));
    const miner_info_doc expected{mi_doc}; 
    BOOST_CHECK_EQUAL(expected, std::get<miner_info_doc>(var_mi_doc));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_with_rev_msg_happy_case)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    transform(optional_fields.cbegin(),
              optional_fields.cend(),
              optional_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value){
                  return make_tuple(field.first, field.second, value);
              });

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto var_mi_doc = ParseMinerInfoDoc(doc);
    BOOST_CHECK(std::holds_alternative<miner_info_doc>(var_mi_doc));
    const auto& mi_doc = get<miner_info_doc>(var_mi_doc);

    const key_set mi_keys{miner_id, prev_miner_id, prev_miner_id_sig};
    const key_set rev_keys{rev_key, prev_rev_key, prev_rev_key_sig};

    std::optional<revocation_msg> rev_msg{
        revocation_msg{comp_miner_id, sig_1, sig_2}};
    const miner_info_doc expected{miner_info_doc::v0_3, h, mi_keys, rev_keys,
                                  vector<data_ref>{},
                                  rev_msg};
    BOOST_CHECK_EQUAL(expected, mi_doc);
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_with_datarefs_happy_case)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              required_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    ostringstream oss;
    oss << R"("dataRefs" : { "refs" : [ { "brfcIds" : [ "brfcid_1", "brfcid_2" ],
                                                    "txid" : ")";
    vector<uint8_t> v(32);
    iota(v.begin(), v.end(), 0);
    const uint256 expected_txid{v};

    oss << expected_txid << R"(", )";
    oss << R"("vout" : 1, "compress" : "gzip" } ] } )";

    fields.push_back(make_tuple("extensions", json_value_type::object, oss.str()));
    const string doc = to_json(fields.cbegin(), fields.cend());

    const auto var_mi_doc = ParseMinerInfoDoc(doc);
    BOOST_CHECK(std::holds_alternative<miner_info_doc>(var_mi_doc));
    const auto& mi_doc = get<miner_info_doc>(var_mi_doc);

    const auto& data_refs = mi_doc.data_refs();
    BOOST_CHECK_EQUAL(1U, data_refs.size());

    const vector<string> expected_brfcids{"brfcid_1", "brfcid_2"};
    const vector<data_ref> expected{
        data_ref{expected_brfcids, expected_txid, 1, "gzip"}};
    BOOST_CHECK_EQUAL_COLLECTIONS(expected.cbegin(),
                                  expected.cend(),
                                  data_refs.cbegin(),
                                  data_refs.cend());
}

BOOST_AUTO_TEST_CASE(verify_data_script_failure_cases)
{
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4) 
    // 7 pushdata 1 (1)
    // 8 version (1)
    // 9 pushdata len(json_data_obj) (1-9) 
    // ? json_data_obj (len(json_data_obj))
    // ? pushdata 69-72 (1)
    // ? sig(miner-info-doc) (69-72)
    // Total >= ? elements
    
    vector<uint8_t> script{0x0, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1};

    script.push_back(1); // unsupported_version
    auto var = VerifyDataScript(script);
    BOOST_CHECK_EQUAL(miner_info_error::script_version_unsupported, get<miner_info_error>(var));

    script[script.size()-1] = 0; // supported version
    const string json{R"({ "123456789abc" : "INVALID" })"};
    vector<uint8_t> mi_script;
    transform(json.begin(),
              json.end(),
              back_inserter(mi_script),
              [](const auto c) { return c; });
    concat(mi_script, script);
  
    var = VerifyDataScript(script);
    BOOST_CHECK_EQUAL(miner_info_error::brfcid_invalid_value_type, get<miner_info_error>(var));
}

BOOST_AUTO_TEST_CASE(verify_data_script_happy_case)
{
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4) 
    // 7 pushdata 1 (1)
    // 8 version (1)
    // 9 pushdata len(json_data_obj) (1-9) 
    // ? json_data_obj (len(json_data_obj))
    // ? pushdata 69-72 (1)
    // ? sig(miner-info-doc) (69-72)
    // Total >= ? elements

    vector<uint8_t> script{0x0, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x0};

    const string json{R"({ "123456789abc" : {}})"};
    vector<uint8_t> mi_script;
    transform(json.begin(),
              json.end(),
              back_inserter(mi_script),
              [](const auto c) { return c; });

    concat(mi_script, script);
  
    const auto var = VerifyDataScript(script);
    BOOST_CHECK(get<bool>(var));
}

BOOST_AUTO_TEST_CASE(parse_dataref_objects_invalid_json)
{
    using mie = miner_info_error;

    // clang-format off
    const vector<pair<string, miner_info_error>>
        test_data{
                  make_pair(R"()",
                            mie::doc_parse_error_ill_formed_json),
                  make_pair(R"({"123456789ab": {}})",
                            mie::brfcid_invalid_length),
                  make_pair(R"({"123456789abcd": {}})",
                            mie::brfcid_invalid_length),
                  make_pair(R"({"123456789abz": {}})",
                            mie::brfcid_invalid_content),
                  make_pair(R"({"123456789abc": "INVALID"})",
                            mie::brfcid_invalid_value_type),
                 };
    // clang-format on

    for(const auto& [ip, expected] : test_data)
    {
        const auto var{VerifyDataObject(ip)};
        BOOST_CHECK_EQUAL(expected, get<miner_info_error>(var));
    }
}

BOOST_AUTO_TEST_CASE(parse_dataref_objects_happy_case)
{
    const auto s = R"({"123456789abc": {}})";
    const auto var{VerifyDataObject(s)};
    BOOST_CHECK(holds_alternative<bool>(var));
    BOOST_CHECK(get<bool>(var));
}

BOOST_AUTO_TEST_SUITE_END()
