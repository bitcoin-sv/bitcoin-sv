// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <iosfwd>

#include "enum_cast.h"

enum class miner_info_error
{
    miner_info_ref_not_found,
    invalid_instruction,
    script_version_unsupported,
    invalid_txid_len,
    invalid_mmr_pbh_hash_len,
    invalid_sig_len,
    txid_not_found,
    doc_output_not_found,
    doc_parse_error_ill_formed_json,
    doc_parse_error_missing_fields,
    doc_parse_error_invalid_string_type,
    doc_parse_error_invalid_number_type,
    doc_parse_error_unsupported_version,
    doc_parse_error_invalid_height,
    doc_parse_error_invalid_miner_id,
    doc_parse_error_invalid_prev_miner_id,
    doc_parse_error_invalid_prev_miner_id_sig,
    doc_parse_error_prev_miner_id_sig_verification_fail,
    doc_parse_error_invalid_revocation_key,
    doc_parse_error_invalid_prev_revocation_key,
    doc_parse_error_invalid_prev_revocation_key_sig,
    doc_parse_error_prev_rev_key_sig_verification_fail,
    doc_parse_error_rev_msg_fields,
    doc_parse_error_rev_msg_field,
    doc_parse_error_rev_msg_key,
    doc_parse_error_rev_msg_sig1,
    doc_parse_error_rev_msg_sig1_key,
    doc_parse_error_sig1_verification_failed,
    doc_parse_error_rev_msg_sig2,
    doc_parse_error_rev_msg_sig2_key,
    doc_parse_error_sig2_verification_failed,
    doc_parse_error_datarefs_invalid_datarefs_type,
    doc_parse_error_datarefs_invalid_refs_type,
    doc_parse_error_datarefs_invalid_dataref_type,
    doc_parse_error_datarefs_dataref_missing_fields,
    doc_parse_error_datarefs_invalid_ref_field_type,
    doc_parse_error_datarefs_refs_brfcid_type,
    doc_parse_error_datarefs_refs_brfcid_field_type,
    doc_parse_error_datarefs_refs_txid_type,
    doc_parse_error_datarefs_refs_vout_type,
    doc_parse_error_datarefs_refs_compress_type,
    block_bind_hash_mismatch,
    block_bind_sig_verification_failed,
    brfcid_invalid_length,
    brfcid_invalid_content,
    brfcid_invalid_value_type,
    size
};

std::ostream& operator<<(std::ostream&, miner_info_error);

void log_parse_error(miner_info_error error,
                     const std::string& txid,
                     size_t n,
                     const std::string& additional_info = "");

const enumTableT<miner_info_error>& enumTable(miner_info_error);

