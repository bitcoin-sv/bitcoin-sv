// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cfile_util.h>
#include <fs.h>
#include <primitives/transaction.h>

#include <memory>

/**
 * Class to help serialising double-spend transactions to disk, controlling
 * the life of those transaction files, and accessing them for later streaming
 * to a double-spend endpoint.
 */
class DSTxnSerialiser final
{
  public:
    DSTxnSerialiser();
    ~DSTxnSerialiser();

    DSTxnSerialiser(const DSTxnSerialiser&) = delete;
    DSTxnSerialiser(DSTxnSerialiser&&) = default;
    DSTxnSerialiser& operator=(const DSTxnSerialiser&) = delete;
    DSTxnSerialiser& operator=(DSTxnSerialiser&&) = default;

    // A handle onto a serialised double-spend txn.
    // When this goes out of scope the serialised txn file is deleted.
    class TxnHandle final
    {
      public:
        TxnHandle(const fs::path& txnFile) : mTxnFile{txnFile} {}
        ~TxnHandle();

        // Movable, non-copyable
        TxnHandle(const TxnHandle&) = delete;
        TxnHandle(TxnHandle&&) = default;
        TxnHandle& operator=(const TxnHandle&) = delete;
        TxnHandle& operator=(TxnHandle&&) = default;

        // Open the underlying file for reading and return a descriptor to it.
        [[nodiscard]] UniqueFileDescriptor OpenFile() const;

        // Accessors
        [[nodiscard]] const fs::path& GetFile() const { return mTxnFile; }
        [[nodiscard]] size_t GetFileSize() const;

      private:
        // Full path to underlying txn file
        fs::path mTxnFile {};
    };
    using TxnHandleUPtr = std::unique_ptr<TxnHandle>;
    using TxnHandleSPtr = std::shared_ptr<TxnHandle>;

    // Serialise the given transaction to disk
    [[nodiscard]] TxnHandleUPtr Serialise(const CTransaction& txn);

  private:

    // Create / remove our working data dir
    void MakeDataDir() const;
    void RemoveDataDir() const;

    // Full path to our directory for storing serialised txns
    fs::path mTxnDir {};

};

