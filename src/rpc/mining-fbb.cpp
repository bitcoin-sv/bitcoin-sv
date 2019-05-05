// Copyright (c) 2015 G. Andrew Stone
// Copyright (c) 2018-2019 The Bitcoin SV developers.
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

namespace
{

/// mkblocktemplate is a modified/cut down version of the code from the RPC method getblocktemplate. 
/// It is currently only called from getminingcandidate, but getblocktemplate could be
/// modified to call a generic version of mkblocktemplate.
UniValue mkblocktemplate(const Config& config, bool coinbaseRequired, CMiningCandidateRef candidate)
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

    static unsigned int nTransactionsUpdatedLast = std::numeric_limits<unsigned int>::max();

    // Update block
    static CBlockIndex *pindexPrev = nullptr;
    static int64_t nStart = 0;
    static std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if (pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5)) 
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex *pindexPrevNew = chainActive.Tip();
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
        pblocktemplate = CMiningFactory::GetAssembler(config)->CreateNewBlock(coinbaseScriptPubKey);

        if (!pblocktemplate) 
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to create a new block. Possibly out of memory.");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }

    CBlockRef blockref = pblocktemplate->GetBlockRef();
    CBlock *pblock = blockref.get();

    // Update nTime
    UpdateTime(pblock, config, pindexPrev);
    pblock->nNonce = 0;

    candidate->SetBlock(blockref);

    return NullUniValue;
}


std::vector<uint256> GetMerkleProofBranches(CBlockRef pblock)
{
    std::vector<uint256> ret;
    std::vector<uint256> leaves;
    int len = pblock->vtx.size();

    for (int i = 0; i < len; i++)
    {
        leaves.push_back(pblock->vtx[i].get()->GetHash());
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

// Sets the version bits in a block
static int32_t MkBlockTemplateVersionBits(int32_t version,
     std::set<std::string> setClientRules,
     CBlockIndex *pindexPrev,
     UniValue *paRules,
     UniValue *pvbavailable) // Keep in line with BU as much as possible.
{   
     const Consensus::Params &consensusParams = Params().GetConsensus();
     
     for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j)
     {
         Consensus::DeploymentPos pos = Consensus::DeploymentPos(j);
         ThresholdState state = VersionBitsState(pindexPrev, consensusParams, pos, versionbitscache);
         switch (state)
         {
         case THRESHOLD_DEFINED:
         case THRESHOLD_FAILED:
             // Not exposed to GBT at all
             break;
         case THRESHOLD_LOCKED_IN:
             // Ensure bit is set in block version
             version |= VersionBitsMask(consensusParams, pos);
             // FALLTHROUGH
         // to get vbavailable set...
         case THRESHOLD_STARTED:
             {
                 const struct BIP9DeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
                 if (pvbavailable != nullptr)
                 {   
                     pvbavailable->push_back(Pair(gbt_vb_name(pos), consensusParams.vDeployments[pos].bit));
                 }
                 if (setClientRules.find(vbinfo.name) == setClientRules.end())
                 {
                     if (!vbinfo.gbt_force)
                     {
                         // If the client doesn't support this, don't indicate it in the [default] version
                         version &= ~VersionBitsMask(consensusParams, pos);
                     }
                     //if (vbinfo.myVote == true) // let the client vote for this feature  
                     //    version |= VersionBitsMask(consensusParams, pos);
                 }
                 break;
             }
         case THRESHOLD_ACTIVE:
             {
                 // Add to rules only
                 const struct BIP9DeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
                 if (paRules != nullptr)
                 {   
                     paRules->push_back(gbt_vb_name(pos));
                 }
                 if (setClientRules.find(vbinfo.name) == setClientRules.end())
                 {
                     // Not supported by the client; make sure it's safe to proceed
                     if (!vbinfo.gbt_force)
                     {
                         // If we do anything other than throw an exception here, be sure version/force isn't sent to old clients
                         throw JSONRPCError(RPC_INVALID_PARAMETER,
                             strprintf("Support for '%s' rule requires explicit client support", vbinfo.name));
                     }
                 }
                 break;
             }
        }
    }
    return version;
}

// Create Mining-Candidate JSON to send to miner
UniValue MkMiningCandidateJson(bool coinbaseRequired, CMiningCandidateRef &candidate)
{
    UniValue ret(UniValue::VOBJ);
    CBlockRef block = candidate->GetBlock();

    CMiningFactory::GetCandidateManager().RemoveOldCandidates();

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

    std::set<std::string> setClientRules;
    CBlockIndex *const pindexPrev = chainActive.Tip();

    block->nVersion = MkBlockTemplateVersionBits(block->nVersion, setClientRules, pindexPrev, nullptr, nullptr);

    ret.push_back(Pair("version", block->nVersion));
    ret.push_back(Pair("nBits", strprintf("%08x", block->nBits)));
    ret.push_back(Pair("time", block->GetBlockTime()));
    ret.push_back(Pair("height", block->GetHeightFromCoinbase()));

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
                    "        \"id\": n,              (string) Candidate identifier for submitminingsolution\n"
                    "        \"prevhash\": \"xxxx\", (hex string) Hash of the previous block\n"
                    "        \"coinbase\": \"xxxx\", (optional hex string encoded binary transaction) Coinbase transaction\n"
                    "        \"version\": n,         (integer) Block version\n"
                    "        \"nBits\": \"xxxx\",    (hex string) Difficulty\n"
                    "        \"time\": n,            (integer) Block time\n"
                    "        \"height\": n,          (integer) Current Block Height\n"
                    "        \"merkleProof\": [      (list of hex strings) Merkle branch for the block\n"
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
    CMiningCandidateRef candidate = CMiningFactory::GetCandidateManager().Create(chainActive.Tip()->GetBlockHash());
    mkblocktemplate(config, coinbaseRequired, candidate);
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

    CMiningCandidateRef result { CMiningFactory::GetCandidateManager().Get(id) };
    if (!result)
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block candidate ID not found");
    }

    CBlockRef block = result->GetBlock();

    UniValue nonce = rcvd["nonce"];
    if (nonce.isNull())
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "nonce not found");
    }
    block->nNonce = (uint32_t)nonce.get_int64(); // 64 bit to deal with sign bit in 32 bit unsigned int

    UniValue time = rcvd["time"];
    if (!time.isNull())
    {
        block->nTime = (uint32_t)time.get_int64();
    }

    UniValue version = rcvd["version"];
    if (!version.isNull())
    {
        block->nVersion = version.get_int(); // version signed 32 bit int
    }

    // Coinbase
    UniValue cbhex = rcvd["coinbase"];
    if (!cbhex.isNull())
    {
        CMutableTransaction coinbase;
        if (DecodeHexTx(coinbase, cbhex.get_str()))
        {
            block->vtx[0] = MakeTransactionRef(std::move(coinbase));
        }
        else
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "coinbase decode failed");
        }
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

        LOCK(cs_main);
        submitted = SubmitBlock(config, block); // returns string on failure
    }
    if(submitted.isNull())
    {
        // Return true on success
        UniValue tru { UniValue::VBOOL };
        tru.setBool(true);
        submitted = tru;
    }

    // Clear out old candidates
    CMiningFactory::GetCandidateManager().RemoveOldCandidates();

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

