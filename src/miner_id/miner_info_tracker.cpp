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
        return strprintf("minerinfotxtore-%06d.dat", height);
    };

    /*
     * Compares a file name to a template and extracts the counter
     * if the template matches
     */
    static std::optional<int> parsestorefile (std::string n)  {

        int res = 0;
        std::string t = "minerinfotxtore-######.dat";

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
    bool MinerInfoTxTracker::store_minerinfo_txid (int32_t height, uint256 const &blockhash, TxId const &txid)
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

            file << height << ' ' << blockhash << ' ' << txid << std::endl;
        }
        catch (std::exception const & e)
        {
            LogPrintf(strprintf("Error while storing funding information: %s",e.what()));
            return false;
        }
        catch (...)
        {
            LogPrintf("Error while storing funding information");
            return false;
        }
        return true;
    }

    /*
     * Load funding information from file.
     */
    bool MinerInfoTxTracker::load_minerinfo_txid ()
    {
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
                    throw std::runtime_error(strprintf("Cannot open funding data file: %s", (dir / filename).string()));
                TxId txid;
                uint256 blockhash;
                int32_t height;

                while (file >> height >> blockhash >> txid )
                {
                    Key key {height, blockhash};
                    entries_[key] = txid;
                }

                file.close();
            }
        }
        catch (std::exception const & e)
        {
            LogPrintf(strprintf("Error while loading funding information: %s",e.what()));
            return false;
        }
        catch (...)
        {
            LogPrintf("Error while loading funding information");
            return false;
        }
        return true;
    }

} // namespace mining

