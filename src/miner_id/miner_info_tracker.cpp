// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/miner_info_tracker.h"
#include "primitives/transaction.h"
#include "util.h"
#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <iterator>
#include <memory>
#include <mutex>

std::unique_ptr<mining::BlockDatarefTracker> g_BlockDatarefTracker;
std::unique_ptr<mining::MempoolDatarefTracker> g_MempoolDatarefTracker;

static const fs::path fundingPath = fs::path("miner_id") / "Funding";

/*
 * Creates filename with counter
 */
static std::string storage_filename_with_date ()
{
    auto t = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(t);

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&tt), "minerinfotxstore-%Y-%m-%d.dat");
    return ss.str();
};
/*
 * Store minerinfo txn-ids in chunks spanning periods of 1000 blocks
 */
static bool store_minerinfo_fund_NL(const std::vector<COutPoint>& funds)
{
    try
    {
        auto dir = (GetDataDir() / fundingPath);
        auto filepath = dir / storage_filename_with_date();

        if(!fs::exists(dir))
            fs::create_directory(dir);
        fs::ofstream file;
        file.open(filepath.string(),
                  std::ios::out | std::ios::binary | std::ios::app);
        if(!file.is_open())
            throw std::runtime_error(
                "Cannot open and truncate funding data file: " +
                filepath.string());

        for(const auto& fund : funds)
            file << fund.GetTxId() << ' ' << fund.GetN() << std::endl;
    }
    catch(std::exception const& e)
    {
        LogPrintf(strprintf("Error while storing funding information: %s\n",
                            e.what()));
        return false;
    }
    catch(...)
    {
        LogPrintf("Error while storing funding information\n");
        return false;
    }
    return true;
}

void mining::BlockDatarefTracker::set_current_minerid(const CPubKey& minerId) {
    std::lock_guard lock (mtx_minerid_);
    minerId_ = minerId;
};

std::optional<CPubKey> mining::BlockDatarefTracker::get_current_minerid() const {
    std::lock_guard lock (mtx_minerid_);
    return minerId_;
};

bool mining::move_and_store(MempoolDatarefTracker& mempool_tracker,
                            BlockDatarefTracker& block_tracker)
{
    try
    {
        std::scoped_lock lock{mempool_tracker.mtx_, block_tracker.mtx_};
        const std::vector<COutPoint> funds{std::move(mempool_tracker.funds_)};
        block_tracker.funds_.insert(block_tracker.funds_.end(),
                                    funds.begin(),
                                    funds.end());
        store_minerinfo_fund_NL(funds);
    }
    catch(const std::exception& e)
    {
        LogPrintf(strprintf("could not store minerinfo tracking information: %s\n",
                            e.what()));
        return false;
    }
    catch(...)
    {
        LogPrintf("could not store minerinfo tracking information\n");
        return false;
    }
    return true;
}

std::optional<COutPoint> mining::MempoolDatarefTracker::funds_back() const
{
    std::lock_guard lock{mtx_};
    if(funds_.empty())
        return std::nullopt;

    return funds_.back();
}

std::optional<COutPoint> mining::MempoolDatarefTracker::funds_front() const
{
    std::lock_guard lock{mtx_};
    if(funds_.empty())
        return std::nullopt;

    return funds_.front();
}

std::vector<COutPoint> mining::MempoolDatarefTracker::funds() const
{
    std::lock_guard lock{mtx_};
    return funds_;
}

void mining::MempoolDatarefTracker::funds_replace(std::vector<COutPoint> other)
{
    std::lock_guard lock{mtx_};
    funds_ = std::move(other);
}

void mining::MempoolDatarefTracker::funds_append(const COutPoint& outp)
{
    std::lock_guard lock{mtx_};
    funds_.push_back({outp});
}

void mining::MempoolDatarefTracker::funds_clear()
{
    std::lock_guard lock{mtx_};
    funds_.clear();
}

bool mining::MempoolDatarefTracker::funds_pop_back()
{
    std::lock_guard lock{mtx_};
    if(funds_.empty())
        return false;
    funds_.pop_back();
    return true;
}

bool mining::MempoolDatarefTracker::contains(const TxId& txid) const
{
    std::lock_guard lock{mtx_};
    return std::find_if(funds_.cbegin(), funds_.cend(), [&txid](const auto& x) {
               return x.GetTxId() == txid;
           }) != funds_.cend();
}

/*
 * Compares a file name to a template and extracts the counter
 * if the template matches
 */
static std::optional<int> parsestorefile (std::string n)  {

    int res = 0;
    std::string t = "minerinfotxstore-####-##-##.dat";

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

std::optional<std::pair<COutPoint, std::optional<CoinWithScript>>>
mining::BlockDatarefTracker::find_fund(
        int32_t height,
        std::function<std::optional<CoinWithScript>(const COutPoint&)>
            get_spendable_coin) const
{
    std::lock_guard lock(mtx_);
    for(auto i = funds_.crbegin(); i != funds_.crend(); ++i)
    {
        const COutPoint& fund = *i;
        std::optional<CoinWithScript> coin = get_spendable_coin(fund);
        if(coin.has_value())
            return {{fund, std::move(coin)}};
    }
    return std::nullopt;
}

/*
 * Load funding information from dir.
 */
std::unique_ptr<mining::BlockDatarefTracker> mining::make_from_dir(
    const boost::filesystem::path& dir)
{
    using namespace std;

    auto tracker = make_unique<BlockDatarefTracker>();
    try
    {
        vector<string> files;
        for(auto const& entry : fs::directory_iterator{dir})
        {
            string file = entry.path().filename().string();
            auto n = parsestorefile(file);
            if(n)
                files.push_back(file);
        }

        sort(files.begin(), files.end());
        for(string filename : files)
        {
            fs::ifstream file(dir / filename, ios::binary | ios::out);
            TxId txid;
            uint32_t n;
            while(file >> txid >> n)
                tracker->funds_.push_back(COutPoint{txid, n}); }
        return tracker;
    }
    catch(exception const& e)
    {
        LogPrint(BCLog::MINERID,
                 strprintf("Warning - Unable to load funding information "
                           "for miner ID; node will be unable "
                           "to mine blocks containing a miner ID unless "
                           "you setup a funding seed as described in the "
                           "documentation: %s\n",
                           e.what()));
    }
    catch(...)
    {
        LogPrint(BCLog::MINERID,
                 "Warning - Unable to load funding information for miner "
                 "ID; node will be unable "
                 "to mine blocks containing a miner ID unless you setup a "
                 "funding seed as described in the documentation\n");
    }
    return tracker;
}

std::unique_ptr<mining::BlockDatarefTracker> mining::make_from_dir()
{
    const auto dir = (GetDataDir() / fundingPath);
    return make_from_dir(dir); 
}

