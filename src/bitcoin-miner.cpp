// Copyright (c) 2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

///////////////////////////////////#include "allowed_args.h"
#include "arith_uint256.h"
#include "chainparamsbase.h"
#include "fs.h"
#include "hash.h"
#include "primitives/block.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "streams.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"

#include <boost/thread.hpp>

#include <cstdlib>
#include <stdio.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <univalue.h>

using namespace std;

typedef unsigned int extra_nonce_type;

// Internal miner
//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
bool static ScanHash(const CBlockHeader *pblock, uint32_t &nNonce, uint256 *phash)
{
    // Write the first 76 bytes of the block header to a double-SHA256 state.
    CHash256 hasher;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *pblock;
    assert(ss.size() == 80);
    hasher.Write((unsigned char *)&ss[0], 76);

    while (true)
    {
        nNonce++;

        // Write the last 4 bytes of the block header (the nonce) to a copy of
        // the double-SHA256 state, and compute the result.
        CHash256(hasher).Write((unsigned char *)&nNonce, 4).Finalize((unsigned char *)phash);

        // Return the nonce if the hash has at least some zero bits,
        // caller will check if it has enough to reach the target
        if (((uint16_t *)phash)[15] == 0)
            return true;

        // If nothing found after trying for a while, return -1
        if ((nNonce & 0xfff) == 0)
            return false;
    }
}

// Not ideal - a modified copy of the options from bitcoin-cli 
std::string HelpMessageCli() 
{
     const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
     const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);

     std::string strUsage;
     strUsage += HelpMessageGroup(_("Options:"));
     strUsage += HelpMessageOpt("-?", _("This help message"));
     strUsage += HelpMessageOpt( "-conf=<file>", strprintf(_("Specify configuration file (default: %s)"), BITCOIN_CONF_FILENAME));
     strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));

     strUsage += HelpMessageGroup(_("RPC options:"));
     strUsage += HelpMessageOpt("-named", strprintf(_("Pass named parameters instead of positional arguments (default: %s)"), DEFAULT_NAMED));
     strUsage += HelpMessageOpt("-rpcconnect=<ip>", strprintf(_("Send commands to node running on <ip> (default: %s)"), DEFAULT_RPCCONNECT));
     strUsage += HelpMessageOpt("-standalone", strprintf(_(""), DEFAULT_RPCCONNECT));
     strUsage += HelpMessageOpt("-rpcport=<port>",
         strprintf(_("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"),
             defaultBaseParams->RPCPort(), testnetBaseParams->RPCPort()));
     strUsage += HelpMessageOpt("-rpcwait", _("Wait for RPC server to start"));
     strUsage += HelpMessageOpt("-rpcuser=<user>", _("Username for JSON-RPC connections"));
     strUsage += HelpMessageOpt("-rpcpassword=<pw>", _("Password for JSON-RPC connections"));
     strUsage += HelpMessageOpt("-rpcclienttimeout=<n>",
                        strprintf(_("Timeout in seconds during HTTP requests, "
                                    "or 0 for no timeout. (default: %d)"),
                                  DEFAULT_HTTP_CLIENT_TIMEOUT));
     
     strUsage += HelpMessageOpt("-stdinrpcpass",
         strprintf(_("Read RPC password from standard input as a single line.  "
                     "When combined with -stdin, the first line from standard "
                     "input is used for the RPC password.")));
     strUsage += HelpMessageOpt("-stdin", _("Read extra arguments from standard input, one per line "
                     "until EOF/Ctrl-D (recommended for sensitive information "
                     "such as passphrases)"));
     strUsage += HelpMessageOpt("-rpcwallet=<walletname>",
         _("Send RPC for non-default wallet on RPC server (argument is wallet "
           "filename in bitcoind directory, required if bitcoind runs with "
           "multiple wallets)"));
     
     return strUsage;
}

