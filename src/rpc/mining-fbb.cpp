/// Copyright (c) 2015 G. Andrew Stone
// Copyright (c) 2018 The Bitcoin SV developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

using namespace std;

namespace
{

/** Mining-Candidate begin */
const int NEW_CANDIDATE_INTERVAL = 30; // seconds

// This class originates in the BU codebase. 
// It is retained as a point of commonality between BU and SV codebases.
class CMiningCandidate  
{
public:
    CBlock block;
};
std::map<int64_t, CMiningCandidate> MiningCandidates;

inline int GetBlockchainHeight()
{
    return chainActive.Height();
}

// Oustanding candidates are removed 30 sec after a new block has been found
void RmOldMiningCandidates()
{
    LOCK(cs_main);
    static unsigned int prevheight = 0;
    unsigned int height = GetBlockchainHeight();

    if (height <= prevheight)
        return;

    int64_t tdiff = GetTime() - (chainActive.Tip()->nTime + NEW_CANDIDATE_INTERVAL);
    if (tdiff >= 0)
    {
        // Clean out mining candidates that are the same height as a discovered block.
        for (auto it = MiningCandidates.cbegin(); it != MiningCandidates.cend();)
        {
            if (it->second.block.GetHeight() <= prevheight)
            {
                it = MiningCandidates.erase(it);
            }
            else
            {
                ++it;
            }
        }
        prevheight = height;
    }
}

/// mkblocktemplate is a modified/cut down version of the code from the RPC method getblocktemplate. 
/// It is currently only called from getminingcandidate, but getblocktemplate could be
/// modified to call a generic version of mkblocktemplate.
UniValue mkblocktemplate(const Config& config, const UniValue &params, CBlock *pblockOut)
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
        //cout << "Creating a new candidate block\n";

        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex *pindexPrevNew = chainActive.Tip();
        nStart = GetTime();

        std::shared_ptr<CReserveScript> coinbaseScript;
        GetMainSignals().ScriptForMining(coinbaseScript);
 
        // If the keypool is exhausted, no script is returned at all.  Catch this.
        if (!coinbaseScript)
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
 
        // throw an error if no script was provided
        if (coinbaseScript->reserveScript.empty())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "No coinbase script available (mining requires a wallet)");

        pblocktemplate = CMiningFactory::GetAssembler(config)->CreateNewBlock(coinbaseScript->reserveScript);
        if (!pblocktemplate) 
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to create a new block. Possibly out of memory.");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }

    CBlock *pblock = &pblocktemplate->block;

    // Update nTime
    UpdateTime(pblock, config, pindexPrev);
    pblock->nNonce = 0;

    if (pblockOut != nullptr)
        *pblockOut = *pblock;

    return NullUniValue;
}

void AddMiningCandidate(CMiningCandidate &candid, int64_t id)
{
    // Save candidate so can be looked up:
    LOCK(cs_main);
    MiningCandidates[id] = candid;
}

std::vector<uint256> GetMerkleProofBranches(CBlock *pblock)
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
UniValue MkMiningCandidateJson(CMiningCandidate &candid)
{
    static int64_t id = 0;
    UniValue ret(UniValue::VOBJ);
    CBlock &block = candid.block;

    RmOldMiningCandidates();

    // Save candidate so can be looked up:
    id++;
    AddMiningCandidate(candid, id);
    ret.push_back(Pair("id", id));

    ret.push_back(Pair("prevhash", block.hashPrevBlock.GetHex()));

    {
        const CTransaction *tran = block.vtx[0].get();
        ret.push_back(Pair("coinbase", EncodeHexTx(*tran)));
    }

    std::set<std::string> setClientRules;
    CBlockIndex *const pindexPrev = chainActive.Tip();

    block.nVersion = MkBlockTemplateVersionBits(block.nVersion, setClientRules, pindexPrev, nullptr, nullptr);

    ret.push_back(Pair("version", block.nVersion));
    ret.push_back(Pair("nBits", strprintf("%08x", block.nBits)));
    ret.push_back(Pair("time", block.GetBlockTime()));
    ret.push_back(Pair("height", block.GetHeight()));

    // merkleProof:
    {
        std::vector<uint256> brancharr = GetMerkleProofBranches(&block);
        UniValue merkleProof(UniValue::VARR);
        for (const auto &i : brancharr)
        {
            merkleProof.push_back(i.GetHex());
        }
        ret.push_back(Pair("merkleProof", merkleProof));
    }

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
    if (request.fHelp || request.params.size() > 0)
    {
        throw std::runtime_error(
                    "getminingcandidate\n"
                    "\nReturns Mining-Candidate protocol data.\n"
                    "\nArguments: None\n"
                    "\nResult: (json string)\n"
                    "    {\n                         \n"
                    "        \"id\": n,              (integer) candidate identifier for submitminingsolution\n"
                    "        \"prevhash\": \"xxxx\", (hex string) Hash of the previous block\n"
                    "        \"coinbase\": \"xxxx\", (hex string encoded binary transaction) Coinbase transaction\n"
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

    CMiningCandidate candidate;
    LOCK(cs_main);
    mkblocktemplate(config, request.params, &candidate.block);  // These mirror the functions in BU
    return MkMiningCandidateJson(candidate);
}

/// RPC - Return a successfully mined block 
UniValue submitminingsolution(const Config& config, const JSONRPCRequest& request) 
{
    UniValue rcvd;
    std::shared_ptr<CBlock> block = std::make_shared<CBlock>();

    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error(
                "submitminingsolution \"<json string>\" \n"
                "\nAttempts to submit a new block to the network.\n"
                "\nJson Object should comprise of the following and must be escaped\n"
                "    {\n"
                "        \"id\": n,           (integer) ID from getminingcandidate RPC\n"
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

    int64_t id = rcvd["id"].get_int64();

    LOCK(cs_main);
    if (MiningCandidates.count(id) == 1)
    {
        *block = MiningCandidates[id].block;
        MiningCandidates.erase(id);
    }
    else
    {
        return UniValue("Block candidate ID not found");
    }

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
            block->vtx[0] = MakeTransactionRef(std::move(coinbase));
        else
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "coinbase decode failed");
        }
    }

    // Merkle root
    {
        std::vector<uint256> merkleProof = GetMerkleProofBranches(block.get());
        uint256 t = block->vtx[0]->GetHash();
        block->hashMerkleRoot = CalculateMerkleRoot(t, merkleProof);
    }

    UniValue submitted = SubmitBlock(config, block); // returns string on failure
    RmOldMiningCandidates();
    return submitted;
}

/** Mining-Candidate end */

const CRPCCommand commands[] =
{
  //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "mining",             "getminingcandidate",     getminingcandidate,     true, {}  },
    { "mining",             "submitminingsolution",   submitminingsolution,   true, {}  },
};

} // namespace

void RegisterMiningFBBRPCCommands(CRPCTable &t)
{
    for (auto& cmd : commands)
        t.appendCommand(cmd.name, &cmd);
}

