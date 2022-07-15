// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <numeric>

#include <boost/test/unit_test.hpp>
#include <boost/algorithm/hex.hpp>

#include "miner_id/miner_info_doc.h"
#include "script/instruction_iterator.h"
#include "script/opcodes.h"

using namespace std;

namespace
{    
    enum class json_value_type
    {
        string,
        number,
        object
    };

    vector<pair<string, json_value_type>>
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
    const string compressed_key_2{[]{ return string{"02"} + string(64, '0');}()};
    const string compressed_key_3{[]{ return string{"03"} + string(64, '0');}()};
    const string sig_0(142, '0');
    const string sig_1(142, '1');
    const vector<string> good_values{version,
                                     height,
                                     compressed_key_2, // minerId
                                     compressed_key_3, // prevMinerId
                                     sig_0,            // prevMinerIdSig
                                     compressed_key_3, // revocationKey
                                     compressed_key_2, // prevRevocationKey
                                     sig_1};           // prevRevocationKeySig
    
    vector<pair<string_view, json_value_type>>
        optional_fields{make_pair("revocationMessage", json_value_type::string)};
    
    constexpr int h{1234};
    const key_set mi_keys{compressed_key_2, compressed_key_3, sig_0};
    const key_set rev_keys{compressed_key_3, compressed_key_2, sig_1};
    const miner_info_doc mi_doc{miner_info_doc::v0_3,
                                h,
                                mi_keys,
                                rev_keys};

    template<typename T>
    std::string to_json(T first, T last)
    {
        bool first_pass{true};
        string doc{R"({ )"};
        doc = accumulate(first,
                         last,
                         doc,
                         [&first_pass](auto& doc, const auto& x) {
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
    
    template<typename T, typename U>
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

        const auto status = ParseMinerInfoScript(script); 
        BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
        BOOST_CHECK_EQUAL(expected, get<miner_info_error>(status));
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
    const key_set mi_keys{compressed_key_2, compressed_key_3, sig_0};
    const key_set rev_keys{compressed_key_3, compressed_key_2, sig_1};

    const miner_info_doc expected{miner_info_doc::v0_3,
                                  height,
                                  rev_keys,
                                  rev_keys};
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
        
    const auto status = ParseMinerInfoScript(script);
    BOOST_CHECK(std::holds_alternative<mi_doc_sig>(status));
    const auto [raw_mi_doc, mi_doc, mi_sig] = get<mi_doc_sig>(status);
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
    const auto status = ParseMinerInfoDoc(doc);
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_ill_formed_json,
                      std::get<miner_info_error>(status));
}

                        // name, type, value
using json_field_type = tuple<string, json_value_type, string>;
using json_fields_type = vector<json_field_type>;

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_required_fields)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    // Test for all but one field, for each field
    for(size_t i{}; i < required_fields.size(); ++i)
    {
        const string doc = to_json(next(fields.cbegin()), fields.cend());
        const auto status = ParseMinerInfoDoc(doc);   
        BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
        BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_missing_fields,
                          std::get<miner_info_error>(status));

        rotate(fields.begin(), next(fields.begin()), fields.end());
    }
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_bad_version)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_unsupported_version,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_bad_height)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_height,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_miner_id)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_miner_id,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_miner_id)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_miner_id,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_miner_id_sig)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_miner_id_sig,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_revocation_key)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_revocation_key,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_revocation_key)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_revocation_key,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_invalid_prev_revocation_key_sig)
{
    vector<string> values{good_values};
 
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_invalid_prev_revocation_key_sig,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_only)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_fields,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_sig_only)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });

    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                R"("sig1" : "42", "sig2" : "42")"));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_fields,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_no_compromised_minerId_field)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_field,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_key)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_key,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig1)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig1,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig1_key)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig1_key,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig2)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    oss << R"("sig1" : ")" << sig_0 << R"(", "INVALID" : "42")";
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                oss.str()));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig2,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_invalid_sig2_key)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    oss << R"("sig1" : ")" << sig_0 << R"(", "sig2" : "INVALID")";
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                oss.str()));
    
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_rev_msg_sig2_key,
                      std::get<miner_info_error>(status));
}

BOOST_AUTO_TEST_CASE(revocation_message_construction)
{
    const string compromised_miner_id{64, '1'};
    const string sig_1{64, '2'};
    const string sig_2{64, '3'};
    const revocation_msg msg{compromised_miner_id, sig_1, sig_2};
    BOOST_CHECK_EQUAL(compromised_miner_id, msg.compromised_miner_id());
    BOOST_CHECK_EQUAL(sig_1, msg.sig_1());
    BOOST_CHECK_EQUAL(sig_2, msg.sig_2());
}

BOOST_AUTO_TEST_CASE(revocation_message_equality)
{
    const string cmp_miner_id_1{64, '1'};
    const string sig_1_1{64, '2'};
    const string sig_2_1{64, '3'};
    const revocation_msg a{cmp_miner_id_1, sig_1_1, sig_2_1};
    BOOST_CHECK_EQUAL(a, a);
    
    const revocation_msg b{a};
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, a);

    const string cmp_miner_id_2{64, '4'};
    const revocation_msg c{cmp_miner_id_2, sig_1_1, sig_2_1};
    BOOST_CHECK_NE(a, c);
    
    const string sig_1_2{64, '5'};
    const revocation_msg d{cmp_miner_id_2, sig_1_2, sig_2_1};
    BOOST_CHECK_NE(c, d);
    
    const string sig_2_2{64, '6'};
    const revocation_msg e{cmp_miner_id_2, sig_1_2, sig_2_2};
    BOOST_CHECK_NE(d, e);
}