std::string HelpMessage()
{
    std::stringstream ss;
    ss << HelpMessageCli();
    ss << HelpMessageGroup(_("Mining options:"));
    ss << HelpMessageOpt( "-blockversion", strprintf(_("Set the block version number. For testing only.  Value must be an integer: %s)"), DEFAULT_NAMED));
    ss << HelpMessageOpt( "-cpus", strprintf(_("Number of cpus to use for mining (default: 1).  Value must be an integer: %s)"), 1));
    ss << HelpMessageOpt( "-duration", strprintf(_("Number of seconds to mine a particular block candidate (default: 30). Value must be an integer: %s)"), DEFAULT_NAMED));
    ss << HelpMessageOpt( "-nblock", strprintf(_("Number of blocks to mine (default: mine forever / -1). Value must be an integer: %s)"), DEFAULT_NAMED));
    return ss.str();
}

inline uint32_t nbits(const UniValue &candidate_props)
{
    uint32_t val;
    std::stringstream ss;
    ss << std::hex << candidate_props["nBits"].get_str();
    ss >> val;
    return val;
}

static CBlockHeader CpuMinerJsonToHeader(const UniValue &candidate_props)
{
    // Does not set hashMerkleRoot (Does not exist in Mining-Candidate params).
    CBlockHeader blockheader;

    // nVersion
    blockheader.nVersion = candidate_props["version"].get_int();

    // hashPrevBlock
    string tmpstr = candidate_props["prevhash"].get_str();
    std::vector<unsigned char> vec = ParseHex(tmpstr);
    std::reverse(vec.begin(), vec.end()); // sent reversed
    blockheader.hashPrevBlock = uint256(vec);

    // nTime:
    blockheader.nTime = candidate_props["time"].get_int();

    // nBits
    blockheader.nBits = nbits(candidate_props);

    return blockheader;
}

static void CalculateNextMerkleRoot(uint256 &merkle_root, const uint256 &merkle_branch)
{
    // Append a branch to the root. Double SHA256 the whole thing:
    uint256 hash;
    CHash256()
        .Write(merkle_root.begin(), merkle_root.size())
        .Write(merkle_branch.begin(), merkle_branch.size())
        .Finalize(hash.begin());
    merkle_root = hash;
}

static uint256 CalculateMerkleRoot(uint256 &coinbase_hash, const std::vector<uint256> &merkleproof)
{
    uint256 merkle_root = coinbase_hash;
    for (unsigned int i = 0; i < merkleproof.size(); i++)
    {
        CalculateNextMerkleRoot(merkle_root, merkleproof[i]);
    }
    return merkle_root;
}

// PLEASE LEAVE FOR FUTURE TROUBLE SHOOTING
void print_coinbase_transaction(ostream& str, vector<unsigned char>& coinbase_bytes)
{
   for(unsigned char ch : coinbase_bytes)
        str << hex << setw(2) << setfill('0') << (int)ch;
    str << ".\n";
}

void Add_space_for_extra_nonce(vector<unsigned char>& coinbase_bytes, size_t offset_extra_nonce)
{
    // Copy the first part of the coinbase transaction into the tmp vector
    vector<unsigned char> tmp(coinbase_bytes.begin(), coinbase_bytes.begin() + offset_extra_nonce);

    // Add space for the extra-nonce into the tmp vector
    for(size_t i=0; i<sizeof(extra_nonce_type); i++)
        tmp.push_back((unsigned char)OP_NOP1);

    // Add on the second part of the coinbase transaction onto the tmp vector
    tmp.insert(tmp.begin() + offset_extra_nonce + sizeof(extra_nonce_type), 
                coinbase_bytes.begin() + offset_extra_nonce, 
                coinbase_bytes.end());

    // Copy the tmp vector into the original transaction 
    coinbase_bytes.assign(tmp.begin(), tmp.end());

    coinbase_bytes[41] += sizeof(extra_nonce_type);
}

