// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DBWRAPPER_H
#define BITCOIN_DBWRAPPER_H

#include "clientversion.h"
#include "fs.h"
#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

#include <string_view>
#include <memory>

static const size_t DBWRAPPER_PREALLOC_KEY_SIZE = 64;
static const size_t DBWRAPPER_PREALLOC_VALUE_SIZE = 1024;

class dbwrapper_error : public std::runtime_error {
public:
    dbwrapper_error(const std::string &msg) : std::runtime_error(msg) {}
};

class CDBWrapper;

/**
 * These should be considered an implementation detail of the specific database.
 */
namespace dbwrapper_private {

/**
 * Handle database error by throwing dbwrapper_error exception.
 */
void HandleError(const leveldb::Status &status);

/**
 * Work around circular dependency, as well as for testing in dbwrapper_tests.
 * Database obfuscation should be considered an implementation detail of the
 * specific database.
 */
const std::vector<uint8_t> &GetObfuscateKey(const CDBWrapper &w);

/**
 * Input stream that can be used to unserialize data from external buffer without
 * creating unnecessary copies.
 *
 * This class holds a non-owning reference (string_view) to external buffer which must
 * therefore outlive CDataStreamInput object.
 */
// NOTE: Class is based on CDataStream and provides only read functionality
class CDataStreamInput
{
    std::string_view buf;
    const std::vector<std::uint8_t>& obfuscate_key;
    unsigned int nReadPos;

public:
    typedef std::string_view::size_type size_type;
    typedef std::string_view::const_reference const_reference;
    typedef std::string_view::value_type value_type;
    typedef std::string_view::const_iterator const_iterator;

    /**
     * Constructor
     *
     * @param buf External buffer that contains previously serialized data
     * @param obfuscate_key Key used to de-obfuscate serialized data in buffer
     *
     * @note Both buf and obfuscate_key must outlive constructed CDataStreamInput object.
     */
    CDataStreamInput(std::string_view buf, const std::vector<uint8_t>& obfuscate_key)
        : buf(buf)
        , obfuscate_key(obfuscate_key)
        , nReadPos(0)
    {}

    // Access to serialized data
    const value_type* data() const { return buf.data() + nReadPos; }
    size_type size() const { return buf.size() - nReadPos; }

    //
    // Stream subset
    //
    bool eof() const { return size() == 0; }

    // Since this class is only used to read from level DB,
    // values for stream type and version are fixed.
    int GetType() const { return SER_DISK; }
    int GetVersion() const { return CLIENT_VERSION; }

    void read(char* pch, std::size_t nSize)
    {
        if (nSize == 0) {
            return;
        }

        // Read from buffer at current position
        unsigned int nReadPosNext = nReadPos + nSize;
        if (nReadPosNext >= buf.size())
        {
            if (nReadPosNext > buf.size())
            {
                throw std::ios_base::failure("CDataStreamInput::read(): end of data");
            }

            memcpy(pch, &buf[nReadPos], nSize);
            XorBuf(pch, nSize, nReadPos);
            nReadPos = 0;
            buf = {};
            return;
        }
        memcpy(pch, &buf[nReadPos], nSize);
        XorBuf(pch, nSize, nReadPos);
        nReadPos = nReadPosNext;
    }

    void ignore(int nSize)
    {
        // Ignore from buffer at current position
        if (nSize < 0)
        {
            throw std::ios_base::failure("CDataStreamInput::ignore(): nSize negative");
        }

        unsigned int nReadPosNext = nReadPos + nSize;
        if (nReadPosNext >= buf.size())
        {
            if (nReadPosNext > buf.size())
            {
                throw std::ios_base::failure("CDataStreamInput::ignore(): end of data");
            }

            nReadPos = 0;
            buf = {};
            return;
        }
        nReadPos = nReadPosNext;
    }

