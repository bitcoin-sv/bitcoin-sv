// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_ASYNC_FILE_READER_H
#define BITCOIN_ASYNC_FILE_READER_H

#include <cstdio>
#include <memory>
#include <string>

#include "streams.h"

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
        CAsyncFileReader(std::unique_ptr<FILE, CCloseFile>&& file)
            : mFile{std::move(file)}
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
                aio_cancel(mFileId, &mControllBlock);
            }
        }

        CAsyncFileReader(CAsyncFileReader&& other)
            : mFileId{other.mFileId}
            , mOffset{other.mOffset}
            , mEndOfStream{other.mEndOfStream}
        {
            // Check that we aren't moving while read is in progress as
            // aio_return takes a reference to other.mControllBlock instance
            assert(other.mReadInProgress == false);

            mFile = std::move(other.mFile);
        }

        // Move assignment is never used (nor expected to ever be) so we mark it
        // as deleted since default version would not be strict enough as it
        // would require the same mReadInProgress check as move constructor.
        CAsyncFileReader& operator=(CAsyncFileReader&& other) = delete;

        CAsyncFileReader(const CAsyncFileReader&) = delete;
        CAsyncFileReader& operator=(const CAsyncFileReader&) = delete;

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
                memset(&mControllBlock, 0, sizeof(aiocb));
                mControllBlock.aio_nbytes = maxSize;
                mControllBlock.aio_fildes = mFileId;
                mControllBlock.aio_offset = mOffset;
                mControllBlock.aio_buf = pch;

                EnqueueReadRequest(mControllBlock);

                mReadInProgress = true;
            }

            if(IsReadRequestDone(mControllBlock))
            {
                int numBytes = aio_return(&mControllBlock);
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

        static bool IsReadRequestDone(aiocb& controllBlock)
        {
            return aio_error(&controllBlock) != EINPROGRESS;
        }

        std::unique_ptr<FILE, CCloseFile> mFile;
        int mFileId;
        size_t mOffset;
        aiocb mControllBlock;
        bool mReadInProgress = false;
        bool mEndOfStream = false;
    };

#endif

#endif // BITCOIN_ASYNC_FILE_READER_H
