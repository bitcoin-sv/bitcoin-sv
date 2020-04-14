// Copyright (c) 2015 G. Andrew Stone
// Copyright (c) 2018-2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining.h"

#include "amount.h"
#include "config.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "hash.h"
#include "mining/candidates.h"
#include "mining/factory.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "tinyformat.h"
#include "util.h"
#include "validationinterface.h"
#include "validation.h"
#include "version.h"
#include "versionbits.h"
#include "utilstrencodings.h"
#include <iomanip>
#include <limits>
#include <queue>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

using namespace std;
using mining::CBlockTemplate;

namespace
{

/// mkblocktemplate is a modified/cut down version of the code from the RPC method getblocktemplate. 
/// It is currently only called from getminingcandidate, but getblocktemplate could be
/// modified to call a generic version of mkblocktemplate.
CMiningCandidateRef mkblocktemplate(const Config& config, bool coinbaseRequired)
{
    LOCK(cs_main);

    if (!g_connman) 
    {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    if ((g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) && !gArgs.IsArgSet("-standalone")) 
    {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Bitcoin is not connected!");
    }

    if (IsInitialBlockDownload()) 
    {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Bitcoin is downloading blocks...");
    }

    if(!mining::g_miningFactory)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No mining factory available");
    }

    auto assembler { mining::g_miningFactory->GetAssembler() };

    // Update block
    static CBlockIndex *pindexPrev = nullptr;
    static int64_t nStart = 0;
    static unsigned int nTransactionsUpdatedLast = std::numeric_limits<unsigned int>::max();
    static std::unique_ptr<CBlockTemplate> pblocktemplate { std::make_unique<CBlockTemplate>() };
    if (pindexPrev != chainActive.Tip() || assembler->GetTemplateUpdated() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5)) 
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Update other fields for tracking state of this candidate
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        nStart = GetTime();

        // Dummy script; the real one is either created by the wallet below, or will be replaced by one
        // from the miner when they submit the mining solution
        CScript coinbaseScriptPubKey { CScript() << OP_TRUE };
        if(coinbaseRequired)
        {
            std::shared_ptr<CReserveScript> coinbaseScript {nullptr};
            GetMainSignals().ScriptForMining(coinbaseScript);
 
            // If the keypool is exhausted, no script is returned at all.  Catch this.
            if (!coinbaseScript)
                throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
 
            // throw an error if no script was provided
            if (coinbaseScript->reserveScript.empty())
                throw JSONRPCError(RPC_INTERNAL_ERROR, "No coinbase script available (mining requires a wallet)");
            coinbaseScriptPubKey = coinbaseScript->reserveScript;
        }
        pblocktemplate = assembler->CreateNewBlock(coinbaseScriptPubKey, pindexPrev);

        if (!pblocktemplate) 
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to create a new block. Possibly out of memory.");
    }

    CBlockRef blockref = pblocktemplate->GetBlockRef();
    CBlock *pblock = blockref.get();

    // Update nTime
    UpdateTime(pblock, config, pindexPrev);
    pblock->nNonce = 0;

    // Create candidate and return it
    CMiningCandidateRef candidate  { mining::CMiningFactory::GetCandidateManager().Create(blockref) };
    return candidate;
}


std::vector<uint256> GetMerkleProofBranches(CBlockRef pblock)
{
    std::vector<uint256> ret;
    std::vector<uint256> leaves;
    int len = pblock->vtx.size();

    for (int i = 0; i < len; i++)
    {
        leaves.emplace_back(pblock->vtx[i]->GetHash());
    }

    return ComputeMerkleBranch(leaves, 0);
}

void CalculateNextMerkleRoot(uint256 &merkle_root, const uint256 &merkle_branch)
{
    // Append a branch to the root. Double SHA256 the whole thing:
    uint256 hash;
    CHash256()
        .Write(merkle_root.begin(), merkle_root.size())
        .Write(merkle_branch.begin(), merkle_branch.size())
        .Finalize(hash.begin());
    merkle_root = hash;
}

uint256 CalculateMerkleRoot(uint256 &coinbase_hash, const std::vector<uint256> &merkleProof)
{
    uint256 merkle_root = coinbase_hash;
    for (unsigned int i = 0; i < merkleProof.size(); i++)
    {
        CalculateNextMerkleRoot(merkle_root, merkleProof[i]);
    }
    return merkle_root;
}