    template<typename T>
    CDataStreamInput& operator>>(T& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }

private:
    /**
     * XOR the contents of given buffer with de-obfuscation key
     *
     * It is used by read() method to de-obfuscate each chunk of stream as it is being read.
     *
     * @param buf Buffer that will be xor-ed
     * @param bufSize Size of buffer
     * @param readPos Read position from start of stream. Used to calculate proper offset into key.
     */
    void XorBuf(char* buf, std::size_t bufSize, unsigned int readPos)
    {
        if (obfuscate_key.size() == 0)
        {
            return;
        }

        for (size_type i = 0, j = readPos % obfuscate_key.size(); i != bufSize; i++)
        {
            buf[i] ^= obfuscate_key[j++];

            // This potentially acts on very many bytes of data, so it's
            // important that we calculate `j`, i.e. the `key` index in this way
            // instead of doing a %, which would effectively be a division for
            // each byte Xor'd -- much slower than need be.
            if (j == obfuscate_key.size()) j = 0;
        }
    }
};

} // namespace dbwrapper_private

/** Batch of changes queued to be written to a CDBWrapper */
class CDBBatch {
    friend class CDBWrapper;

private:
    const CDBWrapper &parent;
    leveldb::WriteBatch batch;

    CDataStream ssKey;
    CDataStream ssValue;

    size_t size_estimate;

public:
    /**
     * @param[in] _parent   CDBWrapper that this batch is to be submitted to
     */
    CDBBatch(const CDBWrapper &_parent)
        : parent(_parent), ssKey(SER_DISK, CLIENT_VERSION),
          ssValue(SER_DISK, CLIENT_VERSION), size_estimate(0){};

    void Clear() {
        batch.Clear();
        size_estimate = 0;
    }

    template <typename K, typename V> void Write(const K &key, const V &value) {
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        ssValue.reserve(DBWRAPPER_PREALLOC_VALUE_SIZE);
        ssValue << value;
        ssValue.Xor(dbwrapper_private::GetObfuscateKey(parent));
        leveldb::Slice slValue(ssValue.data(), ssValue.size());

        batch.Put(slKey, slValue);
        // LevelDB serializes writes as:
        // - byte: header
        // - varint: key length (1 byte up to 127B, 2 bytes up to 16383B, ...)
        // - byte[]: key
        // - varint: value length
        // - byte[]: value
        // The formula below assumes the key and value are both less than 16k.
        size_estimate += 3 + (slKey.size() > 127) + slKey.size() +
                         (slValue.size() > 127) + slValue.size();
        ssKey.clear();
        ssValue.clear();
    }

    template <typename K> void Erase(const K &key) {
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        batch.Delete(slKey);
        // LevelDB serializes erases as:
        // - byte: header
        // - varint: key length
        // - byte[]: key
        // The formula below assumes the key is less than 16kB.
        size_estimate += 2 + (slKey.size() > 127) + slKey.size();
        ssKey.clear();
    }

    size_t SizeEstimate() const { return size_estimate; }
};

class CDBIterator {
private:
    const CDBWrapper &parent;
    leveldb::Iterator *piter;

public:
    /**
     * @param[in] _parent          Parent CDBWrapper instance.
     * @param[in] _piter           The original leveldb iterator.
     */
    CDBIterator(const CDBIterator&) = delete;
    CDBIterator& operator=(const CDBIterator&) = delete;
    CDBIterator(CDBIterator&&) = delete;
    CDBIterator& operator=(CDBIterator&&) = delete;
    CDBIterator(const CDBWrapper &_parent, leveldb::Iterator *_piter)
        : parent(_parent), piter(_piter){};
    ~CDBIterator();

    bool Valid();

    void SeekToFirst();

