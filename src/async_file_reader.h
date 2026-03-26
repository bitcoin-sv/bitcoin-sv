// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_ASYNC_FILE_READER_H
#define BITCOIN_ASYNC_FILE_READER_H

#include <cassert>
#include <cstring>
#include <memory>

#include "cfile_util.h"

#ifdef _WIN32

    /**
     * NOTE: For now we don't support async reader for Windows so we just alias
     *       sync reader and pretend that it's asynchronous.
     */
    using CAsyncFileReader = CFileReader;

#else

    #include <sys/types.h>
    #include <aio.h>
    #include <errno.h>

    /**
     * Async RAII file reader for use with streams that want to take ownership of
     * the underlying FILE pointer. File pointer is closed once the CAsyncFileReader
     * instance gets out of scope.
     */
    class CAsyncFileReader
    {
    public:
        explicit CAsyncFileReader(UniqueCFile file):
            mFile{std::move(file)},
            mControllBlock{std::make_unique<aiocb>()}
        {
            assert(mFile);
            mOffset = ftell(mFile.get());
            mFileId = fileno(mFile.get());
            assert(mFileId != -1);
        }

        ~CAsyncFileReader()
        {
            if(mReadInProgress)
            {
                aio_cancel(mFileId, mControllBlock.get());
            }
        }

        CAsyncFileReader(const CAsyncFileReader&) = delete;
        CAsyncFileReader& operator=(const CAsyncFileReader&) = delete;

        CAsyncFileReader(CAsyncFileReader&& other) noexcept:
            mFile{std::move(other.mFile)},
            mFileId{other.mFileId},
            mControllBlock{std::move(other.mControllBlock)},
            mOffset{other.mOffset},
            mReadInProgress{other.mReadInProgress},
            mEndOfStream{other.mEndOfStream}
        {
            other.mReadInProgress = false;
        }

        CAsyncFileReader& operator=(CAsyncFileReader&& other) noexcept
        {
            if(this != &other)
            {
                if(mReadInProgress)
                {
                    aio_cancel(mFileId, mControllBlock.get());
                }
                mFile = std::move(other.mFile);
                mFileId = other.mFileId;
                mControllBlock = std::move(other.mControllBlock);
                mOffset = other.mOffset;
                mReadInProgress = other.mReadInProgress;
                mEndOfStream = other.mEndOfStream;
                other.mReadInProgress = false;
            }
            return *this;
        }

        /**
         * pch: buffer to which data will be read
         * maxSize: maximum size that pch can hold
         *
         * Once the read request is made the buffer pch is pointing to must
         * exist untill the read is complete (size greater than 0 is returned)
         * or CAsyncFileReader instance is destroyed (destructor terminates
         * pending read request).
         */
        size_t Read(char* pch, size_t maxSize)
        {
            if(EndOfStream() || !mFile)
            {
                return 0;
            }

            if(!mReadInProgress)
            {
                memset(mControllBlock.get(), 0, sizeof(aiocb));
                mControllBlock->aio_nbytes = maxSize;
                mControllBlock->aio_fildes = mFileId;
                mControllBlock->aio_offset = mOffset;
                mControllBlock->aio_buf = pch;

                EnqueueReadRequest(*mControllBlock);

                mReadInProgress = true;
            }

            if(IsReadRequestDone())
            {
                const auto numBytes{aio_return(mControllBlock.get())};
                mReadInProgress = false;

                if(numBytes < 0)
                {
                    CloseFile();
                    throw
                        std::ios_base::failure(
                            "CAsyncFileReader::Read: read failed");
                }
                else if(numBytes > 0)
                {
                    mOffset += numBytes;
                }
                else
                {
                    mEndOfStream = true;
                }

                return numBytes;
            }

            return 0;
        }

        bool EndOfStream() const
        {
            return mEndOfStream;
        }

    private:
        void CloseFile()
        {
            mFile.reset();
        }

        void EnqueueReadRequest(aiocb& controllBlock)
        {
            if (aio_read(&controllBlock) == -1)
            {
                CloseFile();
                throw
                    std::ios_base::failure(
                        "CAsyncFileReader::Read: read scheduling failed");
            }
        }

        bool IsReadRequestDone() const
        {
            return aio_error(mControllBlock.get()) != EINPROGRESS;
        }

        UniqueCFile mFile;
        int mFileId{};
        std::unique_ptr<aiocb> mControllBlock;
        off_t mOffset{};

        bool mReadInProgress = false;
        bool mEndOfStream = false;
    };

#endif

#endif // BITCOIN_ASYNC_FILE_READER_H