// Create Mining-Candidate JSON to send to miner
UniValue MkMiningCandidateJson(bool coinbaseRequired, CMiningCandidateRef &candidate)
{
    UniValue ret(UniValue::VOBJ);
    CBlockRef block = candidate->GetBlock();

    mining::CMiningFactory::GetCandidateManager().RemoveOldCandidates();

    std::stringstream idstr {};
    idstr << candidate->GetId();
    ret.push_back(Pair("id", idstr.str()));

    ret.push_back(Pair("prevhash", block->hashPrevBlock.GetHex()));

    const CTransaction* cbtran = block->vtx[0].get();
    if(coinbaseRequired)
    {
        ret.push_back(Pair("coinbase", EncodeHexTx(*cbtran)));
    }
    ret.push_back(Pair("coinbaseValue", cbtran->vout[0].nValue.GetSatoshis()));

    ret.push_back(Pair("version", block->nVersion));
    ret.push_back(Pair("nBits", strprintf("%08x", block->nBits)));
    ret.push_back(Pair("time", block->GetBlockTime()));
    ret.push_back(Pair("height", block->GetHeightFromCoinbase()));

    // number of transactions including coinbase transaction
    ret.push_back(Pair("num_tx", static_cast<uint64_t>(block->GetTransactionCount())));
    ret.push_back(Pair("sizeWithoutCoinbase", static_cast<uint64_t>(block->GetSizeWithoutCoinbase())));

    // merkleProof:
    std::vector<uint256> brancharr = GetMerkleProofBranches(block);
    UniValue merkleProof(UniValue::VARR);
    for (const auto &i : brancharr)
    {
        merkleProof.push_back(i.GetHex());
    }
    ret.push_back(Pair("merkleProof", merkleProof));

    return ret;
}

/// RPC - Get a block candidate for a miner.
/// getminingcandidate is a simplified version of getblocktemplate.
/// Miners use getminingcandidate/getblocktemplate RPC calls to ask a full node for a block to mine.
/// blocktemplate returns the full block, getminingcandidate returns the blockheader (including the 
/// Merkle root) which is all the miner needs.
/// getblocktemplate has a number of control parameters that are not available in getminingcandidate.
UniValue getminingcandidate(const Config& config, const JSONRPCRequest& request) 
{
    if (request.fHelp || request.params.size() > 1)
    {
        throw std::runtime_error(
                    "getminingcandidate coinbase (optional, default false)\n"
                    "\nReturns Mining-Candidate protocol data.\n"
                    "\nArguments:\n"
                    "1. \"coinbase\"        (boolean, optional) True if a coinbase transaction is required in result"
                    "\nResult: (json string)\n"
                    "    {\n                         \n"
                    "        \"id\": n,                  (string) Candidate identifier for submitminingsolution\n"
                    "        \"prevhash\": \"xxxx\",     (hex string) Hash of the previous block\n"
                    "        \"coinbase\": \"xxxx\",     (optional hex string encoded binary transaction) Coinbase transaction\n"
                    "        \"version\": n,             (integer) Block version\n"
                    "        \"nBits\": \"xxxx\",        (hex string) Difficulty\n"
                    "        \"time\": n,                (integer) Block time\n"
                    "        \"height\": n,              (integer) Current Block Height\n"
                    "        \"num_tx\": n,              (integer) Number of transactions the current candidate has including coinbase transaction\n"
                    "        \"sizeWithoutCoinbase\": n, (integer) Size of current block candidate in bytes without coinbase transaction\n"
                    "        \"merkleProof\": [          (list of hex strings) Merkle branch for the block\n"
                    "                          xxxx,\n"
                    "                          yyyy,\n"
                    "                         ]\n"
                    "    }\n"
        );
    }

    bool coinbaseRequired {false};
    if (request.params.size() == 1 && !request.params[0].isNull())
    {
        coinbaseRequired = request.params[0].get_bool();
    }

    LOCK(cs_main);
    CMiningCandidateRef candidate { mkblocktemplate(config, coinbaseRequired) };
    return MkMiningCandidateJson(coinbaseRequired, candidate);
}

