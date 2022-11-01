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
    static enumTableT<miner_info_error>
        table{{miner_info_error::miner_info_ref_not_found,
               "miner info ref not found"},
              {miner_info_error::script_version_unsupported,
               "unsupported version"},
              {miner_info_error::invalid_instruction, "invalid instruction"},
              {miner_info_error::invalid_sig_len, "invalid signature length"},
              {miner_info_error::invalid_txid_len, "invalid txid length"},
              {miner_info_error::invalid_mmr_pbh_hash_len,
               "invalid hash(modified merkle root || previous block hash) "
               "length"},
              {miner_info_error::txid_not_found, "txid not found"},
              {miner_info_error::doc_output_not_found,
               "script output not found"},
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
              {miner_info_error::doc_parse_error_prev_miner_id_sig_verification_fail,
               "doc parse error - prevMinerIdSig verification fail"},
              {miner_info_error::doc_parse_error_invalid_revocation_key,
               "doc parse error - invalid revocationKey"},
              {miner_info_error::doc_parse_error_invalid_prev_revocation_key,
               "doc parse error - invalid prevRevocationKey"},
              {miner_info_error::
                   doc_parse_error_invalid_prev_revocation_key_sig,
               "doc parse error - invalid revocationMessageSig"},
              {miner_info_error::doc_parse_error_prev_rev_key_sig_verification_fail, 
               "doc parse error - prevRevocationKeySig verification fail"},
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
              {miner_info_error::doc_parse_error_sig1_verification_failed, 
               "doc parse error - revocation msg sig1 verification failed"},
              {miner_info_error::doc_parse_error_rev_msg_sig2,
               "doc parse error - revocation msg sig2 field missing"},
              {miner_info_error::doc_parse_error_rev_msg_sig2_key,
               "doc parse error - revocation msg sig2 invalid value"},
              {miner_info_error::doc_parse_error_sig2_verification_failed, 
               "doc parse error - revocation msg sig2 verification failed"},
              {miner_info_error::doc_parse_error_datarefs_invalid_datarefs_type,
               "doc parse error - invalid dataRefs object"},
              {miner_info_error::doc_parse_error_datarefs_invalid_refs_type,
               "doc parse error - invalid dataRefs refs object"},
              {miner_info_error::doc_parse_error_datarefs_invalid_dataref_type,
               "doc parse error - invalid dataRefs dataref type"},
              {miner_info_error::doc_parse_error_datarefs_dataref_missing_fields,
               "doc parse error - invalid dataRefs dataref missing fields"},
              {miner_info_error::doc_parse_error_datarefs_invalid_ref_field_type,
               "doc parse error - dataRefs invalid ref field name"},
              {miner_info_error::doc_parse_error_datarefs_refs_brfcid_type,
               "doc parse error - dataRefs refs brfcids type"},
              {miner_info_error::doc_parse_error_datarefs_refs_brfcid_field_type,
               "doc parse error - dataRefs refs brfcids field type"},
              {miner_info_error::doc_parse_error_datarefs_refs_txid_type,
               "doc parse error - dataRefs refs txid type"},
              {miner_info_error::doc_parse_error_datarefs_refs_vout_type,
               "doc parse error - dataRefs refs vout type"},
              {miner_info_error::doc_parse_error_datarefs_refs_compress_type,
               "doc parse error - dataRefs refs compress type"},
              {miner_info_error::block_bind_hash_mismatch, 
               "block bind - hash mismatch"}, 
              {miner_info_error::block_bind_sig_verification_failed,
               "block bind - signature verification failed"},
              {miner_info_error::brfcid_invalid_length,
               "brfcid invalid length"},
              {miner_info_error::brfcid_invalid_content,
               "brfcid invalid content"},
              {miner_info_error::brfcid_invalid_value_type,
               "brfcid invalid value type"},
    };
    return table;
}

void log_parse_error(const miner_info_error error,
                     const string& txid,
                     const size_t n,
                     const std::string& additional_info)
{
    LogPrint(BCLog::MINERID,
             "Invalid MinerInfo: %s, coinbase txid: %s and output index: %d. %s\n",
             enum_cast<std::string>(error),
             txid,
             n,
             additional_info);
}
