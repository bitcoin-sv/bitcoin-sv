// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_IO_H
#define BITCOIN_CORE_IO_H

#include <string>
#include <vector>
#include <optional>
#include "rpc/jsonwriter.h"

class CBlock;
class CMutableTransaction;
class CScript;
class CTransaction;
class uint256;
class UniValue;

class CBlockDetailsData
{
public:
    int confirmations{ 0 };
    std::optional<int64_t> time;
    std::optional<int64_t> blockTime;
    std::optional<int64_t> blockHeight;
};


// core_read.cpp
CScript ParseScript(const std::string &s);
std::string ScriptToAsmStr(const CScript &script,
                           const bool fAttemptSighashDecode = false);
void ScriptToAsmStr(const CScript& script,
                    CTextWriter& textWriter, 
                    const bool fAttemptSighashDecode = false);
bool DecodeHexTx(CMutableTransaction &tx, const std::string &strHexTx);
bool DecodeHexBlk(CBlock &, const std::string &strHexBlk);
uint256 ParseHashUV(const UniValue &v, const std::string &strName);
uint256 ParseHashStr(const std::string &, const std::string &strName);
std::vector<uint8_t> ParseHexUV(const UniValue &v, const std::string &strName);

// core_write.cpp
std::string FormatScript(const CScript &script);
std::string EncodeHexTx(const CTransaction &tx, const int serializeFlags = 0);
void EncodeHexTx(const CTransaction& tx, CTextWriter& writer, const int serializeFlags = 0);
void ScriptPubKeyToUniv(const CScript &scriptPubKey, bool fIncludeHex, bool isGenesisEnabled, UniValue &out);
void TxToJSON(const CTransaction& tx,
              const uint256& hashBlock,
              bool utxoAfterGenesis,
              const int serializeFlags,
              CJSONWriter& entry,
              const std::optional<CBlockDetailsData>& blockData = std::nullopt);
void ScriptPublicKeyToJSON(const CScript& scriptPubKey,
                           bool fIncludeHex,
                           bool isGenesisEnabled,
                           CJSONWriter& out);

#endif // BITCOIN_CORE_IO_H
