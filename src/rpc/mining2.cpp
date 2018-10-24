/// Copyright (c) 2015 G. Andrew Stone
// Copyright (c) 2018 nChain/Bitcoin Cash developers.
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
#include "miner.h"
#include "net.h"
#include "policy/policy.h"
#include "rpc/server.h"
#include "tinyformat.h"
#include "util.h"
#include "validationinterface.h"
#include "validation.h"
#include "version.h"
#include "utilstrencodings.h"
#include <iomanip>
#include <limits>
#include <queue>

using namespace std;

namespace
{

/** Mining-Candidate begin */
const int NEW_CANDIDATE_INTERVAL = 30; // seconds

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

        // Miner supplies the coinbase outputs. 
        CScript scriptPubKey = CScript() << OP_1;
        pblocktemplate = BlockAssembler(config).CreateNewBlock(scriptPubKey); 
        if (!pblocktemplate) 
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

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

    ret = ComputeMerkleBranch(leaves, 0);
    return ret;
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

// PLEASE LEAVE FOR FUTURE TROUBLE SHOOTING
void Debug_dump(const CTransaction& tx)
{
    string hex =EncodeHexTx(tx); 
    cout << "Creating mining candidate json, coinbase is " << hex << "\n";
    CMutableTransaction tx2;
    bool ok = DecodeHexTx(tx2, hex);
    if(!ok)
	    cout << "Unable to deserialise tx\n";
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
        //Debug_dump(*tran);
    }

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

        // merklePath parameter:
        // If the coinbase is ever allowed to be anywhere in the hash tree via a hard fork, we will need to communicate
        // how to calculate the merkleProof by supplying a bit for every level in the proof.
        // This bit tells the calculator whether the next hash is on the left or right side of the tree.
        // In other words, whether to do cat(A,B) or cat(B,A).  Specifically, if the bit is 0,the proof calcuation uses
        // Hash256(concatentate(running hash, next hash in proof)), if the bit is 1, the proof calculates
        // Hash256(concatentate(next hash in proof, running hash))

        // ret.push_back(Pair("merklePath", 0));  // diliberately disabled.
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
    CMiningCandidate candidate;
    LOCK(cs_main);

    if (request.fHelp || request.params.size() > 0)
    {
        throw std::runtime_error("getminingcandidate"
                                "\nReturns Mining-Candidate protocol data.\n"
                                "\nArguments: None\n");
    }
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
            "submitminingsolution \"Mining-Candidate data\" ( \"jsonparametersobject\" )\n"
            "\nAttempts to submit a new block to the network.\n"
            "\nArguments\n"
            "1. \"submitminingsolutiondata\"    (string, required) the mining solution (JSON encoded) data to submit\n"
            "\nResult:\n"
            "\nNothing on success, error string if block was rejected.\n"
            "Identical to \"submitblock\".\n"
            "\nExamples:\n" +
            HelpExampleRpc("submitminingsolution", "\"mydata\""));
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

void RegisterMining2RPCCommands(CRPCTable &t)
{
    for (auto& cmd : commands)
        t.appendCommand(cmd.name, &cmd);
}