/// RPC - Return a successfully mined block 
UniValue submitminingsolution(const Config& config, const JSONRPCRequest& request) 
{
    UniValue rcvd;

    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error(
                "submitminingsolution \"<json string>\" \n"
                "\nAttempts to submit a new block to the network.\n"
                "\nJson Object should comprise of the following and must be escaped\n"
                "    {\n"
                "        \"id\": n,           (string) ID from getminingcandidate RPC\n"
                "        \"nonce\": n,        (integer) Miner generated nonce\n"
                "        \"coinbase\": \"\",  (hex string, optional) Modified Coinbase transaction\n"
                "        \"time\": n,         (integer, optional) Block time\n"
                "        \"version\": n       (integer, optional) Block version\n"
                "    }\n"
                "\nResult:\n"
                "\nNothing on success, error string if block was rejected.\n"
                "Identical to \"submitblock\".\n"
                "\nExamples:\n" +
                HelpExampleRpc("submitminingsolution", "\"<json string>\""));
    }

    rcvd = request.params[0].get_obj();

    std::string idstr { rcvd["id"].get_str() };
    MiningCandidateId id { boost::lexical_cast<MiningCandidateId>(idstr) };

    CMiningCandidateRef result { mining::CMiningFactory::GetCandidateManager().Get(id) };
    if (!result)
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block candidate ID not found");
    }

    // Make a copy of the block we're trying to submit so that we can safely update the fields
    // sent to us without invalidating other candidates based off the same block.
    CBlockRef baseBlock { result->GetBlock() };
    CBlockRef block { std::make_shared<CBlock>(baseBlock->GetBlockHeader()) };
    block->vtx = baseBlock->vtx;

    UniValue nonce = rcvd["nonce"];
    if (nonce.isNull())
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "nonce not found");
    }
    block->nNonce = (uint32_t)nonce.get_int64(); // 64 bit to deal with sign bit in 32 bit unsigned int

    UniValue time = rcvd["time"];
    if (!time.isNull())
    {
        // Use time from client
        block->nTime = (uint32_t)time.get_int64();
    }
    else
    {
        // Use original time from mining candidate
        block->nTime = result->GetBlockTime();
    }

    // Reset nBits to those from the original candidate
    block->nBits = result->GetBlockBits();

    UniValue version = rcvd["version"];
    if (!version.isNull())
    {
        // Use version from client
        block->nVersion = version.get_int(); // version signed 32 bit int
    }
    else
    {
        // Use original version from mining candidate
        block->nVersion = result->GetBlockVersion();
    }

    // Coinbase
    UniValue cbhex = rcvd["coinbase"];
    if (!cbhex.isNull())
    {
        CMutableTransaction coinbase;
        if (DecodeHexTx(coinbase, cbhex.get_str()))
        {
            // Use coinbase from client
            block->vtx[0] = MakeTransactionRef(std::move(coinbase));
        }
        else
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "coinbase decode failed");
        }
    }
    else
    {
        // Use original coinbase from mining candidate
        block->vtx[0] = result->GetBlockCoinbase();
    }

    // Merkle root
    {
        std::vector<uint256> merkleProof = GetMerkleProofBranches(block);
        uint256 t = block->vtx[0]->GetHash();
        block->hashMerkleRoot = CalculateMerkleRoot(t, merkleProof);
    }

    // Submit solution
    UniValue submitted {};
    {
        // Ensure we run full checks on submitted block
        block->fChecked = false;

        auto submitBlock = [](const Config& config , const std::shared_ptr<CBlock>& blockptr) 
        {
            return ProcessNewBlock(config, blockptr, true, nullptr);
        };
        submitted = processBlock(config, block, submitBlock); // returns string on failure
    }
    if(submitted.isNull())
    {
        // Return true on success
        UniValue tru { UniValue::VBOOL };
        tru.setBool(true);
        submitted = tru;
    }

    // Clear out old candidates
    mining::CMiningFactory::GetCandidateManager().RemoveOldCandidates();

    return submitted;
}

/** Mining-Candidate end */

const CRPCCommand commands[] =
{
  //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "mining",             "getminingcandidate",     getminingcandidate,     true, {"coinbase"}  },
    { "mining",             "submitminingsolution",   submitminingsolution,   true, {}  },
};

} // namespace

void RegisterMiningFBBRPCCommands(CRPCTable &t)
{
    for (auto& cmd : commands)
        t.appendCommand(cmd.name, &cmd);
}