// WARNING: This methods "splits" coinbaseBytes and inserts space for an extra-nonce.
static bool CpuMineBlockHasher(CBlockHeader *pblock, vector<unsigned char>& coinbaseBytes, const std::vector<uint256> &merkleproof)
{
    extra_nonce_type nExtraNonce = std::rand();
    uint32_t nNonce = pblock->nNonce;
    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
    bool found = false;
    int ntries = 10;

    // coinbase data layout
    // 4 bytes - version  
    // 1 byte - no of inputs (compact size) [start offset=4]
    // 32 bytes - input tx ID [start offset=5]
    // 4 bytes - input CTxOut index [start offset=37]
    // 1 byte - script length [start offset=41]
    // 3/4 bytes - block height [start offset=42] RegTest: typically looks like {0x02, 0xA&, 0x00}
    // -- extra nonce -- [start offset=45/46]

    size_t bytes_used_for_height = coinbaseBytes[42];
    size_t offset_extra_nonce = 43 + bytes_used_for_height;
    
    if(coinbaseBytes.size() < offset_extra_nonce + 2) // crude.
    {
        cerr << "Invalid coinbase transaction supplied\n";
        return false;
    }

    //cout << "Original coinbase tx is:\n";
    //print_coinbase_transaction(cout, coinbaseBytes);
  
    Add_space_for_extra_nonce(coinbaseBytes, offset_extra_nonce);
  
    //cout << "Expanded coinbase tx is:\n";
    //print_coinbase_transaction(cout, coinbaseBytes);

    while (!found)
    {
        // hashMerkleRoot:
        {
            ++nExtraNonce;
            unsigned char *pbytes = (unsigned char *)coinbaseBytes.data();            
            *(extra_nonce_type *)(pbytes + offset_extra_nonce) = nExtraNonce;
            uint256 hash;
            CHash256().Write(pbytes, coinbaseBytes.size()).Finalize(hash.begin());

            pblock->hashMerkleRoot = CalculateMerkleRoot(hash, merkleproof);
        }

        //
        // Search
        //
        uint256 hash;
        while (!found)
        {
            // Check if something found
            if (ScanHash(pblock, nNonce, &hash))
            {
                if (UintToArith256(hash) <= hashTarget)
                {
                    // Found a solution
                    pblock->nNonce = nNonce;
                    found = true;
                    printf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex().c_str(),
                        hashTarget.GetHex().c_str());
                    break;
                }
                else
                {
                    if (ntries-- < 1)
                    {
                        pblock->nNonce = nNonce; // report the last nonce checked for accounting
                        return false; // Give up leave
                    }
                }
            }
        }
    }

    return found;
}

static double GetDifficulty(uint64_t nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff = (double)0x0000ffff / (double)(nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }
    return dDiff;
}

