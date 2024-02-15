// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_BLOCK_VALIDATION_H
#define BITCOIN_BLOCK_VALIDATION_H

#include <algorithm>
#include <exception>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "chain.h"
#include "uint256.h"
#include "taskcancellation.h"

/**
 * Exception used for indicating that block has been validated but a different
 * block that was in parallel validation was validated before and changed
 * chain tip so we should not change it again (not an error).
 */
class CBestBlockAttachmentCancellation : public std::exception
{
public:
    const char* what() const noexcept override
    {
        return "CBestBlockAttachmentCancellation";
    };
};

/**
 * Class for tracking blocks that are currently in script validation stage.
 *
 * FOR TESTING ONLY: Also handles blocking blocks from reporting validation
 *                   completed to simulate long validating blocks and reorder
 *                   of accepted-validating blocks order while cs_main is
 *                   released.
 *
 * NOTE: This class doesn't require cs_main lock as CBlockIndex address and hash
 *       stability are guaranteed by mapBlockIndex implementation.
 */
class CBlockValidationStatus
{
private:
    std::vector<const CBlockIndex*> mCurrentlyValidatingBlocks;
    mutable std::mutex mMutexCurrentlyValidatingBlocks;

    std::vector<uint256> mWaitAfterValidation;
    mutable std::mutex mMutexWaitAfterValidation;

public:
    class CScopeGuard // NOLINT(cppcoreguidelines-special-member-functions)
    {
    public:
        CScopeGuard(CBlockValidationStatus& instance, const CBlockIndex& index)
            : mInstance{instance}
            , mIndex{index}
        {
            std::scoped_lock lockGuard{mInstance.mMutexCurrentlyValidatingBlocks};

            bool alreadyValidating =
                std::any_of(
                    mInstance.mCurrentlyValidatingBlocks.begin(),
                    mInstance.mCurrentlyValidatingBlocks.end(),
                    [&index](const CBlockIndex* other)
                    {
                        return other == &index;
                    });

            if (alreadyValidating)
            {
                // Same block already being validated - if this happens we have a bug
                throw CBestBlockAttachmentCancellation{};
            }

            mInstance.mCurrentlyValidatingBlocks.push_back(&mIndex);
        }

        ~CScopeGuard()
        {
            std::scoped_lock lockGuard{mInstance.mMutexCurrentlyValidatingBlocks};
            mInstance.mCurrentlyValidatingBlocks.erase(
                std::find(
                    mInstance.mCurrentlyValidatingBlocks.begin(),
                    mInstance.mCurrentlyValidatingBlocks.end(),
                    &mIndex));
        }

    private:
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members) 
        CBlockValidationStatus& mInstance;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members) 
        const CBlockIndex& mIndex;
    };

    CScopeGuard getScopedCurrentlyValidatingBlock(const CBlockIndex& index)
    {
        return {*this, index};
    }

    bool isAncestorInValidation(const CBlockIndex& index) const
    {
        std::lock_guard lockGuard{mMutexCurrentlyValidatingBlocks};

        return
            std::any_of(
                mCurrentlyValidatingBlocks.begin(),
                mCurrentlyValidatingBlocks.end(),
                [&index](const CBlockIndex* other)
                {
                    return index.GetAncestor(other->GetHeight()) == other;
                });
    }

    bool areNSiblingsInValidation(
        const CBlockIndex& index,
        int thresholdNumber) const
    {
        std::lock_guard lockGuard{mMutexCurrentlyValidatingBlocks};

        int count =
            // NOLINTNEXTLINE(*-narrowing-conversions)
            std::count_if(
                mCurrentlyValidatingBlocks.begin(),
                mCurrentlyValidatingBlocks.end(),
                [&index](const CBlockIndex* other)
                {
                    return index.GetHeight() == other->GetHeight();
                });

        return count >= thresholdNumber;
    }

    std::vector<uint256> getCurrentlyValidatingBlocks() const
    {
        std::lock_guard<std::mutex> lockGuard(mMutexCurrentlyValidatingBlocks);
        std::vector<uint256> result;
        result.reserve(mCurrentlyValidatingBlocks.size());
        for (auto validatingIndex : mCurrentlyValidatingBlocks)
        {
            result.emplace_back(validatingIndex->GetBlockHash());
        }
        return result;
    }

    std::vector<uint256> getWaitingAfterValidationBlocks() const
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

    void waitIfRequired(
        const uint256& blockHash,
        const task::CCancellationToken& token) const
    {
        bool foundFlag = false;

        // if blockHash is not in mWaitAfterValidation
        // then we break while loop
        do // NOLINT(cppcoreguidelines-avoid-do-while) 
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
        } while (foundFlag && !token.IsCanceled());
    }
};

#endif // BITCOIN_BLOCK_VALIDATION_H
