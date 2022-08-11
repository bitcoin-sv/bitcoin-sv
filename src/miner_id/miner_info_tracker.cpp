// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/miner_info_tracker.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

namespace mining {

    static const fs::path fundingPath = fs::path("miner_id") / "Funding";

    /*
     * Creates filename with counter
     */
    static std::string minerinfotxstore (int32_t height)
    {
        return strprintf("minerinfotxstore-%06d.dat", height);
    };

    /*
     * Compares a file name to a template and extracts the counter
     * if the template matches
     */
    static std::optional<int> parsestorefile (std::string n)  {

        int res = 0;
        std::string t = "minerinfotxstore-######.dat";

        if (t.size() != n.size())
            return std::nullopt;

        for (size_t i = 0; i < t.size(); ++i)
        {
            if (t[i] == '#')
            {
                if (n[i] < '0' || n[i] > '9')
                    return std::nullopt;
                res *= 10;
                res += (n[i] - '0');
            }
            else if (n[i] != t[i])
            {
                return std::nullopt;
            }
            else
            {
                continue;
            }
        }
        return res;
    };

    static std::istream & operator >> (std::istream & stream, uint256 & hash)
    {
        std::string s;
        stream >> s;
        hash = uint256S(s);
        return stream;
    }

    static std::istream & operator >> (std::istream & stream, TxId & txid)
    {
        uint256 hash;
        stream >> hash;
        txid = TxId(hash);
        return stream;
    }

    /*
     * Store minerinfo txn-ids in chunks spanning periods of 1000 blocks
     */
    bool DatarefTracker::store_minerinfo_fund (int32_t height, const uint256& blockhash, const FundingNode& fundingNode, size_t idx)
    {
        try
        {
            auto dir = (GetDataDir() / fundingPath);
            auto filepath = dir / minerinfotxstore(height / 1000);

            if (!fs::exists(dir))
                fs::create_directory(dir);
            fs::ofstream file;
            file.open(filepath.string(), std::ios::out | std::ios::binary | std::ios::app);
            if (!file.is_open())
                throw std::runtime_error("Cannot open and truncate funding data file: " + filepath.string());

            file
                << height << ' '
                << blockhash << ' '
                << fundingNode.outPoint.GetTxId() << ' '
                << fundingNode.outPoint.GetN() << ' '
                << fundingNode.previous.GetTxId() << ' '
                << fundingNode.previous.GetN() << ' '
                << idx << std::endl;
        }
        catch (std::exception const & e)
        {
            LogPrintf(strprintf("Error while storing funding information: %s\n",e.what()));
            return false;
        }
        catch (...)
        {
            LogPrintf("Error while storing funding information\n");
            return false;
        }
        return true;
    }

    /*
     * Load funding information from file.
     */
    bool DatarefTracker::load_minerinfo_funds ()
    {
        static size_t count_logs = 0;
        try
        {
            auto dir = (GetDataDir() / fundingPath);

            std::vector<std::string> files;
            for (auto const& entry : fs::directory_iterator{dir})
            {
                std::string file = entry.path().filename().string();
                auto n = parsestorefile(file);
                if (n)
                    files.push_back(file);
            }

            std::sort(files.begin(), files.end());
            for (std::string filename: files)
            {
                fs::ifstream file;
                file.open(dir / filename, std::ios::binary | std::ios::out);
                if (!file.is_open())
                    return true;  //nothing stored yet, this is not an error
                int32_t height;
                uint256 blockhash;
                TxId txid;
                uint32_t n;
                TxId prev_txid;
                uint32_t prev_n;

                size_t idx;

                while (file >> height >> blockhash >> txid >> n >> prev_txid >>  prev_n >> idx)
                {
                    Key key {height, blockhash};
                    DatarefVector & v = entries_[key];
                    if (v.funds.size() <= idx)
                        v.funds.resize(idx+1);

                    COutPoint outPoint {txid, n};
                    COutPoint previous {prev_txid, prev_n};

                    v.funds[idx] = FundingNode{outPoint,previous};
                }

                file.close();
            }
        }
        catch (std::exception const & e)
        {
            if (++count_logs < 3)
                LogPrint(BCLog::MINERID, strprintf(
				"Warning - Unable to load funding information for miner ID; node will be unable " 
				"to mine blocks containing a miner ID unless you setup a funding seed as described in the documentation: %s\n",
				e.what()));
            return false;
        }
        catch (...)
        {
            if (++count_logs < 3)
                LogPrint(BCLog::MINERID,
				"Warning - Unable to load funding information for miner ID; node will be unable "
				"to mine blocks containing a miner ID unless you setup a funding seed as described in the documentation\n");
            return false;
        }
        return true;
    }

} // namespace mining