    template <typename K> void Seek(const K &key) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());
        piter->Seek(slKey);
    }

    void Next();

    template <typename K> bool GetKey(K &key) {
        leveldb::Slice slKey = piter->key();
        try {
            CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(),
                              SER_DISK, CLIENT_VERSION);
            ssKey >> key;
        } catch (const std::exception &) {
            return false;
        }
        return true;
    }

    unsigned int GetKeySize() { return piter->key().size(); }

    /**
     * Default for TStream template parameter in GetValue() member function
     */
    template<class TBase>
    using GetValue_TStreamDefault  = TBase;

    // See comments in CDBWrapper::Read() method for description of template parameters and usage principles.
    template <template<class TBase> class TStream = GetValue_TStreamDefault, typename V, typename... Args>
    bool GetValue(V &value, Args&&... args) {
        leveldb::Slice slValue = piter->value();
        try {
            // Create data stream optimized for reading and unserialize the value
            static_assert(std::is_base_of<dbwrapper_private::CDataStreamInput, TStream<dbwrapper_private::CDataStreamInput>>::value, "TStream must be a class template derived from TBase!");
            TStream<dbwrapper_private::CDataStreamInput> ssValue( std::string_view(slValue.data(), slValue.size()),
                dbwrapper_private::GetObfuscateKey(parent),
                std::forward<Args>(args)... );
            ssValue >> value;
        } catch (const std::exception &) {
            return false;
        }
        return true;
    }

    unsigned int GetValueSize() { return piter->value().size(); }
};

class CDBWrapper {
    friend const std::vector<uint8_t> &
    dbwrapper_private::GetObfuscateKey(const CDBWrapper &w);

private:
    //! custom environment this database is using (may be nullptr in case of
    //! default environment)
    leveldb::Env *penv;

    //! database options used
    leveldb::Options options;

    //! options used when reading from the database
    leveldb::ReadOptions readoptions;

    //! options used when iterating over values of the database
    leveldb::ReadOptions iteroptions;

    //! options used when writing to the database
    leveldb::WriteOptions writeoptions;

    //! options used when sync writing to the database
    leveldb::WriteOptions syncoptions;

    //! the database itself
    leveldb::DB *pdb;

    //! a key used for optional XOR-obfuscation of the database
    std::vector<uint8_t> obfuscate_key;

    //! the key under which the obfuscation key is stored
    static const std::string OBFUSCATE_KEY_KEY;

    //! the length of the obfuscate key in number of bytes
    static const unsigned int OBFUSCATE_KEY_NUM_BYTES;

    std::vector<uint8_t> CreateObfuscateKey() const;

public:
    struct MaxFiles {
        const size_t maxFiles;
        explicit MaxFiles(size_t maxFiles_) : maxFiles{maxFiles_} {}
        static MaxFiles Default() { return MaxFiles{64}; }
    };

    /**
     * @param[in] path        Location in the filesystem where leveldb data will
     * be stored.
     * @param[in] nCacheSize  Configures various leveldb cache settings.
     * @param[in] fMemory     If true, use leveldb's memory environment.
     * @param[in] fWipe       If true, remove all existing data.
     * @param[in] obfuscate   If true, store data obfuscated via simple XOR. If
     * false, XOR
     *                        with a zero'd byte array.
     */
    CDBWrapper(const CDBWrapper&) = delete;
    CDBWrapper& operator=(const CDBWrapper&) = delete;
    CDBWrapper(CDBWrapper&&) = delete;
    CDBWrapper& operator=(CDBWrapper&&) = delete;
    CDBWrapper(const fs::path &path, size_t nCacheSize, bool fMemory = false,
               bool fWipe = false, bool obfuscate = false,
               MaxFiles nMaxFiles = MaxFiles::Default());
    ~CDBWrapper();

public:
    /**
     * Default for TStream template parameter in Read() member function
     */
    template<class TBase>
    using Read_TStreamDefault  = TBase;

