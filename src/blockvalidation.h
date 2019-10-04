// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_BLOCK_VALIDATION_H
#define BITCOIN_BLOCK_VALIDATION_H

#include <algorithm>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "uint256.h"

class CBlockValidationStatus
{
private:
    std::vector<uint256> mCurrentlyValidatingBlocks;
    std::mutex mMutexCurrentlyValidatingBlocks;

    std::vector<uint256> mWaitAfterValidation;
    std::mutex mMutexWaitAfterValidation;

public:
    class CScopeGuard
    {
    public:
        CScopeGuard(CBlockValidationStatus& instance, const uint256& blockHash)
            : mInstance{instance}
            , mBlockHash{blockHash}
        {
            std::scoped_lock lockGuard{mInstance.mMutexCurrentlyValidatingBlocks};
            mInstance.mCurrentlyValidatingBlocks.push_back(mBlockHash);
        }

        ~CScopeGuard()
        {
            std::scoped_lock lockGuard{mInstance.mMutexCurrentlyValidatingBlocks};
            mInstance.mCurrentlyValidatingBlocks.erase(
                std::find(
                    mInstance.mCurrentlyValidatingBlocks.begin(),
                    mInstance.mCurrentlyValidatingBlocks.end(),
                    mBlockHash));
        }

    private:
        CBlockValidationStatus& mInstance;
        uint256 mBlockHash;
    };

    CScopeGuard getScopedCurrentlyValidatingBlock(const uint256& blockHash)
    {
        return {*this, blockHash};
    }

    std::vector<uint256> getCurrentlyValidatingBlocks()
    {
        std::scoped_lock lockGuard(mMutexCurrentlyValidatingBlocks);
        return mCurrentlyValidatingBlocks;
    }

    std::vector<uint256> getWaitingAfterValidationBlocks()
    {
        std::scoped_lock lockGuard(mMutexWaitAfterValidation);
        return mWaitAfterValidation;
    }

    void waitAfterValidation(const uint256& blockHash, std::string_view action)
    {
        std::scoped_lock lockGuard(mMutexWaitAfterValidation);
        if (action == "add")
        {
            mWaitAfterValidation.push_back(blockHash);
        }
        else if (action == "remove")
        {
            auto found =
                std::find(
                    mWaitAfterValidation.begin(),
                    mWaitAfterValidation.end(),
                    blockHash);

            if (found != mWaitAfterValidation.end())
            {
                mWaitAfterValidation.erase(found);
            }
        }
    }

    void waitIfRequired(const uint256& blockHash)
    {
        bool foundFlag = false;

        // if blockHash is not in mWaitAfterValidation
        // then we break while loop
        do
        {
            {
                std::scoped_lock lockGuard(mMutexWaitAfterValidation);
                foundFlag =
                    std::any_of(
                        mWaitAfterValidation.begin(),
                        mWaitAfterValidation.end(),
                        [&blockHash](const uint256& otherHash)
                        {
                            return otherHash == blockHash;
                        });
            }

            if(foundFlag)
            {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(100ms);
            }
        } while (foundFlag);
    }
};

#endif // BITCOIN_BLOCK_VALIDATION_H