static UniValue CpuMineBlock(unsigned int searchDuration, const UniValue &params, bool &found)
{
    UniValue tmp(UniValue::VOBJ);
    UniValue ret(UniValue::VARR);
    string tmpstr;
    std::vector<uint256> merkleproof;
    CBlockHeader header;
    vector<unsigned char> coinbaseBytes(ParseHex(params["coinbase"].get_str()));

    found = false;

    // re-create merkle branches:
    {
        UniValue uvMerkleproof = params["merkleProof"];
        for (unsigned int i = 0; i < uvMerkleproof.size(); i++)
        {
            tmpstr = uvMerkleproof[i].get_str();
            std::vector<unsigned char> mbr = ParseHex(tmpstr);
            std::reverse(mbr.begin(), mbr.end());
            merkleproof.push_back(uint256(mbr));
        }
    }

    header = CpuMinerJsonToHeader(params);

    // Set the version (only to test):
    {
        int blockversion = gArgs.GetArg("-blockversion", header.nVersion);
        if (blockversion != header.nVersion)
            printf("Force header.nVersion to %d\n", blockversion);
        header.nVersion = blockversion;
    }

    uint32_t startNonce = header.nNonce = std::rand();
    std::string candidateId = params["id"].get_str();

    printf("Mining: id: %s parent: %s bits: %x difficulty: %.8e time: %d\n", candidateId.c_str(),
        header.hashPrevBlock.ToString().c_str(), header.nBits, GetDifficulty(header.nBits), header.nTime);

    int64_t start = GetTime();
    while ((GetTime() < start + searchDuration) && !found)
    {
        // When mining mainnet, you would normally want to advance the time to keep the block time as close to the
        // real time as possible.  However, this CPU miner is only useful on testnet and in testnet the block difficulty
        // resets to 1 after 20 minutes.  This will cause the block's difficulty to mismatch the expected difficulty
        // and the block will be rejected.  So do not advance time (let it be advanced by bitcoind every time we
        // request a new block).
        // header.nTime = (header.nTime < GetTime()) ? GetTime() : header.nTime;

        found = CpuMineBlockHasher(&header, coinbaseBytes, merkleproof);
    }

    // Leave if not found:
    if (!found)
    {
        printf("Checked %d possibilities\n", header.nNonce - startNonce);
        return ret;
    }

    printf("Solution! Checked %d possibilities\n", header.nNonce - startNonce);

    tmpstr = HexStr(coinbaseBytes.begin(), coinbaseBytes.end());
    tmp.push_back(Pair("coinbase", tmpstr));
    tmp.push_back(Pair("id", candidateId));
    tmp.push_back(Pair("time", UniValue(uint64_t(header.nTime)))); // Optional. We have changed so must send.
    tmp.push_back(Pair("nonce", UniValue(uint64_t(header.nNonce))));
    tmp.push_back(Pair("version", UniValue(header.nVersion))); // Optional. We may have changed so sending.
    ret.push_back(tmp);

    return ret;
}

static UniValue RPCSubmitSolution(const UniValue &solution, int &nblocks)
{
    UniValue reply = CallRPC("submitminingsolution", solution);

    const UniValue &error = find_value(reply, "error");

    if (!error.isNull())
    {
        fprintf(stderr, "Block Candidate submission error: %d %s\n", error["code"].get_int(),
            error["message"].get_str().c_str());
        return reply;
    }

    const UniValue &result = find_value(reply, "result");

    if (result.isStr())
    {
        fprintf(stderr, "Block Candidate rejected. Error: %s\n", result.get_str().c_str());
        // Print some debug info if the block is rejected
        UniValue dbg = solution[0].get_obj();
        fprintf(stderr, "id: %d  time: %d  nonce: %d  version: 0x%x\n", (unsigned int)dbg["id"].get_int64(),
            (uint32_t)dbg["time"].get_int64(), (uint32_t)dbg["nonce"].get_int64(), (uint32_t)dbg["version"].get_int());
        fprintf(stderr, "coinbase: %s\n", dbg["coinbase"].get_str().c_str());
    }
    else
    {
        if (result.isTrue())
        {
            printf("Block Candidate accepted.\n");
            if (nblocks > 0)
                nblocks--; // Processed a block
        }
        else
        {
            fprintf(stderr, "Unknown \"submitminingsolution\" Response.\n");
        }
    }

    return reply;
}