    /**
     * Retrieve value for given key from database and unserialize it into value object.
     *
     * @param TStream Stream class providing required unserialization functionality (i.e. as in CDataStreamInput).
     *                This is a template template parameter so that custom unserialization functionality can be provided
     *                by creating a class template derived from required TBase that is provided by implementation
     *                of this method (CRTP).
     * @param args Additional arguments passed to TStream constructor (besides buffer with serialized data and obfuscation key).
     */
    template <template<class TBase> class TStream = Read_TStreamDefault, typename K, typename V, typename... Args>
    bool Read(const K& key, V& value, Args&&... args) const
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        std::string strValue;
        leveldb::Status status = pdb->Get(readoptions, slKey, &strValue);
        if (!status.ok()) {
            if (status.IsNotFound()) return false;
            LogPrintf("LevelDB read failure: %s\n", status.ToString());
            dbwrapper_private::HandleError(status);
        }
        try {
            // Create data stream optimized for reading and unserialize the value
            static_assert(std::is_base_of<dbwrapper_private::CDataStreamInput, TStream<dbwrapper_private::CDataStreamInput>>::value, "TStream must be a class template derived from TBase!");
            TStream<dbwrapper_private::CDataStreamInput> ssValue( strValue,
                                                                  obfuscate_key,
                                                                  std::forward<Args>(args)... );
            ssValue >> value;
        } catch (const std::exception &) {
            return false;
        }
        return true;
    }

    template <typename K, typename V>
    bool Write(const K &key, const V &value, bool fSync = false) {
        CDBBatch batch(*this);
        batch.Write(key, value);
        return WriteBatch(batch, fSync);
    }

    template <typename K> bool Exists(const K &key) const {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        leveldb::Slice slKey(ssKey.data(), ssKey.size());

        std::string strValue;
        leveldb::Status status = pdb->Get(readoptions, slKey, &strValue);
        if (!status.ok()) {
            if (status.IsNotFound()) return false;
            LogPrintf("LevelDB read failure: %s\n", status.ToString());
            dbwrapper_private::HandleError(status);
        }
        return true;
    }

    template <typename K> bool Erase(const K &key, bool fSync = false) {
        CDBBatch batch(*this);
        batch.Erase(key);
        return WriteBatch(batch, fSync);
    }

    bool WriteBatch(CDBBatch &batch, bool fSync = false);

    // not available for LevelDB; provide for compatibility with BDB
    bool Flush() { return true; }

    bool Sync() {
        CDBBatch batch(*this);
        return WriteBatch(batch, true);
    }

    CDBIterator *NewIterator() {
        return new CDBIterator(*this, pdb->NewIterator(iteroptions));
    }

    /**
     * Return true if the database managed by this class contains no entries.
     */
    bool IsEmpty();

    template <typename K>
    size_t EstimateSize(const K &key_begin, const K &key_end) const {
        CDataStream ssKey1(SER_DISK, CLIENT_VERSION),
            ssKey2(SER_DISK, CLIENT_VERSION);
        ssKey1.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey2.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey1 << key_begin;
        ssKey2 << key_end;
        leveldb::Slice slKey1(ssKey1.data(), ssKey1.size());
        leveldb::Slice slKey2(ssKey2.data(), ssKey2.size());
        uint64_t size = 0;
        leveldb::Range range(slKey1, slKey2);
        pdb->GetApproximateSizes(&range, 1, &size);
        return size;
    }

    /**
     * Compact a certain range of keys in the database.
     */
    template <typename K>
    void CompactRange(const K &key_begin, const K &key_end) const {
        CDataStream ssKey1(SER_DISK, CLIENT_VERSION),
            ssKey2(SER_DISK, CLIENT_VERSION);
        ssKey1.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey2.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey1 << key_begin;
        ssKey2 << key_end;
        leveldb::Slice slKey1(ssKey1.data(), ssKey1.size());
        leveldb::Slice slKey2(ssKey2.data(), ssKey2.size());
        pdb->CompactRange(&slKey1, &slKey2);
    }
};

#endif // BITCOIN_DBWRAPPER_H