BOOST_AUTO_TEST_CASE(parse_revocation_msg_happy_case)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
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
    oss << R"("sig1" : ")" << sig_0 << R"(", "sig2" : ")" << sig_1 << R"(")";
    fields.push_back(make_tuple("revocationMessageSig",
                                json_value_type::object,
                                oss.str()));

    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_doc>(status));

    std::optional<revocation_msg> rev_msg{
        revocation_msg{compressed_key_2, sig_0, sig_1}};
    const miner_info_doc expected{miner_info_doc::v0_3,
                                  h,
                                  mi_keys,
                                  rev_keys,
                                  rev_msg};
    BOOST_CHECK_EQUAL(expected, std::get<miner_info_doc>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_doc_happy_case)
{
    json_fields_type fields;
    transform(required_fields.cbegin(),
              required_fields.cend(),
              good_values.cbegin(),
              back_inserter(fields),
              [](const auto& field, const auto& value) {
                  return make_tuple(field.first, field.second, value);
              });
    const string doc = to_json(fields.cbegin(), fields.cend());
    const auto status = ParseMinerInfoDoc(doc);   
    BOOST_CHECK(std::holds_alternative<miner_info_doc>(status));
    
    const miner_info_doc expected{mi_doc}; 
    BOOST_CHECK_EQUAL(expected, std::get<miner_info_doc>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_script_from_string)
{
    namespace ba = boost::algorithm;

    const string s{
        "006a04601dface01004dc2047b2276657273696f6e223a22302e33222c226865696768"
        "74223a34323433312c22707265764d696e65724964223a223033316164313332383437"
        "3661376666373930313637373562356363363664303238616636643634376461356338"
        "3632376531323636653661323039643364316565222c22707265764d696e6572496453"
        "6967223a22333034343032323036636332636230376337633063316131323836306665"
        "3139613165323232623239663964643930326365343861616131613564386565616465"
        "3434386234303230323230373337393965633137646631303264336161363036623463"
        "3032373930646232336237653239643339663264333861646234346337383936323433"
        "3334356564222c226d696e65724964223a223033316164313332383437366137666637"
        "3930313637373562356363363664303238616636643634376461356338363237653132"
        "3636653661323039643364316565222c22707265765265766f636174696f6e4b657922"
        "3a22303361306264653733346564363562323963383163373331336432653464336339"
        "626337313164326463323231383265396461643239653563373266636432636630222c"
        "227265766f636174696f6e4b6579223a22303264316139636639376130666531666630"
        "3163373233633336343133306332306561633336393565313338316438353437333238"
        "39323639336635346230306432222c22707265765265766f636174696f6e4b65795369"
        "67223a2233303435303232313030613639353837346532373364613737323338303837"
        "6132386137663939333737343030623863346138643330666365356631356134666539"
        "6662363038386638303232303164663862353233363930656533633361373231373033"
        "6566333639366134376238333665393366386463626333626432626463613737613064"
        "386132646666222c227265766f636174696f6e4d657373616765223a7b22636f6d7072"
        "6f6d697365645f6d696e65724964223a22303336333331666333653337326663386232"
        "3264653435653662626535326632376334666134616463313036396263613535343239"
        "35386139326131393935666138227d2c227265766f636174696f6e4d65737361676553"
        "6967223a7b2273696731223a2233303434303232303766653632353032376235343566"
        "6632333838316633626661323039303237396530316331333563323231656137316634"
        "3732396230653731636637616262313032323036303963333066363561303639376566"
        "3232313865643964353331643238316535636231373437313136303634306433313238"
        "65396162346436353230366163222c2273696732223a22333034353032323130306235"
        "3437303838623061303935396231393239336461303937323966316430613736316534"
        "3434356134386234646437336263646332356633643264613232323032323036333961"
        "6232613166363863656566633535343231316230383830333239326439373439383438"
        "37353932336263303437366662636163316365303432313761227d2c226d696e657243"
        "6f6e74616374223a7b22656d61696c223a226d696e696e67406d696e696e673033446f"
        "6d61696e2e636f6d222c226e616d65223a226d696e696e6730332d6f70656e2d6d696e"
        "6572227d7d473045022100cc5b54c3ac5dc082505c3644aa3567dedcd7a422c72a2e37"
        "5a017d1202d3b408022003053ad1843b5238fc6c7b7a025ffa697646471f7029495409"
        "953265ce11f783"};

    vector<uint8_t> script;
    ba::unhex(s.begin(), s.end(), back_inserter(script));

    const auto var_mi_doc_sig = ParseMinerInfoScript(script);
    BOOST_CHECK(std::holds_alternative<mi_doc_sig>(var_mi_doc_sig));
    const auto [raw_mi_doc, mi_doc, mi_sig] = get<mi_doc_sig>(var_mi_doc_sig);
    //cout << mi_doc << endl;
}

BOOST_AUTO_TEST_SUITE_END()
