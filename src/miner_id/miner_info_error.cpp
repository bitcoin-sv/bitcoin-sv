// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "miner_info_error.h"

#include "logging.h"

using namespace std;

std::ostream& operator<<(std::ostream& os, const miner_info_error e) 
{
    static const auto table = enumTable(e);
    os << table.castToString(e);
    return os;
}

const enumTableT<miner_info_error>& enumTable(miner_info_error)
{
    static enumTableT<miner_info_error> table{
        {miner_info_error::script_version_unsupported, "unsupported version"},
        {miner_info_error::invalid_instruction, "invalid instruction"},
        {miner_info_error::invalid_sig_len, "invalid signature length"},
        {miner_info_error::invalid_txid_len, "invalid txid length"},
        {miner_info_error::invalid_prev_block_hash_len, "invalid previous block hash length"},
        {miner_info_error::invalid_mm_root_len, "invalid modified merkle root length"},
        {miner_info_error::txid_not_found, "txid not found"},
        {miner_info_error::doc_output_not_found, "script output not found"},
        {miner_info_error::doc_parse_error_ill_formed_json,
         "doc parse error - ill-formed json"},
        {miner_info_error::doc_parse_error_missing_fields,
         "doc parse error - missing fields"},
        {miner_info_error::doc_parse_error_invalid_string_type,
         "doc parse error - invalid string type"},
        {miner_info_error::doc_parse_error_invalid_number_type,
         "doc parse error - invalid number type"},
        {miner_info_error::doc_parse_error_unsupported_version,
         "doc parse error - unsupported version"},
        {miner_info_error::doc_parse_error_invalid_height,
         "doc parse error - invalid height"},
        {miner_info_error::doc_parse_error_invalid_miner_id,
         "doc parse error - invalid minerId"},
        {miner_info_error::doc_parse_error_invalid_prev_miner_id,
         "doc parse error - invalid prevMinerId"},
        {miner_info_error::doc_parse_error_invalid_prev_miner_id_sig,
         "doc parse error - invalid prevMinerId signature"},
        {miner_info_error::doc_parse_error_invalid_revocation_key,
         "doc parse error - invalid revocationKey"},
        {miner_info_error::doc_parse_error_invalid_prev_revocation_key,
         "doc parse error - invalid prevRevocationKey"},
        {miner_info_error::doc_parse_error_invalid_prev_revocation_key_sig,
         "doc parse error - invalid revocationMessageSig"},
        {miner_info_error::doc_parse_error_rev_msg_fields,
         "doc parse error - revocation msg fields"},
        {miner_info_error::doc_parse_error_rev_msg_field,
         "doc parse error - revocation msg field"},
        {miner_info_error::doc_parse_error_rev_msg_key,
         "doc parse error - revocation msg key"},
        {miner_info_error::doc_parse_error_rev_msg_sig1,
         "doc parse error - revocation msg sig1 field missing"},
        {miner_info_error::doc_parse_error_rev_msg_sig1_key,
         "doc parse error - revocation msg sig1 invalid value"},
        {miner_info_error::doc_parse_error_rev_msg_sig2,
         "doc parse error - revocation msg sig2 field missing"},
        {miner_info_error::doc_parse_error_rev_msg_sig2_key,
         "doc parse error - revocation msg sig2 invalid value"},
    };
    return table;
}

void log_parse_error(const miner_info_error error,
                     const string& txid,
                     const size_t n)
{
    LogPrint(BCLog::MINERID,
             "Invalid MinerInfo: %s, txid: %s and output index: %d.\n",
             enum_cast<std::string>(error),
             txid,
             n);
}

