// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include <numeric>

#include "crypto/common.h"
#include "hash.h"
#include "script/script_num.h"
#include "streams.h"
#include "tinyformat.h"

uint256 CBlockHeader::GetHash() const {
    return SerializeHash(*this);
}

std::string CBlock::ToString() const {
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, "
                   "hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, "
                   "vtx=%u)\n",
                   GetHash().ToString(), nVersion, hashPrevBlock.ToString(),
                   hashMerkleRoot.ToString(), nTime, nBits, nNonce, vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++) {
        s << "  " << vtx[i]->ToString() << "\n";
    }
    return s.str();
}

size_t CBlockHeader::GetHeaderSize()
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

size_t CBlock::GetTransactionCount()
{
    return vtx.size();
}

size_t CBlock::GetSizeWithoutCoinbase()
{
    size_t size = GetHeaderSize();
    for (const CTransactionRef& tx : vtx)
    {
        if (!tx->IsCoinBase())
        {
            size += tx->GetTotalSize();
        }
    }
    return size;
}

int32_t CBlock::GetHeightFromCoinbase()
    const // Returns the block's height as specified in its coinbase transaction
{
    const CScript &sig = vtx[0]->vin[0].scriptSig;

    // Get length of height number
    if (sig.empty()) throw std::runtime_error("Empty coinbase scriptSig");
    uint8_t numlen = sig[0];

    // Parse height as CScriptNum
    if (numlen == OP_0)
        return 0;
    if ((numlen >= OP_1) && (numlen <= OP_16))
        return numlen - OP_1 + 1;

    if (sig.size() - 1 < numlen)
        throw std::runtime_error("Badly formatted height in coinbase");
    std::vector<unsigned char> heightScript(numlen);
    copy(sig.begin() + 1, sig.begin() + 1 + numlen, heightScript.begin());
    CScriptNum coinbaseHeight(heightScript, false, numlen);
    return coinbaseHeight.getint();
}

size_t ser_size(const CBlock& block)
{
    size_t total{sizeof(CBlockHeader)};
    total += cmpt_ser_size(block.vtx.size());
    return std::accumulate(block.cbegin(),
                           block.cend(),
                           total,
                           [](auto total, const auto& sp_tx) {
                               total += ser_size(*sp_tx);
                               return total;
                           });
}

std::ostream& operator<<(std::ostream& os, const CBlockHeader& header)
{
    os << "CBlockHeader: "
       << "\n\tnVersion: " << header.nVersion
       << "\n\thashPrevBlock: " << header.hashPrevBlock
       << "\n\thashMerkleRoot: " << header.hashPrevBlock
       << "\n\tnTime: " << header.nTime
       << "\n\tnBits: " << header.nBits
       << "\n\tnNonce: " << header.nNonce;
    return os;
}