int CpuMiner(void)
{
    int searchDuration = gArgs.GetArg("-duration", 30);
    int nblocks = gArgs.GetArg("-nblocks", -1); //-1 mine forever

    UniValue mineresult;
    bool found = false;

    if (0 == nblocks)
    {
        printf("Nothing to do for zero (0) blocks\n");
        return 0;
    }

    while (0 != nblocks)
    {
        UniValue reply;
        UniValue result;
        UniValue nbits;
        string strPrint;
        int nRet = 0;
        try
        {
            // Execute and handle connection failures with -rpcwait
            const bool fWait = true;
            do
            {
                try
                {
                    if (found)
                    {
                        // Submit the solution.
                        // Called here so all exceptions are handled properly below.
                        RPCSubmitSolution(mineresult, nblocks);
                        if (nblocks == 0)
                            return 0; // Done mining exit program
                        found = false; // Mine again
                    }

                    UniValue params(UniValue::VARR);                        
                    params.push_back(UniValue(true));
                    reply = CallRPC("getminingcandidate", params);

                    // Parse reply
                    result = find_value(reply, "result");
                    const UniValue &error = find_value(reply, "error");

                    if (!error.isNull())
                    {
                        // Error
                        int code = error["code"].get_int();
                        if (fWait && code == RPC_IN_WARMUP)
                            throw CConnectionFailed("server in warmup");
                        strPrint = "error: " + error.write();
                        nRet = abs(code);
                        if (error.isObject())
                        {
                            UniValue errCode = find_value(error, "code");
                            UniValue errMsg = find_value(error, "message");
                            strPrint = errCode.isNull() ? "" : "error code: " + errCode.getValStr() + "\n";

                            if (errMsg.isStr())
                                strPrint += "error message:\n" + errMsg.get_str();
                        }
                    }
                    else
                    {
                        nbits = find_value(result, "nBits");
                        if(nbits.isNull() && !nbits.isStr())
                        {
                            strPrint = "No valid difficulty (nBits) supplied.";
                            return 0;  // exit
                        }

                        // Result
                        if (result.isNull())
                            strPrint = "";
                        else if (result.isStr())
                            strPrint = result.get_str();
                        else
                            strPrint = result.write(2);
                    }
                    // Connection succeeded, no need to retry.
                    break;
                }
                catch (const CConnectionFailed &c)
                {
                    if (fWait)
                    {
                        printf("Warning: %s\n", c.what());
                        MilliSleep(1000);
                    }
                    else
                        throw;
                }
            } while (fWait);
        }
        catch (const boost::thread_interrupted &)
        {
            throw;
        }
        catch (const std::exception &e)
        {
            strPrint = string("error: ") + e.what();
            nRet = EXIT_FAILURE;
        }
        catch (...)
        {
            PrintExceptionContinue(NULL, "CommandLineRPC()");
            throw;
        }

        if (strPrint != "")
        {
            if (nRet != 0)
                fprintf(stderr, "%s\n", strPrint.c_str());
            // Actually do some mining
            if (result.isNull())
            {
                MilliSleep(1000);
            }
            else
            {
                found = false;
                mineresult = CpuMineBlock(searchDuration, result, found);
                if (!found)
                {
                    // printf("Mining did not succeed\n");
                    mineresult.setNull();
                }
                // The result is sent to bitcoind above when the loop gets to it.
                // See:   RPCSubmitSolution(mineresult,nblocks);
                // This is so RPC Exceptions are handled in one place.
            }
        }
    }
    return 0;
}

void static MinerThread()
{
    while (1)
    {
        try
        {
            CpuMiner();
        }
        catch (const std::exception &e)
        {
            PrintExceptionContinue(&e, "CommandLineRPC()");
        }
        catch (...)
        {
            PrintExceptionContinue(NULL, "CommandLineRPC()");
        }
    }
}

int main(int argc, char *argv[])
{
    SetupEnvironment();
    if (!SetupNetworking())
    {
        fprintf(stderr, "Error: Initializing networking failed\n");
        exit(1);
    }

    try
    {
        std::string appname("bitcoin-miner");
        std::string usage = "\n" + _("Usage:") + "\n" + "  " + appname + " [options] " + "\n";
        int ret = AppInitRPC(argc, argv, usage, HelpMessage);
        if (ret != CONTINUE_EXECUTION)
            return ret;
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "AppInitRPC()");
        return EXIT_FAILURE;
    }
    catch (...)
    {
        PrintExceptionContinue(NULL, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int nThreads = gArgs.GetArg("-cpus", 1);
    boost::thread_group minerThreads;
    for (int i = 0; i < nThreads - 1; i++)
        minerThreads.create_thread(MinerThread);

    int ret = EXIT_FAILURE;
    try
    {
        ret = CpuMiner();
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CommandLineRPC()");
    }
    catch (...)
    {
        PrintExceptionContinue(NULL, "CommandLineRPC()");
    }
    return ret;
}
